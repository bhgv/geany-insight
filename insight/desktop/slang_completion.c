/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "slang_completion.h"
#include <glib/gstdio.h>
#include <string.h>

gboolean insight_slang_source(const gchar *path) {
 const gchar *dot=path ? strrchr(path,'.'):NULL;
 return dot && (!g_ascii_strcasecmp(dot,".v") || !g_ascii_strcasecmp(dot,".sv") || !g_ascii_strcasecmp(dot,".vh") || !g_ascii_strcasecmp(dot,".svh"));
}
static void candidate_free(gpointer data) {
 InsightClangCompletion *c=data;g_free(c->name);g_free(c->signature);g_free(c->type);g_free(c->comment);g_ptr_array_unref(c->parameters);g_free(c);
}
static gchar *unhex(const gchar *value) {
 gsize n=strlen(value);if(n>32768 || n%2) return NULL;gchar *result=g_malloc(n/2+1);
 for(gsize i=0;i<n;i+=2) {gint a=g_ascii_xdigit_value(value[i]),b=g_ascii_xdigit_value(value[i+1]);if(a<0 || b<0 || !(a|b)) {g_free(result);return NULL;}result[i/2]=(a<<4)|b;}
 result[n/2]=0;if(!g_utf8_validate(result,-1,NULL) || strpbrk(result,"\r\n\t")) {g_free(result);return NULL;}return result;
}
static void terminate(GCancellable *cancel,gpointer data) {(void)cancel;g_subprocess_force_exit(G_SUBPROCESS(data));}
GPtrArray *insight_slang_complete(const gchar *service,const gchar *path,const gchar *text,
 guint offset,guint rootlen,GPtrArray *paths,GHashTable *buffers,GCancellable *cancel) {
 if(!service || !*service || !path || !text || offset>strlen(text) || rootlen>offset || !paths || !buffers || (cancel && g_cancellable_is_cancelled(cancel))) return NULL;
 guint start=offset-rootlen,begin=start,line=1;
 for(guint i=0;i<offset;i++) if(text[i]=='\n') line++;
 /* Only plain identifier hierarchies. Indexed/dynamic receivers decline. */
 while(begin && (g_ascii_isalnum(text[begin-1]) || strchr("_$.:",text[begin-1]))) begin--;
 gchar *receiver=g_strndup(text+begin,start-begin),*prefix=g_strndup(text+start,rootlen);
 gsize size=strlen(receiver);if(size && receiver[size-1]=='.') receiver[--size]=0;
 else if(size>=2 && receiver[size-1]==':' && receiver[size-2]==':') receiver[size-2]=0;
 else if(size) {g_free(receiver);g_free(prefix);return NULL;}
 if(begin && (text[begin-1]==']' || text[begin-1]==')')) {g_free(receiver);g_free(prefix);return NULL;}
 gchar *directory=g_dir_make_tmp("geany-slang-XXXXXX",NULL);if(!directory) {g_free(receiver);g_free(prefix);return NULL;}
 GPtrArray *argv=g_ptr_array_new_with_free_func(g_free),*temporary=g_ptr_array_new_with_free_func(g_free),*out=NULL;
 g_ptr_array_add(argv,g_strdup(service));g_ptr_array_add(argv,g_strdup("--editor-complete"));g_ptr_array_add(argv,g_strdup(path));g_ptr_array_add(argv,g_strdup_printf("%u",line));g_ptr_array_add(argv,receiver);g_ptr_array_add(argv,prefix);
 GHashTable *selected=g_hash_table_new(g_str_hash,g_str_equal);gsize bytes=0,total_args=0;gboolean valid=TRUE;
 for(guint i=0;i<paths->len;i++) {
  const gchar *file=g_ptr_array_index(paths,i);if(!insight_slang_source(file) || g_hash_table_contains(selected,file)) continue;
  total_args+=strlen(file);if(g_hash_table_size(selected)>=256 || total_args>12000) {valid=FALSE;break;}g_hash_table_add(selected,(gpointer)file);
  const gchar *overlay=g_hash_table_lookup(buffers,file);
  if(overlay) {gsize length=strlen(overlay);bytes+=length;if(length>2u*1024u*1024u || bytes>32u*1024u*1024u) {valid=FALSE;break;}
   gchar *target=g_strdup_printf("%s/%u.sv",directory,temporary->len);g_ptr_array_add(temporary,target);
   if(!g_file_set_contents(target,overlay,length,NULL)) {valid=FALSE;break;}
   g_ptr_array_add(argv,g_strdup("--overlay"));g_ptr_array_add(argv,g_strdup(file));g_ptr_array_add(argv,g_strdup(target));
  }
 }
 g_ptr_array_add(argv,g_strdup("--"));
 /* Preserve captured source order: do not iterate the membership hash. */
 for(guint i=0;i<paths->len;i++) {const gchar *file=g_ptr_array_index(paths,i);if(g_hash_table_remove(selected,file)) g_ptr_array_add(argv,g_strdup(file));}
 g_hash_table_unref(selected);g_ptr_array_add(argv,NULL);
 gsize command_bytes=0;for(guint i=0;i+1<argv->len;i++) command_bytes+=2*strlen(g_ptr_array_index(argv,i))+4;
 if(command_bytes>24000) valid=FALSE;
 if(valid) {
  GSubprocess *process=g_subprocess_newv((const gchar *const *)argv->pdata,G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_SILENCE,NULL);
  if(process) {
   gulong handler=cancel ? g_cancellable_connect(cancel,G_CALLBACK(terminate),process,NULL):0;
   gchar *reply=NULL;
   gboolean ok=g_subprocess_communicate_utf8(process,NULL,cancel,&reply,NULL,NULL);
   if(cancel && handler) g_cancellable_disconnect(cancel,handler);
   if(!ok) {g_subprocess_force_exit(process);g_subprocess_wait(process,NULL,NULL);}
   if(ok && g_subprocess_get_successful(process) && reply && strlen(reply)<=4u*1024u*1024u && g_str_has_prefix(reply,"insight-slang-completion-v1\n")) {
    out=g_ptr_array_new_with_free_func(candidate_free);gchar **rows=g_strsplit(reply,"\n",-1);
    for(guint i=1;rows[i] && *rows[i] && out->len<4096;i++) {
     gchar **fields=g_strsplit(rows[i],"\t",3);gchar *name=fields[0] && fields[1] && !fields[2] ? unhex(fields[0]):NULL,*kind=name ? unhex(fields[1]):NULL;
     if(!name || !*name || !kind) {g_free(name);g_free(kind);g_strfreev(fields);g_ptr_array_unref(out);out=NULL;break;}
     InsightClangCompletion *c=g_new0(InsightClangCompletion,1);c->name=name;c->signature=g_strdup(name);c->type=kind;c->comment=g_strdup("");c->parameters=g_ptr_array_new_with_free_func(g_free);c->callable=FALSE;g_ptr_array_add(out,c);g_strfreev(fields);
    }g_strfreev(rows);
   }if(out && !out->len) {g_ptr_array_unref(out);out=NULL;}g_free(reply);g_object_unref(process);
  }
 }
 for(guint i=0;i<temporary->len;i++) g_unlink(g_ptr_array_index(temporary,i));g_rmdir(directory);g_free(directory);g_ptr_array_unref(temporary);g_ptr_array_unref(argv);return out;
}
