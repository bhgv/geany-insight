/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "extensions.h"
#include <glib/gstdio.h>
#include <string.h>
#define SETTINGS_LIMIT (32u * 1024u)
#define PATH_LIMIT (16u * 1024u)
struct InsightExtensions { InsightSession *session; char *settings_path; };
static void invalid(GError **error,const char *message) {
 g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,message);
}
static gboolean idle(InsightExtensions *m,GError **error) {
 if(!m) { invalid(error,"Invalid extension manager"); return FALSE; }
 if(insight_session_is_running(m->session)) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Lua command is running"); return FALSE;
 }
 return TRUE;
}
static gboolean read_bounded(const char *path,gsize limit,char **source,gsize *size,GError **error) {
 if(source) *source=NULL;
 if(size) *size=0;
 if(!path || !source || !size) { invalid(error,"Missing file path"); return FALSE; }
 GFile *file=g_file_new_for_path(path);
 GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,
  G_FILE_QUERY_INFO_NONE,NULL,error);
 if(!info) { g_object_unref(file); return FALSE; }
 gboolean regular=g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR;
 goffset length=g_file_info_get_size(info); g_object_unref(info);
 if(!regular || length<0 || (guint64)length>limit) {
  g_object_unref(file); invalid(error,"File is not regular or exceeds size limit"); return FALSE;
 }
 GFileInputStream *stream=g_file_read(file,NULL,error); g_object_unref(file);
 if(!stream) return FALSE;
 GByteArray *bytes=g_byte_array_new(); char buffer[8192]; gssize count;
 while((count=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,NULL,error))>0) {
  if((gsize)count>limit-bytes->len) { invalid(error,"File exceeds size limit"); count=-1; break; }
  g_byte_array_append(bytes,(const guint8 *)buffer,(guint)count);
 }
 g_object_unref(stream);
 if(count<0) { g_byte_array_free(bytes,TRUE); return FALSE; }
 *size=bytes->len; g_byte_array_append(bytes,(const guint8 *)"",1);
 *source=(char *)g_byte_array_free(bytes,FALSE); return TRUE;
}
gboolean insight_read_lua_file(const char *path,char **source,gsize *size,GError **error) {
 return read_bounded(path,256u*1024u,source,size,error);
}
static gboolean save_selection(InsightExtensions *m,const char *path,GError **error) {
 if(path && (!g_path_is_absolute(path) || strlen(path)>PATH_LIMIT)) {
  invalid(error,"Extension path must be absolute and within 16 KiB"); return FALSE;
 }
 gchar *directory=g_path_get_dirname(m->settings_path);
 if(g_mkdir_with_parents(directory,0700)!=0) {
  g_free(directory); g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create extension settings directory"); return FALSE;
 }
 g_free(directory);
 GKeyFile *settings=g_key_file_new();
 g_key_file_set_integer(settings,"extension","version",1);
 gchar *encoded=path ? g_base64_encode((const guchar *)path,strlen(path)) : g_strdup("");
 g_key_file_set_string(settings,"extension","path-base64",encoded); g_free(encoded);
 gsize size=0; gchar *data=g_key_file_to_data(settings,&size,error); g_key_file_free(settings);
 if(!data) return FALSE;
 GFile *file=g_file_new_for_path(m->settings_path);
 gboolean ok=g_file_replace_contents(file,data,size,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);
 g_object_unref(file); g_free(data); return ok;
}
static InsightSession *load_candidate(const char *path,GError **error) {
 char *source=NULL; gsize size=0;
 if(!insight_read_lua_file(path,&source,&size,error)) return NULL;
 InsightSession *next=insight_session_new(); char message[512]="";
 int status=next ? insight_session_load(next,source,size,message,sizeof message) : 2;
 g_free(source);
 if(status) {
  invalid(error,message[0] ? message : "Lua runtime unavailable"); insight_session_free(next); return NULL;
 }
 return next;
}
InsightExtensions *insight_extensions_new(const char *settings_path) {
 if(!settings_path) return NULL;
 InsightSession *session=insight_session_new(); if(!session) return NULL;
 InsightExtensions *m=g_new0(InsightExtensions,1); m->session=session; m->settings_path=g_strdup(settings_path); return m;
}
void insight_extensions_free(InsightExtensions *m) {
 if(!m) return;
 insight_session_free(m->session); g_free(m->settings_path); g_free(m);
}
InsightSession *insight_extensions_session(InsightExtensions *m) { return m ? m->session : NULL; }
gboolean insight_extensions_select(InsightExtensions *m,const char *path,GError **error) {
 if(!idle(m,error)) return FALSE;
 if(!path || !g_path_is_absolute(path) || strlen(path)>PATH_LIMIT) {
  invalid(error,"Invalid extension selection"); return FALSE;
 }
 InsightSession *next=load_candidate(path,error); if(!next) return FALSE;
 if(!save_selection(m,path,error)) { insight_session_free(next); return FALSE; }
 insight_session_free(m->session); m->session=next; return TRUE;
}
gboolean insight_extensions_unload(InsightExtensions *m,GError **error) {
 if(!idle(m,error)) return FALSE;
 InsightSession *next=insight_session_new();
 if(!next) { invalid(error,"Lua runtime unavailable"); return FALSE; }
 if(!save_selection(m,NULL,error)) { insight_session_free(next); return FALSE; }
 insight_session_free(m->session); m->session=next; return TRUE;
}
gboolean insight_extensions_restore(InsightExtensions *m,GError **error) {
 if(!idle(m,error)) return FALSE;
 char *data=NULL; gsize size=0; GError *io_error=NULL;
 if(!read_bounded(m->settings_path,SETTINGS_LIMIT,&data,&size,&io_error)) {
  if(g_error_matches(io_error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) { g_clear_error(&io_error); return TRUE; }
  g_propagate_error(error,io_error); return FALSE;
 }
 GKeyFile *settings=g_key_file_new();
 if(memchr(data,0,size) || !g_key_file_load_from_data(settings,data,size,G_KEY_FILE_NONE,&io_error)) {
  if(!io_error) invalid(&io_error,"NUL in extension settings");
  g_free(data); g_key_file_free(settings); g_propagate_error(error,io_error); return FALSE;
 }
 g_free(data);
 int version=g_key_file_get_integer(settings,"extension","version",&io_error);
 if(io_error || version!=1) {
  if(!io_error) invalid(&io_error,"Unsupported extension settings version");
  g_key_file_free(settings); g_propagate_error(error,io_error); return FALSE;
 }
 gchar *encoded=g_key_file_get_string(settings,"extension","path-base64",&io_error); g_key_file_free(settings);
 if(!encoded) { g_propagate_error(error,io_error); return FALSE; }
 if(!*encoded) {
  g_free(encoded); InsightSession *next=insight_session_new();
  if(!next) { invalid(error,"Lua runtime unavailable"); return FALSE; }
  insight_session_free(m->session); m->session=next; return TRUE;
 }
 gsize length=0; guchar *raw=g_base64_decode(encoded,&length);
 gchar *canonical=g_base64_encode(raw,length);
 gboolean valid=length && length<=PATH_LIMIT && !memchr(raw,0,length) && !strcmp(encoded,canonical);
 g_free(encoded); g_free(canonical);
 if(!valid) { g_free(raw); invalid(error,"Invalid encoded extension path"); return FALSE; }
 char *path=g_strndup((const char *)raw,length); g_free(raw);
 if(!g_path_is_absolute(path)) { g_free(path); invalid(error,"Extension path is not absolute"); return FALSE; }
 InsightSession *next=load_candidate(path,error); g_free(path);
 if(!next) return FALSE;
 insight_session_free(m->session); m->session=next; return TRUE;
}
