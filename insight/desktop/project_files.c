/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "project_files.h"
#include "glib_compat.h"
#include <string.h>
#ifdef G_OS_WIN32
#include <windows.h>
#endif
#define GROUP "insight-project-files"
static gchar *canonical(const char *path) {
 if(!path || !g_path_is_absolute(path) || strlen(path)>16384 || !g_utf8_validate(path,-1,NULL) || strchr(path,'\n') || strchr(path,'\r')) return NULL;
 return insight_canonicalize_filename(path,NULL);
}
static gchar *key(const char *path) {
#ifdef G_OS_WIN32
 return g_utf8_casefold(path,-1);
#else
 return g_strdup(path);
#endif
}
static gboolean same(const char *a,const char *b) { gchar *x=key(a),*y=key(b); gboolean result=!strcmp(x,y); g_free(x); g_free(y); return result; }
static gboolean below(const char *path,const char *tree) {
 gchar *p=key(path),*t=key(tree); gsize n=strlen(t);
 gboolean result=!strcmp(p,t) || (g_str_has_prefix(p,t) && (G_IS_DIR_SEPARATOR(t[n-1]) || G_IS_DIR_SEPARATOR(p[n])));
 g_free(p); g_free(t); return result;
}
InsightProjectFiles *insight_project_files_new(void) {
 InsightProjectFiles *s=g_new0(InsightProjectFiles,1); s->automatic=TRUE; s->masks=g_strdup("");s->master_file=g_strdup("");s->master_expression=g_strdup("");
 s->files=g_ptr_array_new_with_free_func(g_free); s->trees=g_ptr_array_new_with_free_func(g_free); s->excluded_files=g_ptr_array_new_with_free_func(g_free); s->excluded_trees=g_ptr_array_new_with_free_func(g_free); return s;
}
void insight_project_files_free(InsightProjectFiles *s) { if(!s) return; g_free(s->masks);g_free(s->master_file);g_free(s->master_expression); g_ptr_array_free(s->files,TRUE); g_ptr_array_free(s->trees,TRUE); g_ptr_array_free(s->excluded_files,TRUE); g_ptr_array_free(s->excluded_trees,TRUE); g_free(s); }
InsightProjectFiles *insight_project_files_clone(const InsightProjectFiles *s) {
 InsightProjectFiles *copy=insight_project_files_new(); copy->automatic=s->automatic; g_free(copy->masks); copy->masks=g_strdup(s->masks);g_free(copy->master_file);copy->master_file=g_strdup(s->master_file);g_free(copy->master_expression);copy->master_expression=g_strdup(s->master_expression);
 GPtrArray *from[]={s->files,s->trees,s->excluded_files,s->excluded_trees},*to[]={copy->files,copy->trees,copy->excluded_files,copy->excluded_trees};
 for(guint i=0;i<4;i++) for(guint j=0;j<from[i]->len;j++) g_ptr_array_add(to[i],g_strdup(g_ptr_array_index(from[i],j)));
 return copy;
}
gchar *insight_project_files_escape_master(const gchar *path) {
 gchar **parts=g_strsplit(path,"%",-1);gchar *text=g_strjoinv("%%",parts);g_strfreev(parts);return text;
}
/* Resolve against one environment snapshot, never recursively. Case collisions
 * with different values are ambiguous on Unix rather than order-dependent. */
static gchar *master_environment(gchar **environment,const gchar *name) {
 gchar *result=NULL;
 for(guint i=0;environment[i];i++) {
  const gchar *equal=strchr(environment[i],'=');
  if(!equal || (gsize)(equal-environment[i])!=strlen(name) || g_ascii_strncasecmp(environment[i],name,equal-environment[i])) continue;
  if(result && strcmp(result,equal+1)) { g_free(result);return NULL; }
  if(!result) result=g_strdup(equal+1);
 }
 return result;
}
/* Resolve the executable itself: neither cwd, PATH nor PROGRAM_DIR from the
 * environment identifies the running editor. Do not guess on other systems. */
static gchar *master_program_directory(void) {
 gchar *executable=NULL;
#ifdef G_OS_WIN32
 gunichar2 *buffer=g_new(gunichar2,32768);
 DWORD length=GetModuleFileNameW(NULL,(WCHAR *)buffer,32768);
 if(length && length<32768) executable=g_utf16_to_utf8(buffer,(glong)length,NULL,NULL,NULL);
 g_free(buffer);
#elif defined(__linux__)
 executable=g_file_read_link("/proc/self/exe",NULL);
#endif
 if(!executable || !g_path_is_absolute(executable)) { g_free(executable);return NULL; }
 gchar *directory=g_path_get_dirname(executable);g_free(executable);return directory;
}
static gchar *master_variable(const gchar *name,const gchar *base,const gchar *project,gchar **environment) {
 if(!g_ascii_strcasecmp(name,"PROJECT_SOURCE_DIR")) return g_strdup(base);
 if(!g_ascii_strcasecmp(name,"PROJECT_DATA_DIR") || !g_ascii_strcasecmp(name,"PROJECT_NAME")) {
  if(!project || strlen(project)>16384 || !g_path_is_absolute(project) || !g_utf8_validate(project,-1,NULL) || strpbrk(project,"\r\n")) return NULL;
  if(!g_ascii_strcasecmp(name,"PROJECT_DATA_DIR")) return g_path_get_dirname(project);
  gchar *value=g_path_get_basename(project),*dot=strrchr(value,'.');if(dot && dot!=value) *dot=0;return value;
 }
 if(!g_ascii_strcasecmp(name,"PROGRAM_DIR")) return master_program_directory();
 if(!g_ascii_strcasecmp(name,"TEMP_DIR")) return g_strdup(g_get_tmp_dir());
 if(!g_ascii_strcasecmp(name,"DESKTOP_DIR")) return g_strdup(g_get_user_special_dir(G_USER_DIRECTORY_DESKTOP));
 if(!g_ascii_strcasecmp(name,"MYDOCUMENTS_DIR")) return g_strdup(g_get_user_special_dir(G_USER_DIRECTORY_DOCUMENTS));
#ifdef G_OS_WIN32
 if(!g_ascii_strcasecmp(name,"APPDATA_DIR")) return master_environment(environment,"APPDATA");
 if(!g_ascii_strcasecmp(name,"LOCAL_APPDATA_DIR")) return master_environment(environment,"LOCALAPPDATA");
 if(!g_ascii_strcasecmp(name,"PROGRAMFILES_DIR")) return master_environment(environment,"ProgramFiles");
 if(!g_ascii_strcasecmp(name,"PROGRAMFILESX86_DIR")) return master_environment(environment,"ProgramFiles(x86)");
 if(!g_ascii_strcasecmp(name,"WINDOWS_DIR")) return master_environment(environment,"SystemRoot");
#else
 if(!g_ascii_strcasecmp(name,"APPDATA_DIR")) return g_strdup(g_get_user_config_dir());
 if(!g_ascii_strcasecmp(name,"LOCAL_APPDATA_DIR")) return g_strdup(g_get_user_data_dir());
#endif
 /* Unimplemented predefined names must not accidentally bind an environment
  * value with different semantics. */
 static const gchar *reserved[]={"PROGRAMFILES_DIR","PROGRAMFILESX86_DIR","WINDOWS_DIR","SHARED_DOCUMENTS_DIR","SOURCEINSIGHT_USER_DIR"};
 for(guint i=0;i<G_N_ELEMENTS(reserved);i++) if(!g_ascii_strcasecmp(name,reserved[i])) return NULL;
 return master_environment(environment,name);
}
gchar *insight_project_path_variable(const gchar *name,const gchar *root,const gchar *project,GError **error) {
 gchar *base=canonical(root);if(!base || !name || strlen(name)>64) {g_free(base);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid path variable context");return NULL;}
 gchar **environment=g_get_environ();gchar *result=master_variable(name,base,project,environment);g_strfreev(environment);g_free(base);
 if(result && (!*result || strlen(result)>16384 || !g_utf8_validate(result,-1,NULL) || strpbrk(result,"\r\n"))) g_clear_pointer(&result,g_free);
 if(!result) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Path variable is unavailable");}
 return result;
}
gboolean insight_project_files_set_master(InsightProjectFiles *s,const gchar *input,const gchar *root,const gchar *project,GError **error) {
 gchar *base=canonical(root);
 if(!s || !base || !input || strlen(input)>16384 || !g_utf8_validate(input,-1,NULL) || strpbrk(input,"\r\n")) { g_free(base);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid master file list expression");return FALSE; }
 gchar **environment=g_get_environ();
 GString *expanded=g_string_new(NULL);gboolean valid=TRUE,variable=FALSE;guint tokens=0;
 for(const gchar *p=input;*p && valid;) {
  if(*p!='%') { g_string_append_c(expanded,*p++);continue; }
  if(p[1]=='%') { g_string_append_c(expanded,'%');p+=2;variable=TRUE;continue; }
  const gchar *end=strchr(p+1,'%');if(!end || end-p>65 || ++tokens>32) { valid=FALSE;break; }
  gchar *name=g_strndup(p+1,end-p-1),*value=master_variable(name,base,project,environment);
  if(!value || !*value || strlen(value)>16384 || !g_utf8_validate(value,-1,NULL) || strpbrk(value,"\r\n") || expanded->len+strlen(value)>16384) valid=FALSE;
  else { g_string_append(expanded,value);variable=TRUE; }
  g_free(value);g_free(name);p=end+1;
 }
 gchar *path=valid && expanded->len ? insight_canonicalize_filename(expanded->str,base) : g_strdup("");
 if(!valid || strlen(path)>16384) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Unknown or unavailable master file list path variable");valid=FALSE; }
 if(valid) { gchar *expression=g_strdup(variable ? input : "");g_free(s->master_file);s->master_file=path;path=NULL;g_free(s->master_expression);s->master_expression=expression; }
 g_free(path);g_string_free(expanded,TRUE);g_strfreev(environment);g_free(base);return valid;
}
static void remove_path(GPtrArray *paths,const char *path,gboolean tree) {
 for(guint i=paths->len;i>0;i--) if(tree ? below(g_ptr_array_index(paths,i-1),path) : same(g_ptr_array_index(paths,i-1),path)) g_ptr_array_remove_index(paths,i-1);
}
gboolean insight_project_files_set(InsightProjectFiles *s,const char *path,gboolean tree,gboolean include,GError **error) {
 gchar *p=canonical(path); if(!p) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Project paths must be absolute UTF-8 paths without line breaks"); return FALSE; }
 GPtrArray *target=tree ? (include ? s->trees : s->excluded_trees) : (include ? s->files : s->excluded_files);
 gboolean exists=FALSE; for(guint i=0;i<target->len;i++) if(same(g_ptr_array_index(target,i),p)) { exists=TRUE; break; }
 if(!exists && target->len>=10000) { g_free(p); g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Project membership rule limit reached (10000 per list)"); return FALSE; }
 if(tree) { remove_path(s->files,p,TRUE); remove_path(s->trees,p,TRUE); remove_path(s->excluded_files,p,TRUE); remove_path(s->excluded_trees,p,TRUE); }
 else { remove_path(s->files,p,FALSE); remove_path(s->excluded_files,p,FALSE); }
 g_ptr_array_add(target,p); return TRUE;
}
static gboolean masks_match(const InsightProjectFiles *s,const char *root,const char *path) {
 if(!s->masks || !*s->masks) return FALSE;
 GFile *r=g_file_new_for_path(root),*p=g_file_new_for_path(path); gchar *relative=g_file_get_relative_path(r,p),*base=g_path_get_basename(path); g_object_unref(r); g_object_unref(p);
 if(relative) for(gchar *c=relative;*c;c++) if(G_IS_DIR_SEPARATOR(*c)) *c='/';
 gchar **patterns=g_strsplit(s->masks,";",-1); gboolean result=FALSE;
 for(guint i=0;patterns[i] && !result;i++) { gchar *pattern=g_strstrip(patterns[i]); if(!*pattern) continue; GPatternSpec *spec=g_pattern_spec_new(pattern); result=insight_pattern_match_string(spec,base) || (relative && insight_pattern_match_string(spec,relative)); g_pattern_spec_free(spec); }
 g_strfreev(patterns); g_free(relative); g_free(base); return result;
}
gboolean insight_project_files_contains(const InsightProjectFiles *s,const char *root,const char *path) {
 gchar *r=canonical(root),*p=canonical(path); if(!r || !p) { g_free(r); g_free(p); return FALSE; }
 gboolean included=s->automatic && below(p,r); gsize specificity=0;
 for(guint i=0;i<s->trees->len;i++) { const char *tree=g_ptr_array_index(s->trees,i); if(below(p,tree) && strlen(tree)>=specificity) { included=TRUE; specificity=strlen(tree); } }
 for(guint i=0;i<s->excluded_trees->len;i++) { const char *tree=g_ptr_array_index(s->excluded_trees,i); if(below(p,tree) && strlen(tree)>=specificity) { included=FALSE; specificity=strlen(tree); } }
 for(guint i=0;i<s->files->len;i++) if(same(p,g_ptr_array_index(s->files,i))) included=TRUE;
 for(guint i=0;i<s->excluded_files->len;i++) if(same(p,g_ptr_array_index(s->excluded_files,i))) included=FALSE;
 if(included && masks_match(s,r,p)) included=FALSE;
 g_free(r); g_free(p); return included;
}
static gboolean valid(const InsightProjectFiles *s,GError **error) {
 if(!s || !s->masks || strlen(s->masks)>4096 || !g_utf8_validate(s->masks,-1,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid project exclusion masks"); return FALSE; }
 if(!s->master_expression || strlen(s->master_expression)>16384 || !g_utf8_validate(s->master_expression,-1,NULL) || strpbrk(s->master_expression,"\r\n")) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid master file list expression");return FALSE; }
 if(!s->master_file || strlen(s->master_file)>16384 || !g_utf8_validate(s->master_file,-1,NULL) || strpbrk(s->master_file,"\r\n") || (*s->master_file && !g_path_is_absolute(s->master_file))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid master file list path");return FALSE; }
 GPtrArray *arrays[]={s->files,s->trees,s->excluded_files,s->excluded_trees};
 for(guint i=0;i<4;i++) { if(!arrays[i] || arrays[i]->len>10000) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Too many project membership rules"); return FALSE; }
  for(guint j=0;j<arrays[i]->len;j++) { gchar *p=canonical(g_ptr_array_index(arrays[i],j)); if(!p) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid project membership path"); return FALSE; } g_free(p); }
 }
 return TRUE;
}
gboolean insight_project_files_write(const InsightProjectFiles *s,GKeyFile *config,const char *root,GError **error) {
 gchar *r=canonical(root); if(!r) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid project root"); return FALSE; }
 if(!valid(s,error)) { g_free(r); return FALSE; }
 g_key_file_set_integer(config,GROUP,"version",1); g_key_file_set_boolean(config,GROUP,"automatic",s->automatic); g_key_file_set_string(config,GROUP,"exclude-masks",s->masks);
 GPtrArray *arrays[]={s->files,s->trees,s->excluded_files,s->excluded_trees}; const char *names[]={"files","trees","excluded-files","excluded-trees"}; GFile *base=g_file_new_for_path(r);
 if(*s->master_file && *s->master_expression) g_key_file_set_string(config,GROUP,"master-file-expression",s->master_expression);else g_key_file_remove_key(config,GROUP,"master-file-expression",NULL);
 if(*s->master_file) { GFile *master=g_file_new_for_path(s->master_file);gchar *relative=g_file_get_relative_path(base,master);if(relative) for(gchar *p=relative;*p;p++) if(G_IS_DIR_SEPARATOR(*p)) *p='/';g_key_file_set_string(config,GROUP,"master-file",relative ? relative : s->master_file);g_free(relative);g_object_unref(master); }else g_key_file_remove_key(config,GROUP,"master-file",NULL);
 for(guint i=0;i<4;i++) { GPtrArray *encoded=g_ptr_array_new_with_free_func(g_free);
  for(guint j=0;j<arrays[i]->len;j++) { const char *path=g_ptr_array_index(arrays[i],j); GFile *file=g_file_new_for_path(path); gchar *relative=g_file_get_relative_path(base,file); if(relative) for(gchar *c=relative;*c;c++) if(G_IS_DIR_SEPARATOR(*c)) *c='/'; g_ptr_array_add(encoded,relative ? relative : (same(path,r) ? g_strdup(".") : g_strdup(path))); g_object_unref(file); }
  g_key_file_set_string_list(config,GROUP,names[i],(const gchar *const *)encoded->pdata,encoded->len); g_ptr_array_free(encoded,TRUE);
 }
 g_object_unref(base); g_free(r); return TRUE;
}
InsightProjectFiles *insight_project_files_read_project(GKeyFile *config,const char *root,const char *project,GError **error) {
 InsightProjectFiles *s=insight_project_files_new(); if(!g_key_file_has_group(config,GROUP)) return s;
 GError *e=NULL; gchar *r=canonical(root); if(!r) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid project root"); insight_project_files_free(s); return NULL; }
 gint version=g_key_file_get_integer(config,GROUP,"version",&e);
 if(!e && version!=1) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Unknown project membership version");
 if(!e) s->automatic=g_key_file_get_boolean(config,GROUP,"automatic",&e);
 if(!e) { g_free(s->masks); s->masks=g_key_file_get_string(config,GROUP,"exclude-masks",&e); }
 GPtrArray *arrays[]={s->files,s->trees,s->excluded_files,s->excluded_trees}; const char *names[]={"files","trees","excluded-files","excluded-trees"};
 for(guint i=0;i<4 && !e;i++) { gsize count=0; gchar **paths=g_key_file_get_string_list(config,GROUP,names[i],&count,&e);
  if(!e && count>10000) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Too many project membership rules");
  for(gsize j=0;j<count && !e;j++) { if(!paths[j][0] || strlen(paths[j])>16384 || strchr(paths[j],'\n') || strchr(paths[j],'\r')) { g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid stored project path"); break; }
   gchar *joined=insight_canonicalize_filename(paths[j],r),*p=canonical(joined); g_free(joined); if(!p) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid stored project path"); else g_ptr_array_add(arrays[i],p);
  }
  g_strfreev(paths);
 }
 if(!e && g_key_file_has_key(config,GROUP,"master-file-expression",NULL)) { gchar *value=g_key_file_get_string(config,GROUP,"master-file-expression",&e);if(value) insight_project_files_set_master(s,value,r,project,&e);g_free(value); }
 else if(!e && g_key_file_has_key(config,GROUP,"master-file",NULL)) { gchar *value=g_key_file_get_string(config,GROUP,"master-file",&e);if(value) { if(strlen(value)>16384 || strpbrk(value,"\r\n")) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid stored master file list path");else if(*value) { g_free(s->master_file);s->master_file=insight_canonicalize_filename(value,r); }g_free(value); } }
 g_free(r); if(!e) valid(s,&e); if(e) { g_propagate_error(error,e); insight_project_files_free(s); return NULL; } return s;
}

InsightProjectFiles *insight_project_files_read(GKeyFile *config,const char *root,GError **error) { return insight_project_files_read_project(config,root,NULL,error); }

InsightProjectFiles *insight_project_files_freeze(const InsightProjectFiles *s,const gchar *root,const GPtrArray *paths,GError **error) {
 gchar *base=canonical(root);
 if(!s || !base || !paths || paths->len>20000 || !valid(s,error)) {g_free(base);if(error && !*error) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid project membership snapshot");return NULL;}
 if(*s->master_file) {g_free(base);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Detach the master file list before freezing membership");return NULL;}
 InsightProjectFiles *next=insight_project_files_clone(s);next->automatic=FALSE;g_ptr_array_set_size(next->files,0);g_ptr_array_set_size(next->trees,0);
 GHashTable *seen=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);const GPtrArray *sources[]={s->files,paths};gboolean ok=TRUE;
 for(guint kind=0;kind<2 && ok;kind++) for(guint n=0;n<sources[kind]->len && ok;n++) {
  gchar *path=canonical(g_ptr_array_index((GPtrArray *)sources[kind],n));if(!path) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid project snapshot path");ok=FALSE;break;}
  gchar *identity=key(path);if(g_hash_table_contains(seen,identity)) {g_free(identity);g_free(path);continue;}g_hash_table_add(seen,identity);
  if(insight_project_files_contains(s,base,path)) {ok=insight_project_files_set(next,path,FALSE,TRUE,error);}
  g_free(path);
 }
 g_hash_table_unref(seen);g_free(base);if(!ok) {insight_project_files_free(next);return NULL;}return next;
}
