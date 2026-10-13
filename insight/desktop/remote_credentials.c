/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "remote_credentials.h"
#include <string.h>
static GKeyFile *read_config(const char *path,GError **error) {
 GKeyFile *key=g_key_file_new(); if(!g_file_test(path,G_FILE_TEST_EXISTS)) return key;
 gchar *data=NULL; gsize size=0;
 if(!g_file_get_contents(path,&data,&size,error)) { g_key_file_unref(key); return NULL; }
 gboolean ok=size<=4u*1024u*1024u && !memchr(data,0,size) && g_utf8_validate(data,(gssize)size,NULL);
 if(ok) ok=g_key_file_load_from_data(key,data,size,G_KEY_FILE_KEEP_COMMENTS|G_KEY_FILE_KEEP_TRANSLATIONS,error);
 else g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid SSH/project config");
 g_free(data); if(!ok) { g_key_file_unref(key); return NULL; } return key;
}
static gboolean target_valid(const char *target) { if(!target || strlen(target)!=64) return FALSE; for(const char *p=target;*p;p++) if(!g_ascii_isxdigit(*p)) return FALSE; return TRUE; }
static gchar *group(const char *target) { return g_strdup_printf("insight-ssh-password-%s",target); }
gchar *insight_remote_password_load(const char *path,const char *target,GError **error) {
 if(!path || !target_valid(target)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid SSH credentials target"); return NULL; }
 GKeyFile *key=read_config(path,error); if(!key) return NULL; gchar *name=group(target),*password=g_key_file_get_string(key,name,"password",NULL); g_free(name); g_key_file_unref(key); return password;
}
static gboolean write_password(const char *path,const char *target,const char *password,GError **error) {
 if(!path || !target_valid(target) || (password && (strlen(password)>4096 || !g_utf8_validate(password,-1,NULL) || strchr(password,'\n') || strchr(password,'\r')))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid SSH password"); return FALSE; }
 GKeyFile *key=read_config(path,error); if(!key) return FALSE; gchar *name=group(target);
 if(password) g_key_file_set_string(key,name,"password",password); else g_key_file_remove_group(key,name,NULL);
 gsize length=0; gchar *data=g_key_file_to_data(key,&length,NULL); GFile *file=g_file_new_for_path(path);
 gboolean ok=g_file_replace_contents(file,data,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);
 g_object_unref(file); g_free(data); g_free(name); g_key_file_unref(key); return ok;
}
gboolean insight_remote_password_save(const char *path,const char *target,const char *password,GError **error) { return password && write_password(path,target,password,error); }
gboolean insight_remote_password_forget(const char *path,const char *target,GError **error) { return write_password(path,target,NULL,error); }
