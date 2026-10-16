/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tm_completion.h"
#include <string.h>
struct TMCompletionPrefix { gchar *text,*folded;gboolean sensitive,ascii;gsize length; };
static gboolean valid(const gchar *text,gsize limit) { if(!text) return FALSE;gsize n=0;while(n<=limit && text[n]) n++;return n<=limit && !strpbrk(text,"\n\r\t") && g_utf8_validate(text,n,NULL); }
TMCompletionPrefix *tm_completion_prefix_new(const gchar *text,gboolean sensitive) { if(!valid(text,512)) return NULL;TMCompletionPrefix *p=g_new0(TMCompletionPrefix,1);p->text=g_strdup(text);p->sensitive=sensitive;p->ascii=g_str_is_ascii(text);p->length=strlen(text);if(!sensitive) p->folded=g_utf8_casefold(text,-1);return p; }
gboolean tm_completion_prefix_matches(const TMCompletionPrefix *p,const gchar *name) {
 if(!p || !valid(name,4096)) return FALSE;
 if(p->sensitive) return g_str_has_prefix(name,p->text);
 if(p->ascii && g_str_is_ascii(name)) return !g_ascii_strncasecmp(name,p->text,p->length);
 gchar *folded=g_utf8_casefold(name,-1);gboolean result=g_str_has_prefix(folded,p->folded);g_free(folded);return result;
}
void tm_completion_prefix_free(TMCompletionPrefix *p) { if(p) { g_free(p->text);g_free(p->folded);g_free(p); } }
