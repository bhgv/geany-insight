/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "remote_io.h"
#include <string.h>
static gboolean valid_path(const char *path) { return path && *path && strlen(path)<=16384 && g_utf8_validate(path,-1,NULL) && !strchr(path,'\n') && !strchr(path,'\r'); }
static gboolean identifier(const char *s,gboolean host) {
 if(!s || !*s || (!g_ascii_isalnum(*s) && !(host && *s==':') && *s!='_') || strlen(s)>255) return FALSE;
 for(;*s;s++) if(!g_ascii_isalnum(*s) && !strchr(host ? "._-:" : "._-",*s)) return FALSE;
 return TRUE;
}
InsightRemote *insight_remote_new(const char *host,const char *user,guint port,const char *identity,const char *remote,const char *local,GError **error) {
 if(!identifier(host,TRUE) || (user && *user && !identifier(user,FALSE)) || !port || port>65535 || !valid_path(remote) || *remote!='/' || !valid_path(local) || !g_path_is_absolute(local) || (identity && *identity && !valid_path(identity))) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Use a host/SSH alias, valid user and port, absolute remote path and local cache path"); return NULL;
 }
 InsightRemote *r=g_new0(InsightRemote,1); r->host=g_strdup(host); r->user=g_strdup(user ? user : ""); r->port=port; r->identity=g_strdup(identity ? identity : ""); r->remote=g_strdup(remote); r->local=g_canonicalize_filename(local,NULL); return r;
}
void insight_remote_free(InsightRemote *r) { if(!r) return; g_free(r->host); g_free(r->user); g_free(r->identity); g_free(r->remote); g_free(r->local); g_free(r); }
gchar **insight_remote_argv(const InsightRemote *r,gboolean sftp) {
 GPtrArray *args=g_ptr_array_new();
 g_ptr_array_add(args,g_strdup(sftp ? "sftp" : "ssh"));
 g_ptr_array_add(args,g_strdup("-oBatchMode=no")); g_ptr_array_add(args,g_strdup("-oConnectTimeout=15"));
 g_ptr_array_add(args,g_strdup("-oServerAliveInterval=15")); g_ptr_array_add(args,g_strdup("-oServerAliveCountMax=3"));
 if(*r->user) g_ptr_array_add(args,g_strdup_printf("-oUser=%s",r->user));
 if(*r->identity) { g_ptr_array_add(args,g_strdup("-i")); g_ptr_array_add(args,g_strdup(r->identity)); }
 g_ptr_array_add(args,g_strdup(sftp ? "-P" : "-p")); g_ptr_array_add(args,g_strdup_printf("%u",r->port));
 if(sftp) { g_ptr_array_add(args,g_strdup("-b")); g_ptr_array_add(args,g_strdup("-")); }
 else g_ptr_array_add(args,g_strdup("-tt"));
 g_ptr_array_add(args,g_strdup("--")); g_ptr_array_add(args,sftp && strchr(r->host,':') ? g_strdup_printf("[%s]",r->host) : g_strdup(r->host));
 g_ptr_array_add(args,NULL); return (gchar **)g_ptr_array_free(args,FALSE);
}
/* OpenSSH's quoted pathname parser protects glob characters itself. Escape
 * quotes/backslashes only; extra glob escapes would become literal slashes. */
static gchar *quote(const char *path,gboolean glob) {
 (void)glob;
 GString *s=g_string_new("\"");
 for(const char *p=path;*p;p++) {
  if(*p=='\\' || *p=='"') g_string_append_c(s,'\\');
  g_string_append_c(s,*p);
 }
 g_string_append_c(s,'"'); return g_string_free(s,FALSE);
}
gchar *insight_remote_download(const char *remote,const char *local,gboolean folder,GError **error) {
 if(!valid_path(remote) || *remote!='/' || !valid_path(local)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid SFTP path"); return NULL; }
 gchar *a=quote(remote,TRUE),*b=quote(local,FALSE),*batch=g_strdup_printf("get %s%s %s\nbye\n",folder ? "-R " : "",a,b); g_free(a); g_free(b); return batch;
}
gchar *insight_remote_upload(const char *local,const char *remote,const char *nonce,GError **error) {
 if(!valid_path(local) || !valid_path(remote) || *remote!='/' || !identifier(nonce,FALSE)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid SFTP upload path"); return NULL; }
 gchar *temporary=g_strdup_printf("%s.geany-insight-%s.tmp",remote,nonce),*a=quote(local,TRUE),*b=quote(temporary,FALSE),*c=quote(remote,FALSE);
 gchar *batch=g_strdup_printf("put %s %s\nrename %s %s\nbye\n",a,b,b,c); g_free(a); g_free(b); g_free(c); g_free(temporary); return batch;
}
gchar *insight_remote_path(const InsightRemote *r,const char *local) {
 gchar *canonical=g_canonicalize_filename(local,NULL); gsize prefix=strlen(r->local); gchar *result=NULL;
 if(!strncmp(canonical,r->local,prefix) && G_IS_DIR_SEPARATOR(canonical[prefix])) {
  gchar *relative=g_strdup(canonical+prefix+1); for(char *p=relative;*p;p++) if(G_IS_DIR_SEPARATOR(*p)) *p='/';
  result=g_strdup_printf("%s%s%s",r->remote,g_str_has_suffix(r->remote,"/") ? "" : "/",relative); g_free(relative);
 }
 g_free(canonical); return result;
}
gchar *insight_remote_normalize(const char *base,const char *path) {
 gchar *joined=*path=='/' ? g_strdup(path) : g_strdup_printf("%s/%s",base,path),**parts=g_strsplit(joined,"/",-1); GPtrArray *stack=g_ptr_array_new();
 for(guint i=0;parts[i];i++) { if(!*parts[i] || !strcmp(parts[i],".")) continue; if(!strcmp(parts[i],"..")) { if(stack->len) g_ptr_array_remove_index(stack,stack->len-1); } else g_ptr_array_add(stack,parts[i]); }
 GString *out=g_string_new(""); for(guint i=0;i<stack->len;i++) { g_string_append_c(out,'/'); g_string_append(out,g_ptr_array_index(stack,i)); } if(!out->len) g_string_append_c(out,'/');
 g_ptr_array_free(stack,TRUE); g_strfreev(parts); g_free(joined); return g_string_free(out,FALSE);
}
