/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "clang_backend.h"
#include "glib_compat.h"
#ifdef HAVE_CLANG_CPP
#include "clang_calls.h"
#endif
#include <string.h>
gboolean insight_clang_cpp_available(void) {
#ifdef HAVE_CLANG_CPP
 return TRUE;
#else
 return FALSE;
#endif
}
gboolean insight_clang_source(const gchar *path) {
 const gchar *dot=path ? strrchr(path,'.'):NULL;if(!dot) return FALSE;const gchar *extensions[]={".c",".h",".cc",".cpp",".cxx",".hpp",".hh",".hxx"};for(guint i=0;i<G_N_ELEMENTS(extensions);i++) if(!g_ascii_strcasecmp(dot,extensions[i])) return TRUE;return FALSE;
}
static gchar *dependency_stamp(const gchar *path) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,"standard::size,time::modified,time::modified-usec",G_FILE_QUERY_INFO_NONE,NULL,NULL);g_object_unref(file);if(!info) return g_strdup("missing");
 gchar *stamp=g_strdup_printf("%"G_GUINT64_FORMAT":%"G_GUINT64_FORMAT":%u",(guint64)g_file_info_get_size(info),g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED),g_file_info_get_attribute_uint32(info,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC));g_object_unref(info);return stamp;
}
gboolean insight_clang_dependencies_fresh(const InsightIndex *relations,const gchar *path) {
 gsize count=insight_index_query(relations,"",FALSE,NULL,"@clanginclude",path,0,NULL,0);if(count>4096) return FALSE;const InsightSymbol **rows=g_new(const InsightSymbol *,count);insight_index_query(relations,"",FALSE,NULL,"@clanginclude",path,0,rows,count);gboolean fresh=TRUE;
 for(gsize i=0;i<count && fresh;i++) {gchar *now=dependency_stamp(rows[i]->name);fresh=!g_strcmp0(now,rows[i]->signature);g_free(now);}g_free(rows);return fresh;
}
gchar *insight_clang_symbol_at(InsightIndex *relations,const gchar *path,guint offset,guint line,const gchar *name) {
 if(!relations || !path || !name) return NULL;gsize n=insight_index_query(relations,name,TRUE,NULL,"@clangref",path,0,NULL,0);const InsightSymbol **rows=g_new(const InsightSymbol *,n);insight_index_query(relations,name,TRUE,NULL,"@clangref",path,0,rows,n);gchar *usr=NULL;guint64 smallest=G_MAXUINT64;
 for(gsize i=0;i<n;i++) {gchar **parts=g_strsplit(rows[i]->signature,":",3);if(g_strv_length(parts)==3) {guint64 a=g_ascii_strtoull(parts[0],NULL,10),z=g_ascii_strtoull(parts[1],NULL,10);if(a<=offset && offset<=z && z-a<smallest) {g_free(usr);usr=g_strdup(rows[i]->scope);smallest=z-a;}}g_strfreev(parts);}g_free(rows);if(usr) return usr;
 n=insight_index_query(relations,"",FALSE,NULL,"@clangdef",path,0,NULL,0);rows=g_new(const InsightSymbol *,n);insight_index_query(relations,"",FALSE,NULL,"@clangdef",path,0,rows,n);gboolean ambiguous=FALSE;
 for(gsize i=0;i<n;i++) if(rows[i]->line==line && !g_strcmp0(rows[i]->signature,name)) {if(usr && strcmp(usr,rows[i]->name)) ambiguous=TRUE;g_free(usr);usr=g_strdup(rows[i]->name);}g_free(rows);if(ambiguous) g_clear_pointer(&usr,g_free);return usr;
}
void insight_clang_result_free(InsightClangResult *r) {if(!r) return;insight_index_free(r->symbols);insight_index_free(r->relations);g_array_unref(r->tokens);if(r->attributes) g_hash_table_unref(r->attributes);g_free(r->digest);g_free(r->diagnostics);g_free(r->command_source);g_free(r->parse_language);g_free(r);}
static gboolean import_index(InsightIndex *from,InsightIndex *to,const gchar *path) {
 gsize n=insight_index_count(from);InsightSymbol *rows=g_new(InsightSymbol,n);for(gsize i=0;i<n;i++) rows[i]=*insight_index_at(from,i);gboolean ok=insight_index_update(to,path,rows,n);g_free(rows);return ok;
}
gboolean insight_clang_import(InsightClangResult *r,const gchar *path,InsightIndex *symbols,InsightIndex *relations) {return insight_clang_result_usable(r) && import_index(r->symbols,symbols,path) && import_index(r->relations,relations,path);}
#ifdef HAVE_CLANG_CPP
gboolean insight_clang_available(void) {return TRUE;}
InsightClangResult *insight_clang_parse(const gchar *path,const gchar *text,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {return insight_clang_cpp_parse(path,text,root,resources,overlays,cancel,error);}
GPtrArray *insight_clang_complete(const gchar *path,const gchar *text,guint offset,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {return insight_clang_cpp_complete(path,text,offset,root,resources,overlays,cancel,error);}
#else
GPtrArray *insight_clang_complete(const gchar *path,const gchar *text,guint offset,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {(void)path;(void)text;(void)offset;(void)root;(void)resources;(void)overlays;(void)cancel;g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Clang completion unavailable");return NULL;}
gboolean insight_clang_available(void) {return FALSE;}
InsightClangResult *insight_clang_parse(const gchar *path,const gchar *text,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {(void)path;(void)text;(void)root;(void)resources;(void)overlays;(void)cancel;g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"This build has no bundled Clang backend");return NULL;}
#endif
