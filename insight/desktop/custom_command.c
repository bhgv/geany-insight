/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "custom_command.h"
#include <string.h>
#define LIMIT (1024u*1024u)
static gboolean fail(GError **e,const gchar *s) {g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,s);return FALSE;}
static gboolean field(const gchar *s,gsize max) {return s && strlen(s)<=max && g_utf8_validate(s,-1,NULL) && !strpbrk(s,"\r\n");}
void insight_custom_command_free(gpointer p) {InsightCustomCommand *c=p;if(c) {g_free(c->name);g_free(c->command);g_free(c->directory);g_free(c);}}
gchar *insight_custom_expand(const gchar *input,GHashTable *values,GError **error) {
 if(!field(input,16384) || !values) {fail(error,"Invalid custom command template");return NULL;}
 GString *out=g_string_new(NULL);guint substitutions=0;
 for(const gchar *p=input;*p;) {
  if(*p!='%') {g_string_append_c(out,*p++);continue;}
  if(p[1]=='%') {g_string_append_c(out,'%');p+=2;continue;}
  if(!p[1] || ++substitutions>128) {fail(error,"Invalid custom command substitution");goto invalid;}
  const gchar *end=strchr(p+1,'%');gboolean long_name=end && end-p>2 && end-p<=65;
  if(long_name) for(const gchar *q=p+1;q<end;q++) if(!g_ascii_isalnum(*q) && *q!='_') {long_name=FALSE;break;}
  if(!long_name && strchr("frnbe",p[1]) && p[2]=='%' && (p[3]=='o' || p[3]=='m')) {fail(error,"Bulk custom command modifiers are not supported");goto invalid;}
  gchar *name=long_name ? g_ascii_strup(p+1,end-p-1):g_strndup(p+1,1);
  const gchar *value=g_hash_table_lookup(values,name);g_free(name);
  if(!value || !field(value,16384)) {fail(error,"Custom command variable is unavailable");goto invalid;}
  if(out->len+strlen(value)>65536) {fail(error,"Expanded custom command is too large");goto invalid;}
  g_string_append(out,value);p=long_name ? end+1:p+2;
 }
 return g_string_free(out,FALSE);
invalid:g_string_free(out,TRUE);return NULL;
}
gchar **insight_custom_argv(const gchar *input,GHashTable *values,GError **error) {
 if(!field(input,16384)) {fail(error,"Invalid custom command template");return NULL;}
 gint count=0;gchar **argv=NULL;if(!g_shell_parse_argv(input,&count,&argv,error)) return NULL;
 if(count>128) {fail(error,"Custom command has too many arguments");g_strfreev(argv);return NULL;}
 gsize total=0;for(gint i=0;i<count;i++) {gchar *expanded=insight_custom_expand(argv[i],values,error);if(!expanded) {g_strfreev(argv);return NULL;}g_free(argv[i]);argv[i]=expanded;total+=strlen(expanded)+1;}
 if(!*argv[0] || total>256u*1024u) {fail(error,"Expanded custom command is too large");g_strfreev(argv);return NULL;}return argv;
}
static gboolean valid(InsightCustomCommand *c) {return c && field(c->name,128) && *c->name && field(c->command,16384) && *c->command && field(c->directory,16384);}
GPtrArray *insight_custom_commands_load(const gchar *path,GError **error) {
 GPtrArray *list=g_ptr_array_new_with_free_func(insight_custom_command_free);GFile *f=g_file_new_for_path(path);GError *local=NULL;GFileInputStream *in=g_file_read(f,NULL,&local);g_object_unref(f);
 if(!in) {if(g_error_matches(local,G_IO_ERROR,G_IO_ERROR_NOT_FOUND)) {g_clear_error(&local);return list;}g_propagate_error(error,local);g_ptr_array_unref(list);return NULL;}
 gchar *text=g_malloc(LIMIT+1);gsize length=0;gboolean ok=g_input_stream_read_all(G_INPUT_STREAM(in),text,LIMIT+1,&length,NULL,error);g_object_unref(in);
 if(ok && length>LIMIT) ok=fail(error,"Custom command settings exceed the size limit");
 GKeyFile *key=g_key_file_new();if(ok) ok=g_key_file_load_from_data(key,text,length,G_KEY_FILE_NONE,error);g_free(text);
 gint count=0;if(ok) {GError *local_error=NULL;count=g_key_file_get_integer(key,"commands","count",&local_error);if(local_error) {g_propagate_error(error,local_error);ok=FALSE;}}
 if(ok && (count<0 || count>64)) ok=fail(error,"Custom command count exceeds the limit");
 for(gint i=0;ok && i<count;i++) {gchar *group=g_strdup_printf("command-%d",i);InsightCustomCommand *c=g_new0(InsightCustomCommand,1);c->name=g_key_file_get_string(key,group,"name",NULL);c->command=g_key_file_get_string(key,group,"command",NULL);c->directory=g_key_file_get_string(key,group,"directory",NULL);g_free(group);if(!valid(c)) {insight_custom_command_free(c);ok=fail(error,"Invalid saved custom command");}else g_ptr_array_add(list,c);}
 g_key_file_unref(key);if(!ok) {g_ptr_array_unref(list);return NULL;}return list;
}
gboolean insight_custom_commands_save(const gchar *path,GPtrArray *list,GError **error) {
 if(!list || list->len>64) return fail(error,"Custom command count exceeds the limit");
 GKeyFile *key=g_key_file_new();g_key_file_set_integer(key,"commands","count",list->len);gboolean ok=TRUE;
 for(guint i=0;i<list->len;i++) {InsightCustomCommand *c=g_ptr_array_index(list,i);if(!valid(c)) {ok=fail(error,"Invalid saved custom command");break;}gchar *group=g_strdup_printf("command-%u",i);g_key_file_set_string(key,group,"name",c->name);g_key_file_set_string(key,group,"command",c->command);g_key_file_set_string(key,group,"directory",c->directory);g_free(group);}
 gsize length=0;gchar *text=ok ? g_key_file_to_data(key,&length,error):NULL;g_key_file_unref(key);
 if(text && length>LIMIT) ok=fail(error,"Custom command settings exceed the size limit");
 if(ok && text) {GFile *f=g_file_new_for_path(path);ok=g_file_replace_contents(f,text,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error);g_object_unref(f);}else ok=FALSE;
 g_free(text);return ok;
}
