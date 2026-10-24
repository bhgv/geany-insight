/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "ai_edit_session.h"
#include <gio/gio.h>
#include <glib/gstdio.h>
#include <errno.h>
#include <string.h>
static void variant_free(gpointer p) { InsightAiVariant *v=p;g_free(v->code);g_free(v->requirements);g_free(v->explanation);g_free(v); }
static gboolean valid(const gchar *s,gsize max) { return s && strlen(s)<=max && g_utf8_validate(s,-1,NULL); }
InsightAiEdits *insight_ai_edits_new(const gchar *code) {
 InsightAiEdits *s=g_new0(InsightAiEdits,1);s->variants=g_ptr_array_new_with_free_func(variant_free);if(!insight_ai_edits_push(s,code,"","Original source snapshot.",NULL)) { insight_ai_edits_free(s);return NULL; }return s;
}
void insight_ai_edits_free(InsightAiEdits *s) { if(s) { g_ptr_array_unref(s->variants);g_free(s); } }
const InsightAiVariant *insight_ai_edits_current(const InsightAiEdits *s) { return s && s->variants->len ? g_ptr_array_index(s->variants,s->position):NULL; }
gboolean insight_ai_edits_push(InsightAiEdits *s,const gchar *code,const gchar *requirements,const gchar *explanation,GError **error) {
 if(!valid(code,INSIGHT_AI_SOURCE_LIMIT) || !valid(requirements,INSIGHT_AI_REQUIREMENTS_LIMIT) || !valid(explanation,262144)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"AI variant exceeds its UTF-8 limit.");return FALSE; }
 const InsightAiVariant *old=insight_ai_edits_current(s);if(old && !strcmp(old->code,code) && !strcmp(old->requirements,requirements) && !strcmp(old->explanation,explanation)) return TRUE;
 while(s->variants->len>s->position+1) g_ptr_array_remove_index(s->variants,s->variants->len-1);
 InsightAiVariant *v=g_new0(InsightAiVariant,1);v->code=g_strdup(code);v->requirements=g_strdup(requirements);v->explanation=g_strdup(explanation);g_ptr_array_add(s->variants,v);s->position=s->variants->len-1;
 /* At most 32 variants per file; bounded to about 13 MiB. */
 while(s->variants->len>32) { g_ptr_array_remove_index(s->variants,0);s->position--; }return TRUE;
}
gboolean insight_ai_edits_move(InsightAiEdits *s,gint step) { gint64 n=(gint64)s->position+step;if(n<0 || n>=s->variants->len) return FALSE;s->position=n;return TRUE; }
gchar *insight_ai_requirements_append(const gchar *old,const gchar *extra,GError **error) {
 if(!valid(old,INSIGHT_AI_REQUIREMENTS_LIMIT) || !valid(extra,4096)) goto invalid;
 gchar *trim=g_strdup(extra);g_strstrip(trim);if(!*trim) { g_free(trim);goto invalid; }
 gchar *all=*old ? g_strconcat(old,"\n\n",trim,NULL):g_strdup(trim);g_free(trim);if(strlen(all)<=INSIGHT_AI_REQUIREMENTS_LIMIT) return all;g_free(all);
invalid:g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Enter an additional request (up to 4 KiB); accumulated requirements are limited to 16 KiB.");return NULL;
}
static void prompt_free(gpointer p) { InsightAiPrompt *q=p;g_free(q->text);g_free(q); }
gchar *insight_ai_prompts_path(const gchar *config,const gchar *project,const gchar *scope) {
 if(!scope || !*scope || strlen(scope)>64) return NULL;for(const gchar *c=scope;*c;c++) if(!g_ascii_isalnum(*c) && *c!='-') return NULL;
 gchar *dir=project && *project ? g_strconcat(project,".ai-history",NULL):g_build_filename(config,"insight","ai-history",NULL),*name=g_strconcat(scope,".conf",NULL),*path=g_build_filename(dir,name,NULL);g_free(name);g_free(dir);return path;
}
static GPtrArray *prompts_read(const gchar *path,GError **error) {
 GPtrArray *a=g_ptr_array_new_with_free_func(prompt_free);GFile *file=g_file_new_for_path(path);GError *e=NULL;GFileInputStream *stream=g_file_read(file,NULL,&e);g_object_unref(file);
 if(!stream) { if(g_error_matches(e,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) { g_clear_error(&e);return a; }g_propagate_error(error,e);g_ptr_array_unref(a);return NULL; }
 gchar *raw=g_malloc(4194305);gsize n=0;gboolean ok=g_input_stream_read_all(G_INPUT_STREAM(stream),raw,4194304,&n,NULL,&e);g_object_unref(stream);GKeyFile *k=g_key_file_new();
 if(ok && (n>=4194304 || memchr(raw,0,n) || !g_utf8_validate(raw,n,NULL))) { g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid or oversized AI request history; existing data was preserved.");ok=FALSE; }
 if(ok && n) ok=g_key_file_load_from_data(k,raw,n,G_KEY_FILE_NONE,&e);
 if(ok) for(guint i=0;i<100;i++) { gchar *group=g_strdup_printf("prompt-%u",i),*text=g_key_file_get_string(k,group,"text",NULL);if(valid(text,16384) && *text) { InsightAiPrompt *q=g_new0(InsightAiPrompt,1);q->text=text;q->count=CLAMP(g_key_file_get_integer(k,group,"count",NULL),1,1000000);q->last=g_key_file_get_int64(k,group,"last",NULL);g_ptr_array_add(a,q); }else g_free(text);g_free(group); }
 g_free(raw);g_key_file_unref(k);if(!ok) { g_propagate_error(error,e);g_ptr_array_unref(a);return NULL; }return a;
}
GPtrArray *insight_ai_prompts_load(const gchar *path) { GPtrArray *a=prompts_read(path,NULL);return a ? a:g_ptr_array_new_with_free_func(prompt_free); }
static gboolean prompts_write(GPtrArray *a,const gchar *path,GError **error) {
 gchar *dir=g_path_get_dirname(path);if(g_mkdir_with_parents(dir,0700)<0) { g_set_error(error,G_FILE_ERROR,g_file_error_from_errno(errno),"Cannot create AI history directory: %s",g_strerror(errno));g_free(dir);return FALSE; }g_free(dir);
 GKeyFile *k=g_key_file_new();for(guint i=0;i<a->len;i++) { InsightAiPrompt *p=g_ptr_array_index(a,i);gchar *group=g_strdup_printf("prompt-%u",i);g_key_file_set_string(k,group,"text",p->text);g_key_file_set_integer(k,group,"count",p->count);g_key_file_set_int64(k,group,"last",p->last);g_free(group); }
 gsize n;gchar *data=g_key_file_to_data(k,&n,NULL);GFile *file=g_file_new_for_path(path);gboolean ok=g_file_replace_contents(file,data,n,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);g_object_unref(file);g_free(data);g_key_file_unref(k);return ok;
}
static void prompts_replace(GPtrArray *a,GPtrArray *disk) { g_ptr_array_set_size(a,0);g_ptr_array_set_free_func(disk,NULL);for(guint i=0;i<disk->len;i++) g_ptr_array_add(a,g_ptr_array_index(disk,i));g_ptr_array_unref(disk); }
gboolean insight_ai_prompts_record(GPtrArray *a,const gchar *path,const gchar *text,GError **error) {
 if(!valid(text,16384) || !*text) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid saved prompt.");return FALSE; }
 gchar *stable=g_strdup(text);GPtrArray *disk=prompts_read(path,error);if(!disk) { g_free(stable);return FALSE; }InsightAiPrompt *q=NULL;gint64 latest=0;
 for(guint i=0;i<disk->len;i++) { InsightAiPrompt *p=g_ptr_array_index(disk,i);latest=MAX(latest,p->last);if(!strcmp(p->text,stable)) q=p; }
 if(!q) { if(disk->len>=100) { guint oldest=0;for(guint i=1;i<disk->len;i++) if(((InsightAiPrompt *)g_ptr_array_index(disk,i))->last<((InsightAiPrompt *)g_ptr_array_index(disk,oldest))->last) oldest=i;g_ptr_array_remove_index(disk,oldest); }q=g_new0(InsightAiPrompt,1);q->text=g_strdup(stable);g_ptr_array_add(disk,q); }
 q->count=MIN(q->count,999999)+1;q->last=MAX(g_get_real_time(),latest<G_MAXINT64 ? latest+1:latest);gboolean ok=prompts_write(disk,path,error);if(ok) prompts_replace(a,disk);else g_ptr_array_unref(disk);g_free(stable);return ok;
}
gboolean insight_ai_prompts_remove(GPtrArray *a,const gchar *path,const gchar *text,GError **error) {
 gchar *stable=g_strdup(text);GPtrArray *disk=prompts_read(path,error);if(!disk) { g_free(stable);return FALSE; }
 if(!stable) g_ptr_array_set_size(disk,0);else for(guint i=disk->len;i>0;i--) if(!strcmp(((InsightAiPrompt *)g_ptr_array_index(disk,i-1))->text,stable)) g_ptr_array_remove_index(disk,i-1);
 gboolean ok=prompts_write(disk,path,error);if(ok) prompts_replace(a,disk);else g_ptr_array_unref(disk);g_free(stable);return ok;
}
static const gchar *names[]={"Syntax and diagnostics","Logic and edge cases","Format code","Improve solution","Readable names","Algorithms","Function reference","Add useful comments","Write documentation","Project consistency"};
static const gchar *tasks[]={
 "Review syntax and likely compiler diagnostics. Explain locations and fixes; distinguish model reasoning from actual compiler validation.",
 "Find logical errors, boundary cases, invalid assumptions and resource/lifetime problems. Explain evidence and propose focused corrections.",
 "Format the source consistently with its existing style. Preserve behavior and unrelated content.",
 "Propose a better solution with clear trade-offs in correctness, complexity, maintainability and resource use. Preserve public contracts unless explicitly asked otherwise.",
 "Improve readability of function, variable, class, struct and macro names. Update uses within the target, preserve public interfaces unless approved, and identify cross-file changes still needed.",
 "Explain suitable algorithms and alternatives, their preconditions, time and space complexity. Propose the best justified implementation for this source.",
 "Explain the selected source's functions, parameters, return values, contracts and usage. Mark uncertain library or version-specific behavior rather than inventing references.",
 "Add concise comments explaining intent, invariants and non-obvious decisions. Avoid comments that merely repeat code.",
 "Write useful documentation and API/docstring comments: purpose, parameters, return values, errors, examples and limitations. Preserve behavior.",
 "Review the target and every selected project-context file together for syntax, logic, incompatible interfaces, naming and data-flow errors. Report file/line evidence and uncertainty. Propose code only for the target; list corrections needed in other files for their own review."};
guint insight_ai_task_count(void) { return G_N_ELEMENTS(names); }
const gchar *insight_ai_task_name(guint i) { return i<G_N_ELEMENTS(names) ? names[i]:NULL; }
const gchar *insight_ai_task_prompt(guint i) { return i<G_N_ELEMENTS(tasks) ? tasks[i]:NULL; }
