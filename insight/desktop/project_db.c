/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "project_db.h"
#include <string.h>
#include <stdlib.h>
#define LIMIT (192u*1024u*1024u)
#define TYPE "(usssayayaya(ss)a(ss))"
static gboolean invalid(GError **e) { g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid, incompatible or damaged project database.");return FALSE; }
void insight_project_db_clear(InsightProjectDb *db) { g_free(db->policy);insight_index_free(db->symbols);insight_index_free(db->declarations);insight_index_free(db->calls);g_clear_pointer(&db->files,g_hash_table_unref);g_clear_pointer(&db->directories,g_hash_table_unref);memset(db,0,sizeof(*db)); }
GHashTable *insight_project_db_copy_table(GHashTable *from) { GHashTable *to=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);if(from) { GHashTableIter it;gpointer k,v;g_hash_table_iter_init(&it,from);while(g_hash_table_iter_next(&it,&k,&v)) g_hash_table_insert(to,g_strdup(k),g_strdup(v)); }return to; }
gchar *insight_project_db_path(const gchar *profile,const gchar *identity) { gchar *hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,identity,-1),*name=g_strconcat(hash,".pdb",NULL),*path=g_build_filename(profile,"insight","projects",name,NULL);g_free(hash);g_free(name);return path; }
gchar *insight_project_db_stamp(const gchar *native,gboolean directory) {
 GFile *f=g_file_new_for_path(native);GFileInfo *i=g_file_query_info(f,"standard::type,standard::size,etag::value,id::file,time::modified,time::modified-usec,time::changed,time::changed-usec",G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,NULL,NULL);g_object_unref(f);
 if(!i) return NULL;if(g_file_info_get_file_type(i)!=(directory?G_FILE_TYPE_DIRECTORY:G_FILE_TYPE_REGULAR)) { g_object_unref(i);return NULL; }
 const gchar *id=g_file_info_get_attribute_string(i,G_FILE_ATTRIBUTE_ID_FILE),*etag=g_file_info_get_etag(i);
 gchar *s=g_strdup_printf("%" G_GOFFSET_FORMAT ":%" G_GUINT64_FORMAT ":%u:%" G_GUINT64_FORMAT ":%u:%s:%s",directory?0:g_file_info_get_size(i),g_file_info_get_attribute_uint64(i,G_FILE_ATTRIBUTE_TIME_MODIFIED),g_file_info_get_attribute_uint32(i,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC),directory?0:g_file_info_get_attribute_uint64(i,G_FILE_ATTRIBUTE_TIME_CHANGED),directory?0:g_file_info_get_attribute_uint32(i,G_FILE_ATTRIBUTE_TIME_CHANGED_USEC),id?id:"",etag?etag:"");g_object_unref(i);return s;
}
static GVariant *table_variant(GHashTable *table) { GVariantBuilder b;g_variant_builder_init(&b,G_VARIANT_TYPE("a(ss)"));GHashTableIter it;gpointer k,v;g_hash_table_iter_init(&it,table);while(g_hash_table_iter_next(&it,&k,&v)) { if(!g_utf8_validate(k,-1,NULL)||!g_utf8_validate(v,-1,NULL)||strlen(k)>8192||strlen(v)>4096) { g_variant_builder_clear(&b);return NULL; }g_variant_builder_add(&b,"(ss)",(gchar *)k,(gchar *)v); }return g_variant_builder_end(&b); }
gboolean insight_project_db_save(const gchar *path,const gchar *identity,const gchar *root,const gchar *policy,const InsightProjectDb *db,GCancellable *cancel,GError **error) {
 if(cancel&&g_cancellable_set_error_if_cancelled(cancel,error)) return FALSE;
 const InsightIndex *indexes[]={db->symbols,db->declarations,db->calls};GVariant *parts[3]={0},*files=NULL,*dirs=NULL,*value=NULL;gboolean ok=FALSE;
 for(guint n=0;n<3;n++) { unsigned char *bytes=NULL;size_t size=0;if(!insight_index_serialize(indexes[n],root,&bytes,&size)) goto done;parts[n]=g_variant_ref_sink(g_variant_new_from_data(G_VARIANT_TYPE("ay"),bytes,size,TRUE,free,bytes)); }
 files=table_variant(db->files);dirs=table_variant(db->directories);if(!files||!dirs) goto done;
 value=g_variant_ref_sink(g_variant_new("(usss@ay@ay@ay@a(ss)@a(ss))",1u,identity,root,policy,parts[0],parts[1],parts[2],files,dirs));files=dirs=NULL;
 gsize size=g_variant_get_size(value);if(size>LIMIT) goto done;
 const guchar *data=g_variant_get_data(value);gchar *digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,data,size);GByteArray *out=g_byte_array_sized_new(size+72);g_byte_array_append(out,(guchar *)"GIPDB001",8);g_byte_array_append(out,(guchar *)digest,64);g_byte_array_append(out,data,size);g_free(digest);
 gchar *parent=g_path_get_dirname(path);if(g_mkdir_with_parents(parent,0700)==0) { GFile *file=g_file_new_for_path(path);ok=g_file_replace_contents(file,(gchar *)out->data,out->len,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,cancel,error);g_object_unref(file); }g_free(parent);g_byte_array_unref(out);
 done:for(guint n=0;n<3;n++) if(parts[n]) g_variant_unref(parts[n]);if(value) g_variant_unref(value);if(files) g_variant_unref(g_variant_ref_sink(files));if(dirs) g_variant_unref(g_variant_ref_sink(dirs));if(!ok&&error&&!*error) invalid(error);return ok;
}
static GHashTable *read_table(GVariant *v,guint limit) {
 if(g_variant_n_children(v)>limit) return NULL;GHashTable *table=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);GVariantIter it;const gchar *k,*s;g_variant_iter_init(&it,v);
 while(g_variant_iter_next(&it,"(&s&s)",&k,&s)) { if(!g_path_is_absolute(k)||strlen(k)>8192||strlen(s)>4096||g_hash_table_contains(table,k)) { g_hash_table_unref(table);return NULL; }g_hash_table_insert(table,g_strdup(k),g_strdup(s)); }return table;
}
gboolean insight_project_db_load(const gchar *path,const gchar *identity,const gchar *root,const gchar *policy,InsightProjectDb *db,GError **error) {
 memset(db,0,sizeof(*db));GFile *file=g_file_new_for_path(path);GFileInputStream *stream=g_file_read(file,NULL,error);g_object_unref(file);if(!stream) return FALSE;
 GByteArray *bytes=g_byte_array_new();guchar buffer[8192];gssize n;while((n=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,NULL,error))>0) { if(bytes->len+n>LIMIT+72) { n=-1;invalid(error);break; }g_byte_array_append(bytes,buffer,n); }g_object_unref(stream);if(n<0) { g_byte_array_unref(bytes);return FALSE; }
 gboolean ok=FALSE;GVariant *v=NULL;if(bytes->len<72||memcmp(bytes->data,"GIPDB001",8)) goto done;gchar *digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,bytes->data+72,bytes->len-72);ok=!memcmp(digest,bytes->data+8,64);g_free(digest);if(!ok) goto done;ok=FALSE;
 v=g_variant_ref_sink(g_variant_new_from_data(G_VARIANT_TYPE(TYPE),bytes->data+72,bytes->len-72,FALSE,NULL,NULL));if(!g_variant_is_normal_form(v)) goto done;
 guint version;const gchar *id,*r,*p;g_variant_get_child(v,0,"u",&version);g_variant_get_child(v,1,"&s",&id);g_variant_get_child(v,2,"&s",&r);g_variant_get_child(v,3,"&s",&p);if(version!=1||strcmp(identity,id)||strcmp(root,r)||(policy&&strcmp(policy,p))) goto done;
 db->policy=g_strdup(p);
 InsightIndex **indexes[]={&db->symbols,&db->declarations,&db->calls};for(guint k=0;k<3;k++) { GVariant *part=g_variant_get_child_value(v,k+4);gsize size;const guchar *data=g_variant_get_fixed_array(part,&size,1);char *stored=NULL;*indexes[k]=insight_index_deserialize(data,size,&stored);gboolean same=*indexes[k]&&stored&&!strcmp(stored,root);free(stored);g_variant_unref(part);if(!same) goto done; }
 GVariant *part=g_variant_get_child_value(v,7);db->files=read_table(part,10000);g_variant_unref(part);part=g_variant_get_child_value(v,8);db->directories=read_table(part,10000);g_variant_unref(part);ok=db->files&&db->directories;
 done:if(v) g_variant_unref(v);g_byte_array_unref(bytes);if(!ok) { insight_project_db_clear(db);if(error&&!*error) invalid(error); }return ok;
}
