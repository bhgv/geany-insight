/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "project_files.h"
#include <string.h>
#define GROUP "insight-project-files"
static gchar *canonical(const char *path) {
 if(!path || !g_path_is_absolute(path) || strlen(path)>16384 || !g_utf8_validate(path,-1,NULL) || strchr(path,'\n') || strchr(path,'\r')) return NULL;
 return g_canonicalize_filename(path,NULL);
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
 InsightProjectFiles *s=g_new0(InsightProjectFiles,1); s->automatic=TRUE; s->masks=g_strdup("");
 s->files=g_ptr_array_new_with_free_func(g_free); s->trees=g_ptr_array_new_with_free_func(g_free); s->excluded_files=g_ptr_array_new_with_free_func(g_free); s->excluded_trees=g_ptr_array_new_with_free_func(g_free); return s;
}
void insight_project_files_free(InsightProjectFiles *s) { if(!s) return; g_free(s->masks); g_ptr_array_free(s->files,TRUE); g_ptr_array_free(s->trees,TRUE); g_ptr_array_free(s->excluded_files,TRUE); g_ptr_array_free(s->excluded_trees,TRUE); g_free(s); }
InsightProjectFiles *insight_project_files_clone(const InsightProjectFiles *s) {
 InsightProjectFiles *copy=insight_project_files_new(); copy->automatic=s->automatic; g_free(copy->masks); copy->masks=g_strdup(s->masks);
 GPtrArray *from[]={s->files,s->trees,s->excluded_files,s->excluded_trees},*to[]={copy->files,copy->trees,copy->excluded_files,copy->excluded_trees};
 for(guint i=0;i<4;i++) for(guint j=0;j<from[i]->len;j++) g_ptr_array_add(to[i],g_strdup(g_ptr_array_index(from[i],j)));
 return copy;
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
 for(guint i=0;patterns[i] && !result;i++) { gchar *pattern=g_strstrip(patterns[i]); if(!*pattern) continue; GPatternSpec *spec=g_pattern_spec_new(pattern); result=g_pattern_spec_match_string(spec,base) || (relative && g_pattern_spec_match_string(spec,relative)); g_pattern_spec_free(spec); }
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
 for(guint i=0;i<4;i++) { GPtrArray *encoded=g_ptr_array_new_with_free_func(g_free);
  for(guint j=0;j<arrays[i]->len;j++) { const char *path=g_ptr_array_index(arrays[i],j); GFile *file=g_file_new_for_path(path); gchar *relative=g_file_get_relative_path(base,file); if(relative) for(gchar *c=relative;*c;c++) if(G_IS_DIR_SEPARATOR(*c)) *c='/'; g_ptr_array_add(encoded,relative ? relative : (same(path,r) ? g_strdup(".") : g_strdup(path))); g_object_unref(file); }
  g_key_file_set_string_list(config,GROUP,names[i],(const gchar *const *)encoded->pdata,encoded->len); g_ptr_array_free(encoded,TRUE);
 }
 g_object_unref(base); g_free(r); return TRUE;
}
InsightProjectFiles *insight_project_files_read(GKeyFile *config,const char *root,GError **error) {
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
   gchar *joined=g_canonicalize_filename(paths[j],r),*p=canonical(joined); g_free(joined); if(!p) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid stored project path"); else g_ptr_array_add(arrays[i],p);
  }
  g_strfreev(paths);
 }
 g_free(r); if(!e) valid(s,&e); if(e) { g_propagate_error(error,e); insight_project_files_free(s); return NULL; } return s;
}
