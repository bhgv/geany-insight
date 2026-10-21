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
typedef struct { gint64 next,argument;gboolean exhausted;guint seen; } Sequence;
static gchar *transform(const gchar *text,gint mode,gint radix,Sequence *sequence,guint *count,GError **error) {
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
   if((sequence->argument>0 && original>G_MAXINT64-sequence->argument) || (sequence->argument<0 && original<G_MININT64-sequence->argument)) goto range;
   result=original+sequence->argument;
  } else {
   if(!sequence->seen) sequence->next=mode==1 ? sequence->argument : original;
   if(sequence->exhausted) goto range;
   result=sequence->next;if(sequence->next==G_MAXINT64) sequence->exhausted=TRUE;else sequence->next++;
  }
  gint base=radix;
  if(!base) base=digits[0]=='0' && (digits[1]=='x' || digits[1]=='X') ? 2 : digits[0]=='0' && g_ascii_isdigit(digits[1]) ? 3 : 1;
  gchar *replacement=render(result,base);g_string_append(out,replacement);g_free(replacement);found++;sequence->seen++;p=end;
  if(out->len>LIMIT) goto range;
 }
 if(out->len>LIMIT) goto range;
 if(count) *count=found;
 return g_string_free(out,FALSE);
range:
 g_string_free(out,TRUE);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Renumber exceeds the signed 64-bit or 8 MiB limit");return NULL;
 }
static gboolean options(const gchar *text,gint mode,gint radix,const gchar *value,Sequence *sequence,GError **error) {
 if(text&&strlen(text)<=LIMIT&&g_utf8_validate(text,-1,NULL)&&mode>=0&&mode<=2&&radix>=0&&radix<=3&&(!mode||(value&&number(value,&sequence->argument)))) return TRUE;
 g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Choose valid options, UTF-8 text up to 8 MiB and a signed 64-bit decimal, 0x hex or leading-zero octal value");return FALSE;
}
gchar *insight_renumber(const gchar *text,gint mode,gint radix,const gchar *value,guint *count,GError **error) {
 if(count) *count=0;
 Sequence sequence={0};if(!options(text,mode,radix,value,&sequence,error)) return NULL;
 return transform(text,mode,radix,&sequence,count,error);
}
void insight_renumber_plan_free(InsightRenumberPlan *plan) { if(plan) { g_free(plan->text);g_array_unref(plan->ranges);g_free(plan); } }
InsightRenumberPlan *insight_renumber_plan(const gchar *text,const InsightRenumberRange *ranges,guint n,gint mode,gint radix,const gchar *value,GError **error) {
 Sequence sequence={0};if(!options(text,mode,radix,value,&sequence,error)) return NULL;
 if(!ranges||!n||n>4096) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Choose from 1 to 4096 nonoverlapping ranges.");return NULL; }
 gsize len=strlen(text),cursor=0;GString *out=g_string_new(NULL);InsightRenumberPlan *plan=g_new0(InsightRenumberPlan,1);plan->ranges=g_array_sized_new(FALSE,FALSE,sizeof(InsightRenumberRange),n);
 for(guint i=0;i<n;i++) {
  InsightRenumberRange r=ranges[i];
  if(r.begin<cursor||r.end<r.begin||r.end>len||(text[r.begin]&&((guchar)text[r.begin]&0xc0)==0x80)||(text[r.end]&&((guchar)text[r.end]&0xc0)==0x80)||(r.begin&&text[r.begin-1]=='\r'&&text[r.begin]=='\n')||(r.end&&text[r.end-1]=='\r'&&text[r.end]=='\n')) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Ranges must be ordered, nonoverlapping and on UTF-8/line-ending boundaries.");goto failed; }
  gchar *part=g_strndup(text+r.begin,r.end-r.begin);r.count=0;gchar *changed=transform(part,mode,radix,&sequence,&r.count,error);g_free(part);if(!changed) goto failed;
  g_string_append_len(out,text+cursor,r.begin-cursor);r.after_begin=out->len;g_string_append(out,changed);g_free(changed);r.after_end=out->len;g_array_append_val(plan->ranges,r);cursor=r.end;
  if(out->len>LIMIT) goto large;
 }
 g_string_append(out,text+cursor);if(out->len>LIMIT) goto large;plan->text=g_string_free(out,FALSE);plan->count=sequence.seen;return plan;
large:g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_FAILED,"Renumber exceeds the signed 64-bit or 8 MiB limit");
failed:g_string_free(out,TRUE);insight_renumber_plan_free(plan);return NULL;
}
