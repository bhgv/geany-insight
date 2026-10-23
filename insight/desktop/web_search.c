/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "web_search.h"
#include <string.h>
static gchar *fail(GError **error,const gchar *message) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,message);return NULL; }
gchar *insight_web_search_url(const gchar *pattern,const gchar *term,GError **error) {
 if(!pattern || !term || strlen(pattern)>4096 || strlen(term)>4096 || !*term || !g_utf8_validate(pattern,-1,NULL) || !g_utf8_validate(term,-1,NULL)) return fail(error,"URL template and query must be valid UTF-8, up to 4096 bytes each");
 const gchar *placeholder=strstr(pattern,"%S");if(!placeholder || strstr(placeholder+2,"%S")) return fail(error,"URL template must contain exactly one %S");
 guint prefix=!g_ascii_strncasecmp(pattern,"https://",8) ? 8 : !g_ascii_strncasecmp(pattern,"http://",7) ? 7 : 0;
 if(!prefix) return fail(error,"Only HTTP and HTTPS search URLs are supported");
 const gchar *end=pattern+prefix+strcspn(pattern+prefix,"/?#");if(end==pattern+prefix || placeholder<end || memchr(pattern+prefix,'@',end-pattern-prefix)) return fail(error,"Use a host without credentials and put %S after the host");
 for(const guchar *p=(const guchar *)pattern;*p;p++) {
  if(*p<=32 || *p==127) return fail(error,"Encode spaces and control characters in the URL template");
  if(*p=='%' && (const gchar *)p!=placeholder && !(g_ascii_isxdigit(p[1]) && p[1] && p[2] && g_ascii_isxdigit(p[2]))) return fail(error,"URL template contains an invalid percent escape");
 }
 gchar *probe=g_strdup_printf("%.*squery%s",(gint)(placeholder-pattern),pattern,placeholder+2);GSocketConnectable *address=g_network_address_parse_uri(probe,443,error);g_free(probe);if(!address) return NULL;g_object_unref(address);
 for(const gchar *p=term;*p;p=g_utf8_next_char(p)) if(g_unichar_iscntrl(g_utf8_get_char(p))) return fail(error,"Search query must be a single line without control characters");
 gchar *escaped=g_uri_escape_string(term,NULL,FALSE),*url=g_strdup_printf("%.*s%s%s",(gint)(placeholder-pattern),pattern,escaped,placeholder+2);g_free(escaped);return url;
}
