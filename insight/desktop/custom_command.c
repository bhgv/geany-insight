/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared custom-command parsing, bounded substitutions and persistent profiles.
 * RU: Общий разбор команд, ограниченные подстановки и постоянные профили. */
#include "custom_command.h"
#include "glib_compat.h"
#include <string.h>
#define LIMIT (1024u*1024u)
static gboolean fail(GError **e,const gchar *s) {g_set_error_literal(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,s);return FALSE;}
static gboolean field(const gchar *s,gsize max) {return s && strlen(s)<=max && g_utf8_validate(s,-1,NULL) && !strpbrk(s,"\r\n");}
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
#include "custom-command/links.inc"
#include "custom-command/profiles.inc"

#include "custom-command/bulk.inc"

#include "custom-command/selection.inc"
