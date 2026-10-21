/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "backup_store.h"
#include <string.h>
#define ATTRS G_FILE_ATTRIBUTE_STANDARD_NAME "," G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE "," G_FILE_ATTRIBUTE_ETAG_VALUE "," G_FILE_ATTRIBUTE_ID_FILE
static gboolean fail(GError **error,GIOErrorEnum code,const gchar *message)
{ g_set_error_literal(error,G_IO_ERROR,code,message);return FALSE; }
static gboolean valid_text(const gchar *text,gsize size,GError **error)
{
    if(size>INSIGHT_BACKUP_TEXT_LIMIT) return fail(error,G_IO_ERROR_NO_SPACE,"Backup snapshots are limited to 8 MiB.");
    if(!text || memchr(text,0,size) || !g_utf8_validate(text,size,NULL)) return fail(error,G_IO_ERROR_INVALID_DATA,"Backup snapshots require valid UTF-8 without NUL bytes.");
    return TRUE;
}
static gboolean absolute(const gchar *path,GError **error)
{
    if(path && g_path_is_absolute(path)) return TRUE;
    return fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Backup storage requires an absolute local path.");
}
/* Walk from the root down, never deliberately traversing a symbolic link.
 * GIO checks are portable but cannot eliminate concurrent path substitutions. */
static gboolean directory_check(GFile *directory,gboolean create,GError **error)
{
    GFile *parent=g_file_get_parent(directory);
    if(parent) { gboolean ok=directory_check(parent,FALSE,error);g_object_unref(parent);if(!ok) return FALSE; }
    GError *query=NULL;
    GFileInfo *info=g_file_query_info(directory,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,&query);
    if(!info && create && g_error_matches(query,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) {
        g_clear_error(&query);
        if(!g_file_make_directory(directory,NULL,&query) && !g_error_matches(query,G_IO_ERROR,G_IO_ERROR_EXISTS)) { g_propagate_error(error,query);return FALSE; }
        g_clear_error(&query);
        info=g_file_query_info(directory,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,&query);
    }
    if(!info) { g_propagate_error(error,query);return FALSE; }
    gboolean ok=g_file_info_get_file_type(info)==G_FILE_TYPE_DIRECTORY;g_object_unref(info);
    return ok || fail(error,G_IO_ERROR_NOT_DIRECTORY,"Backup folders must be real directories, not symbolic links.");
}
gchar *insight_backup_directory(const gchar *config,const gchar *source,GError **error)
{
    if(!absolute(config,error) || !absolute(source,error)) return NULL;
    gchar *canonical=g_canonicalize_filename(source,NULL),*hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,canonical,-1);
    gchar *directory=g_build_filename(config,"insight-backups",hash,NULL);g_free(canonical);g_free(hash);return directory;
}
static gboolean snapshot_name(const gchar *name)
{
    if(!name || strlen(name)!=64 || name[8]!='T' || name[21]!='Z' || name[22]!='-' || strcmp(name+59,".utf8")) return FALSE;
    for(guint i=0;i<21;i++) if(i!=8 && !g_ascii_isdigit(name[i])) return FALSE;
    gchar *uuid=g_strndup(name+23,36);gboolean valid=g_uuid_string_is_valid(uuid);g_free(uuid);return valid;
}
static void entry_free(gpointer data)
{
    InsightBackupEntry *entry=data;g_free(entry->name);g_free(entry->etag);g_free(entry->file_id);g_free(entry);
}
static gint newest(gconstpointer a,gconstpointer b)
{ return strcmp((*(InsightBackupEntry * const *)b)->name,(*(InsightBackupEntry * const *)a)->name); }
GPtrArray *insight_backup_list(const gchar *directory,GError **error)
{
    if(!absolute(directory,error)) return NULL;
    GPtrArray *entries=g_ptr_array_new_with_free_func(entry_free);GFile *folder=g_file_new_for_path(directory);GError *query=NULL;
    if(!directory_check(folder,FALSE,&query)) {
        if(g_error_matches(query,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) { g_clear_error(&query);g_object_unref(folder);return entries; }
        g_propagate_error(error,query);g_object_unref(folder);g_ptr_array_free(entries,TRUE);return NULL;
    }
    GFileEnumerator *scan=g_file_enumerate_children(folder,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);g_object_unref(folder);
    if(!scan) { g_ptr_array_free(entries,TRUE);return NULL; }
    gsize total=0;guint visited=0;gboolean ok=TRUE;
    for(;;) {
        GFileInfo *info=g_file_enumerator_next_file(scan,NULL,&query);
        if(!info) { if(query) { g_propagate_error(error,query);query=NULL;ok=FALSE; }break; }
        if(++visited>1000) { g_object_unref(info);ok=fail(error,G_IO_ERROR_NO_SPACE,"Backup folder scan exceeds 1000 entries.");break; }
        const gchar *name=g_file_info_get_name(info);
        if(!snapshot_name(name)) { g_object_unref(info);continue; }
        goffset size=g_file_info_get_size(info);const gchar *etag=g_file_info_get_etag(info);
        if(g_file_info_get_file_type(info)!=G_FILE_TYPE_REGULAR || !etag || !*etag || size<0 || size>INSIGHT_BACKUP_TEXT_LIMIT) {
            g_object_unref(info);ok=fail(error,G_IO_ERROR_INVALID_DATA,"A backup entry is not a bounded regular file with an identity.");break;
        }
        if(entries->len>=INSIGHT_BACKUP_COUNT_LIMIT || (gsize)size>INSIGHT_BACKUP_TOTAL_LIMIT-total) {
            g_object_unref(info);ok=fail(error,G_IO_ERROR_NO_SPACE,"Backup storage is limited to 200 versions and 64 MiB per source file.");break;
        }
        InsightBackupEntry *entry=g_new0(InsightBackupEntry,1);entry->name=g_strdup(name);entry->etag=g_strdup(etag);
        entry->file_id=g_strdup(g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE));entry->size=size;
        total+=size;g_ptr_array_add(entries,entry);g_object_unref(info);
    }
    g_object_unref(scan);
    if(!ok) { g_ptr_array_free(entries,TRUE);return NULL; }
    g_ptr_array_sort(entries,newest);return entries;
}
gboolean insight_backup_save(const gchar *directory,const gchar *text,gsize size,gchar **saved,GError **error)
{
    if(saved) *saved=NULL;
    if(!valid_text(text,size,error)) return FALSE;
    GPtrArray *entries=insight_backup_list(directory,error);if(!entries) return FALSE;
    gsize total=0;for(guint i=0;i<entries->len;i++) total+=((InsightBackupEntry *)g_ptr_array_index(entries,i))->size;
    gboolean room=entries->len<INSIGHT_BACKUP_COUNT_LIMIT && size<=INSIGHT_BACKUP_TOTAL_LIMIT-total;
    g_ptr_array_free(entries,TRUE);
    if(!room) return fail(error,G_IO_ERROR_NO_SPACE,"Backup storage is limited to 200 versions and 64 MiB per source file.");
    GFile *folder=g_file_new_for_path(directory),*root=g_file_get_parent(folder);gboolean ok=FALSE,created=FALSE;
    GFile *temporary=NULL,*target=NULL;GFileOutputStream *output=NULL;
    if(!root || !directory_check(root,TRUE,error) || !directory_check(folder,TRUE,error)) goto done;
    gchar *uuid=g_uuid_string_random(),*temp_name=g_strconcat(".pending-",uuid,NULL);
    GDateTime *now=g_date_time_new_now_utc();gchar *stamp=g_date_time_format(now,"%Y%m%dT%H%M%S");
    gchar *name=g_strdup_printf("%s%06dZ-%s.utf8",stamp,g_date_time_get_microsecond(now),uuid);
    temporary=g_file_get_child(folder,temp_name);target=g_file_get_child(folder,name);
    g_free(uuid);g_free(temp_name);g_free(stamp);g_date_time_unref(now);g_free(name);
    output=g_file_create(temporary,G_FILE_CREATE_PRIVATE,NULL,error);if(!output) goto done;created=TRUE;
    if(!g_output_stream_write_all(G_OUTPUT_STREAM(output),text,size,NULL,NULL,error) || !g_output_stream_close(G_OUTPUT_STREAM(output),NULL,error)) goto done;
    if(!directory_check(folder,FALSE,error)) goto done;
    ok=g_file_move(temporary,target,G_FILE_COPY_NOFOLLOW_SYMLINKS|G_FILE_COPY_NO_FALLBACK_FOR_MOVE,NULL,NULL,NULL,error);
    if(ok && saved) *saved=g_file_get_path(target);
done:
    g_clear_object(&output);if(created && !ok) g_file_delete(temporary,NULL,NULL);
    g_clear_object(&temporary);g_clear_object(&target);g_clear_object(&root);g_object_unref(folder);return ok;
}
static gboolean same(const InsightBackupEntry *entry,GFileInfo *info)
{
    return info && g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR &&
        g_file_info_get_size(info)==(goffset)entry->size && !g_strcmp0(entry->etag,g_file_info_get_etag(info)) &&
        !g_strcmp0(entry->file_id,g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE));
}
gchar *insight_backup_read(const gchar *directory,const InsightBackupEntry *entry,gsize *size,GError **error)
{
    if(!absolute(directory,error)) return NULL;
    if(!entry || !snapshot_name(entry->name) || !entry->etag || !*entry->etag || entry->size>INSIGHT_BACKUP_TEXT_LIMIT) { fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Choose a listed backup version.");return NULL; }
    GFile *folder=g_file_new_for_path(directory),*file=NULL;GFileInputStream *input=NULL;GFileInfo *info=NULL;gchar *text=NULL;
    if(!directory_check(folder,FALSE,error)) goto done;
    file=g_file_get_child(folder,entry->name);info=g_file_query_info(file,ATTRS,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,error);
    if(!info) goto done;
    if(!same(entry,info)) { fail(error,G_IO_ERROR_WRONG_ETAG,"The backup changed. Refresh the list before opening it.");goto done; }
    g_clear_object(&info);input=g_file_read(file,NULL,error);if(!input) goto done;
    info=g_file_input_stream_query_info(input,ATTRS,NULL,error);if(!info) goto done;
    if(!same(entry,info)) { fail(error,G_IO_ERROR_WRONG_ETAG,"The backup changed. Refresh the list before opening it.");goto done; }
    text=g_malloc(entry->size+2);gsize length=0;
    if(!g_input_stream_read_all(G_INPUT_STREAM(input),text,entry->size+1,&length,NULL,error)) { g_clear_pointer(&text,g_free);goto done; }
    g_clear_object(&info);info=g_file_input_stream_query_info(input,ATTRS,NULL,error);
    if(!info) { g_clear_pointer(&text,g_free);goto done; }
    if(!same(entry,info) || length!=entry->size) { fail(error,G_IO_ERROR_WRONG_ETAG,"The backup changed. Refresh the list before opening it.");g_clear_pointer(&text,g_free);goto done; }
    if(!valid_text(text,length,error)) { g_clear_pointer(&text,g_free);goto done; }
    text[length]=0;if(size) *size=length;
done:
    g_clear_object(&info);g_clear_object(&input);g_clear_object(&file);g_object_unref(folder);return text;
}
