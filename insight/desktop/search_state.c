/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "search_state.h"
#include <string.h>
InsightSearchState *insight_search_state_new(void) {
 InsightSearchState *s=g_new0(InsightSearchState,1); s->query=g_strdup(""); s->mask=g_strdup(""); s->context=3; s->files=g_ptr_array_new_with_free_func(g_free); return s;
}
void insight_search_state_free(InsightSearchState *s) { if(!s) return; g_free(s->query); g_free(s->mask); g_ptr_array_free(s->files,TRUE); g_free(s); }
static gboolean valid(const InsightSearchState *s,GError **error) {
 if(!s || !s->query || !s->mask || !s->files || strlen(s->query)>4096 || strlen(s->mask)>4096 || !g_utf8_validate(s->query,-1,NULL) || !g_utf8_validate(s->mask,-1,NULL) || s->flags>7 || s->scope>3 || s->context>50 || s->files->len>10000) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid search settings"); return FALSE;
 }
 for(guint i=0;i<s->files->len;i++) { const char *path=g_ptr_array_index(s->files,i); if(!path || strlen(path)>16384 || !g_utf8_validate(path,-1,NULL) || !g_path_is_absolute(path)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid selected search path"); return FALSE; } }
 return TRUE;
}
gboolean insight_search_state_save(const InsightSearchState *s,const char *path,GError **error) {
 if(!path || !valid(s,error)) return FALSE;
 GKeyFile *key=g_key_file_new();
 g_key_file_set_integer(key,"search","version",1); g_key_file_set_string(key,"search","query",s->query); g_key_file_set_string(key,"search","mask",s->mask);
 g_key_file_set_integer(key,"search","flags",(gint)s->flags); g_key_file_set_integer(key,"search","scope",(gint)s->scope); g_key_file_set_integer(key,"search","context",(gint)s->context); g_key_file_set_boolean(key,"search","keywords",s->keywords);
 g_key_file_set_boolean(key,"search","variations",s->variations);
 g_key_file_set_string_list(key,"search","files",(const gchar *const *)s->files->pdata,s->files->len);
 gsize length; gchar *data=g_key_file_to_data(key,&length,NULL); g_key_file_unref(key); gboolean ok=FALSE;
 if(length>8u*1024u*1024u) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Search settings exceed 8 MiB");
 else { GFile *file=g_file_new_for_path(path); ok=g_file_replace_contents(file,data,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error); g_object_unref(file); }
 g_free(data); return ok;
}
InsightSearchState *insight_search_state_load(const char *path,GError **error) {
 if(!path) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Missing search settings path"); return NULL; }
 GFile *file=g_file_new_for_path(path); GFileInputStream *stream=g_file_read(file,NULL,error); g_object_unref(file); if(!stream) return NULL;
 GString *data=g_string_new(NULL); gchar buffer[16384]; gboolean ok=TRUE;
 for(;;) { gssize got=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,NULL,error); if(got<0) { ok=FALSE; break; } if(!got) break; if(data->len+(gsize)got>8u*1024u*1024u) { ok=FALSE; g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Search settings exceed 8 MiB"); break; } g_string_append_len(data,buffer,got); }
 g_object_unref(stream); GKeyFile *key=g_key_file_new(); InsightSearchState *s=insight_search_state_new();
 if(ok) { ok=!memchr(data->str,0,data->len) && g_utf8_validate(data->str,(gssize)data->len,NULL); if(!ok) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid search settings bytes"); }
 if(ok) ok=g_key_file_load_from_data(key,data->str,data->len,G_KEY_FILE_NONE,error);
 if(ok) {
  GError *e=NULL; gint version=g_key_file_get_integer(key,"search","version",&e); ok=!e && version==1;
  if(!ok && !e) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Unknown search settings version");
  g_free(s->query); g_free(s->mask); s->query=NULL; s->mask=NULL;
  if(ok) { s->query=g_key_file_get_string(key,"search","query",&e); ok=!e; }
  if(ok) { s->mask=g_key_file_get_string(key,"search","mask",&e); ok=!e; }
  if(ok) { s->flags=(guint)g_key_file_get_integer(key,"search","flags",&e); ok=!e; }
  if(ok) { s->scope=(guint)g_key_file_get_integer(key,"search","scope",&e); ok=!e; }
  if(ok) { s->context=(guint)g_key_file_get_integer(key,"search","context",&e); ok=!e; }
  if(ok) { s->keywords=g_key_file_get_boolean(key,"search","keywords",&e); ok=!e; }
  if(ok && g_key_file_has_key(key,"search","variations",NULL)) { s->variations=g_key_file_get_boolean(key,"search","variations",&e); ok=!e; }
  if(ok) { gsize count=0; gchar **paths=g_key_file_get_string_list(key,"search","files",&count,&e); if(!e && count<=10000) for(gsize i=0;i<count;i++) g_ptr_array_add(s->files,g_strdup(paths[i])); else if(!e) g_set_error_literal(&e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Too many selected search files"); g_strfreev(paths); ok=!e; }
  if(e) g_propagate_error(error,e);
 }
 if(ok) ok=valid(s,error);
 g_key_file_unref(key); g_string_free(data,TRUE); if(!ok) { insight_search_state_free(s); return NULL; } return s;
}
