/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "snippet.h"
#include <gio/gio.h>
#include <string.h>
static gboolean valid_template(const gchar *text,GError **error) {
 if(!text || strlen(text)>65536 || !g_utf8_validate(text,-1,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid snippet text (UTF-8, up to 64 KiB)"); return FALSE; } return TRUE;
}
static gsize variable_length(const gchar *text) {
 if(*text!='$' || !(g_ascii_isalpha(text[1]) || text[1]=='_')) return 0;
 gsize i=2; while(g_ascii_isalnum(text[i]) || text[i]=='_') i++;
 return text[i]=='$' ? i+1 : 0;
}
GPtrArray *insight_snippet_variables(const gchar *text,GError **error) {
 if(!valid_template(text,error)) return NULL;
 GPtrArray *out=g_ptr_array_new_with_free_func(g_free); GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 for(gsize i=0;text[i];) {
  if(text[i]=='$' && text[i+1]=='$') { i+=2; continue; }
  gsize n=variable_length(text+i); if(!n) { i++; continue; }
  gchar *name=g_strndup(text+i+1,n-2); i+=n;
  if(!strcmp(name,"cursor") || g_hash_table_contains(seen,name)) { g_free(name); continue; }
  if(out->len==64) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Snippet variable limit reached (64)"); g_free(name); g_hash_table_unref(seen); g_ptr_array_unref(out); return NULL; }
  g_ptr_array_add(out,name); g_hash_table_add(seen,name);
 }
 g_hash_table_unref(seen); return out;
}
static gboolean append_normalized(GString *out,const gchar *text,gsize length,const gchar *eol,GError **error) {
 for(gsize i=0;i<length;i++) {
  if(text[i]=='\r' || text[i]=='\n') { if(text[i]=='\r' && i+1<length && text[i+1]=='\n') i++; g_string_append(out,eol); }
  else g_string_append_c(out,text[i]);
  if(out->len>262144) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Expanded snippet exceeds 256 KiB"); return FALSE; }
 }
 return TRUE;
}
gchar *insight_snippet_expand(const gchar *text,GHashTable *values,const gchar *eol,gsize *cursor,GError **error) {
 if(!valid_template(text,error)) return NULL;
 if(!cursor || !eol || (strcmp(eol,"\n") && strcmp(eol,"\r\n") && strcmp(eol,"\r"))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid snippet output options"); return NULL; }
 GString *out=g_string_new(NULL); gboolean have_cursor=FALSE; gsize at=0;
 for(gsize i=0;text[i];) {
  if(text[i]=='$' && text[i+1]=='$') { if(!append_normalized(out,"$",1,eol,error)) goto fail; i+=2; continue; }
  gsize n=variable_length(text+i);
  if(n) {
   gchar *name=g_strndup(text+i+1,n-2); i+=n;
   if(!strcmp(name,"cursor")) { if(!have_cursor) { at=out->len; have_cursor=TRUE; } g_free(name); continue; }
   const gchar *value=values ? g_hash_table_lookup(values,name) : NULL;
   if(!value || strlen(value)>65536 || !g_utf8_validate(value,-1,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Missing or invalid snippet variable"); g_free(name); goto fail; }
   g_free(name); if(!append_normalized(out,value,strlen(value),eol,error)) goto fail;
  } else {
   gsize start=i++; while(text[i] && text[i]!='$') i++;
   if(!append_normalized(out,text+start,i-start,eol,error)) goto fail;
  }
 }
 *cursor=have_cursor ? at : out->len; return g_string_free(out,FALSE);
fail:
 g_string_free(out,TRUE); return NULL;
}
