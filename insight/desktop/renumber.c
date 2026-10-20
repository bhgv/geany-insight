/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "renumber.h"
#include <errno.h>
#include <string.h>
#define LIMIT (8u * 1024u * 1024u)
static gint base_for(const gchar *text) {
 if(*text=='-' || *text=='+') text++;
 return text[0]=='0' ? (text[1]=='x' || text[1]=='X' ? 16 : 8) : 10;
}
static gboolean number(const gchar *text,gint64 *value) {
 gchar *end=NULL;errno=0;*value=g_ascii_strtoll(text,&end,base_for(text));
 return *text && !g_ascii_isspace(*text) && end!=text && !*end && errno!=ERANGE;
}
static gboolean word(gunichar c) { return g_unichar_isalnum(c) || g_unichar_ismark(c) || c=='_' || c=='.'; }
static gchar *render(gint64 value,gint radix) {
 gboolean negative=value<0;guint64 magnitude=negative ? (guint64)(-(value+1))+1 : (guint64)value;
 if(radix==2) return g_strdup_printf("%s0x%" G_GINT64_MODIFIER "x",negative ? "-" : "",magnitude);
 if(radix==3 && magnitude) return g_strdup_printf("%s0%" G_GINT64_MODIFIER "o",negative ? "-" : "",magnitude);
 return g_strdup_printf("%" G_GINT64_FORMAT,value);
}
gchar *insight_renumber(const gchar *text,gint mode,gint radix,const gchar *value,guint *count,GError **error) {
 if(count) *count=0;
 if(!text || strlen(text)>LIMIT || !g_utf8_validate(text,-1,NULL) || mode<0 || mode>2 || radix<0 || radix>3) goto invalid;
 gint64 next=0,argument=0;gboolean exhausted=FALSE;
 if(mode && (!value || !number(value,&argument))) goto invalid;
 GString *out=g_string_sized_new(strlen(text));guint found=0;
 const gchar *p=text;
 while(*p) {
  const gchar *start=p,*digits=p;
  if((*digits=='-' || *digits=='+') && g_ascii_isdigit(digits[1])) digits++;
  if(!g_ascii_isdigit(*digits) || (p>text && word(g_utf8_get_char(g_utf8_find_prev_char(text,p))))) {
   const gchar *end=g_utf8_next_char(p);g_string_append_len(out,p,end-p);p=end;continue;
  }
  /* Consume the entire candidate so floats, suffixes and malformed octal/hex
   * are never partially rewritten. This is deliberately not a language lexer. */
  const gchar *end=digits;
  while(*end && word(g_utf8_get_char(end))) end=g_utf8_next_char(end);
  if(base_for(digits)!=16 && (*end=='+' || *end=='-') && end>digits && (end[-1]=='e' || end[-1]=='E')) {
   end++;while(*end && word(g_utf8_get_char(end))) end=g_utf8_next_char(end);
  }
  gchar *token=g_strndup(start,end-start);gint64 original=0;
  if(!number(token,&original)) {
   /* Valid integer spelling outside signed 64-bit is an error, not a skip. */
   gchar *tail=NULL;errno=0;g_ascii_strtoll(token,&tail,base_for(token));
   gboolean overflow=tail!=token && !*tail && errno==ERANGE;
   g_free(token);if(overflow) goto range;
   g_string_append_len(out,start,end-start);p=end;continue;
  }
  g_free(token);gint64 result;
  if(mode==2) {
   if((argument>0 && original>G_MAXINT64-argument) || (argument<0 && original<G_MININT64-argument)) goto range;
   result=original+argument;
  } else {
   if(!found) next=mode==1 ? argument : original;
   if(exhausted) goto range;
   result=next;if(next==G_MAXINT64) exhausted=TRUE;else next++;
  }
  gint base=radix;
  if(!base) base=digits[0]=='0' && (digits[1]=='x' || digits[1]=='X') ? 2 : digits[0]=='0' && g_ascii_isdigit(digits[1]) ? 3 : 1;
  gchar *replacement=render(result,base);g_string_append(out,replacement);g_free(replacement);found++;p=end;
  if(out->len>LIMIT) goto range;
 }
 if(out->len>LIMIT) goto range;
 if(count) *count=found;
 return g_string_free(out,FALSE);
range:
 g_string_free(out,TRUE);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Renumber exceeds the signed 64-bit or 8 MiB limit");return NULL;
invalid:
 g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Choose valid options and a signed 64-bit decimal, 0x hex or leading-zero octal value");return NULL;
}
