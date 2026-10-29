/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "project_report.h"
#include "text_transfer.h"
#include <string.h>
static gboolean fail(GError **error,GIOErrorEnum code,const gchar *message)
{ g_set_error_literal(error,G_IO_ERROR,code,message);return FALSE; }
static gboolean field(const gchar *value,gsize maximum)
{ return value && strlen(value)<=maximum && g_utf8_validate(value,-1,NULL); }
static gint path_order(gconstpointer a,gconstpointer b)
{ return strcmp(*(gchar * const *)a,*(gchar * const *)b); }
InsightReportSnapshot *insight_report_snapshot_new(const gchar *project,const gchar *root,const gchar *project_file,GPtrArray *paths,InsightIndex *index,guint skipped,gboolean incomplete,GError **error)
{
    if(!field(project,16384) || !field(root,16384) || !g_path_is_absolute(root) || !field(project_file,16384) || !g_path_is_absolute(project_file) || !paths || paths->len>INSIGHT_REPORT_FILE_LIMIT || !index || insight_index_count(index)>250000) {
        fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Project report requires a bounded, completed project snapshot.");return NULL;
    }
    InsightReportSnapshot *snapshot=g_new0(InsightReportSnapshot,1);snapshot->refs=1;snapshot->project=g_strdup(project);snapshot->root=g_strdup(root);snapshot->project_file=g_strdup(project_file);
    snapshot->index=insight_index_ref(index);snapshot->skipped=skipped;snapshot->incomplete=incomplete;snapshot->paths=g_ptr_array_new_with_free_func(g_free);
    GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
    for(guint i=0;i<paths->len;i++) {
        const gchar *path=g_ptr_array_index(paths,i);
        if(!field(path,16384) || !g_path_is_absolute(path)) { g_hash_table_unref(seen);insight_report_snapshot_unref(snapshot);fail(error,G_IO_ERROR_INVALID_DATA,"Project report paths must be absolute UTF-8 filenames.");return NULL; }
        if(!g_hash_table_contains(seen,path)) { gchar *copy=g_strdup(path);g_ptr_array_add(snapshot->paths,copy);g_hash_table_add(seen,copy); }
    }
    g_hash_table_unref(seen);g_ptr_array_sort(snapshot->paths,path_order);return snapshot;
}
InsightReportSnapshot *insight_report_snapshot_ref(InsightReportSnapshot *snapshot)
{ if(snapshot) g_atomic_int_inc(&snapshot->refs);return snapshot; }
void insight_report_snapshot_unref(InsightReportSnapshot *snapshot)
{
    if(!snapshot || !g_atomic_int_dec_and_test(&snapshot->refs)) return;
    g_free(snapshot->project);g_free(snapshot->root);g_free(snapshot->project_file);g_ptr_array_unref(snapshot->paths);insight_index_free(snapshot->index);insight_index_free(snapshot->calls);g_free(snapshot);
}
static gchar *escaped(const gchar *text)
{
    GString *out=g_string_new(NULL);
    for(const gchar *p=text;*p;p++) switch(*p) {
        case '\\':g_string_append(out,"\\\\");break;case '\n':g_string_append(out,"\\n");break;
        case '\r':g_string_append(out,"\\r");break;case '\t':g_string_append(out,"\\t");break;
        default:g_string_append_c(out,*p);break;
    }
    return g_string_free(out,FALSE);
}
static gchar *display_path(const gchar *root,const gchar *path,gboolean relative)
{
    if(!relative) return g_strdup(path);
    GFile *base=g_file_new_for_path(root),*file=g_file_new_for_path(path);gchar *result=g_file_get_relative_path(base,file);
    g_object_unref(file);g_object_unref(base);
    if(!result) result=g_strdup(path);
    for(gchar *p=result;*p;p++) if(G_IS_DIR_SEPARATOR(*p)) *p='/';return result;
}
static gint symbol_order(gconstpointer a,gconstpointer b,gpointer data)
{
    const InsightSymbol *x=*(const InsightSymbol * const *)a,*y=*(const InsightSymbol * const *)b;gboolean names=GPOINTER_TO_INT(data);gint order=0;
    if(names) order=g_strcmp0(x->name,y->name);
    if(!order && x->line!=y->line) order=x->line<y->line ? -1:1;
    if(!order) order=g_strcmp0(x->name,y->name);
    if(!order) order=g_strcmp0(x->kind,y->kind);
    if(!order) order=g_strcmp0(x->scope,y->scope);
    if(!order) order=g_strcmp0(x->signature,y->signature);return order;
}
static gboolean bounded(GString *out,GError **error)
{ return out->len<=INSIGHT_REPORT_LIMIT || fail(error,G_IO_ERROR_NO_SPACE,"Project report exceeds 4 MiB. Narrow the symbol types or export a file list."); }
gchar *insight_project_report_build(const InsightReportSnapshot *snapshot,const InsightReportOptions *options,GCancellable *cancel,gsize *length,GError **error)
{
    if(length) *length=0;
    if(!snapshot || !options) { fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Invalid project report request.");return NULL; }
    if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) return NULL;
    GString *out=g_string_new(NULL);GHashTable *groups=g_hash_table_new_full(g_str_hash,g_str_equal,NULL,(GDestroyNotify)g_ptr_array_unref);guint count=0;gboolean ok=TRUE;
    for(guint i=0;i<snapshot->paths->len;i++) g_hash_table_insert(groups,g_ptr_array_index(snapshot->paths,i),g_ptr_array_new());
    if(!options->file_list && options->include_symbols) for(gsize i=0;i<insight_index_count(snapshot->index);i++) {
        if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) { ok=FALSE;goto done; }
        const InsightSymbol *symbol=insight_index_at(snapshot->index,i);GPtrArray *group=g_hash_table_lookup(groups,symbol->path);
        if(!group || (options->kinds && !g_hash_table_contains(options->kinds,symbol->kind))) continue;
        if(++count>INSIGHT_REPORT_SYMBOL_LIMIT) { ok=fail(error,G_IO_ERROR_NO_SPACE,"Project report exceeds 100000 symbols. Narrow the symbol types.");goto done; }
        if(!field(symbol->name,65536) || !field(symbol->kind,65536) || !field(symbol->scope,65536) || !field(symbol->signature,65536)) { ok=fail(error,G_IO_ERROR_INVALID_DATA,"Project report symbol fields must be bounded UTF-8.");goto done; }
        g_ptr_array_add(group,(gpointer)symbol);
    }
    if(!options->file_list) {
        gchar *project=escaped(snapshot->project),*root=escaped(snapshot->root);
        g_string_append_printf(out,"Project Report: %s\nRoot: %s\nFiles: %u\nSymbols: %u\nSkipped by indexer: %u\nSnapshot: %s\n\n",project,root,snapshot->paths->len,count,snapshot->skipped,snapshot->incomplete ? "incomplete / bounded index":"completed index");g_free(project);g_free(root);
        g_string_append(out,"Symbols reflect the captured index, including published open-buffer parses.\n\n");
    }
    for(guint i=0;i<snapshot->paths->len;i++) {
        if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) { ok=FALSE;goto done; }
        const gchar *path=g_ptr_array_index(snapshot->paths,i);gchar *display=display_path(snapshot->root,path,options->relative_paths);
        if(options->file_list) {
            gsize size=strlen(display);
            if(strpbrk(display,"\r\n*?") || (size && (g_ascii_isspace(display[0]) || g_ascii_isspace(display[size-1])))) {
                g_free(display);ok=fail(error,G_IO_ERROR_INVALID_DATA,"Plain file lists cannot represent line breaks, wildcards or edge whitespace in filenames.");goto done;
            }
            if(*display==';') g_string_append(out,"./");g_string_append(out,display);g_string_append_c(out,'\n');g_free(display);
        } else {
            gchar *safe=escaped(display);g_free(display);g_string_append_printf(out,"File: %s\n",safe);g_free(safe);
            GPtrArray *group=g_hash_table_lookup(groups,path);g_ptr_array_sort_with_data(group,symbol_order,GINT_TO_POINTER(options->sort_names));
            for(guint n=0;n<group->len;n++) {
                const InsightSymbol *symbol=g_ptr_array_index(group,n);
                gchar *name=escaped(symbol->name),*kind=escaped(symbol->kind),*scope=escaped(symbol->scope),*signature=escaped(symbol->signature);
                if(options->line_numbers) g_string_append_printf(out,"  %lu\t",symbol->line);else g_string_append(out,"  ");
                g_string_append_printf(out,"%s\t%s",kind,name);
                if(*scope) g_string_append_printf(out,"\tscope=%s",scope);
                if(*signature) g_string_append_printf(out,"\t%s",signature);
                g_string_append_c(out,'\n');g_free(name);g_free(kind);g_free(scope);g_free(signature);
                if(!bounded(out,error)) { ok=FALSE;goto done; }
            }
            g_string_append_c(out,'\n');
        }
        if(options->file_list && out->len>2u*1024u*1024u) { ok=fail(error,G_IO_ERROR_NO_SPACE,"Project file list exceeds the 2 MiB Master File List limit.");goto done; }
        if(!bounded(out,error)) { ok=FALSE;goto done; }
    }
done:
    g_hash_table_unref(groups);
    if(!ok) { g_string_free(out,TRUE);return NULL; }
    if(length) *length=out->len;return g_string_free(out,FALSE);
}

static gboolean parents_regular(GFile *file,GError **error)
{
    GFile *parent=g_file_get_parent(file);if(!parent) return TRUE;
    GFileInfo *info=g_file_query_info(parent,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);
    if(!info) { g_object_unref(parent);return FALSE; }
    gboolean ok=g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY;g_object_unref(info);
    if(!ok) { g_object_unref(parent);return fail(error,G_IO_ERROR_NOT_DIRECTORY,"Report destinations require real parent directories, not symbolic links."); }
    ok=parents_regular(parent,error);g_object_unref(parent);return ok;
}
static gboolean same_file(GFile *target,const gchar *target_id,const gchar *path,gboolean identity)
{
    GFile *other=g_file_new_for_path(path);gboolean same=g_file_equal(target,other);
    if(!same && identity && target_id) {
        GFileInfo *info=g_file_query_info(other,G_FILE_ATTRIBUTE_ID_FILE,G_FILE_QUERY_INFO_NONE,NULL,NULL);
        same=info && !g_strcmp0(target_id,g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE));g_clear_object(&info);
    }
    g_object_unref(other);return same;
}
gboolean insight_project_report_save(const InsightReportSnapshot *snapshot,const gchar *path,const gchar *text,gsize size,GPtrArray *open_paths,GError **error)
{
    if(!snapshot || !path || !g_path_is_absolute(path) || !text || size>INSIGHT_REPORT_LIMIT) return fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Invalid project report destination or size.");
    GFile *target=g_file_new_for_path(path);gboolean ok=FALSE;GFileInfo *info=NULL;
    if(!parents_regular(target,error)) goto done;
    info=g_file_query_info(target,G_FILE_ATTRIBUTE_ID_FILE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,NULL);
    const gchar *id=info ? g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE):NULL;
    for(guint i=0;i<=snapshot->paths->len;i++) {
        const gchar *utf8=i==snapshot->paths->len ? snapshot->project_file:g_ptr_array_index(snapshot->paths,i);
        gchar *native=g_filename_from_utf8(utf8,-1,NULL,NULL,error);if(!native) goto done;
        gboolean protected=same_file(target,id,native,i==snapshot->paths->len);g_free(native);
        if(protected) { fail(error,G_IO_ERROR_PERMISSION_DENIED,"Choose an output outside the captured source files and project file.");goto done; }
    }
    for(guint i=0;open_paths && i<open_paths->len;i++) if(same_file(target,id,g_ptr_array_index(open_paths,i),TRUE)) {
        fail(error,G_IO_ERROR_PERMISSION_DENIED,"Close the destination document before saving a project report.");goto done;
    }
    ok=insight_text_file_write(path,text,size,error);
done:
    g_clear_object(&info);g_object_unref(target);return ok;
}
