/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "replace_files.h"
#include <string.h>
static void hit_free(gpointer p) { InsightReplaceHit *h=p;g_free(h->replacement);g_free(h); }
void insight_replace_plan_free(InsightReplacePlan *p) { if(p) { g_free(p->before);g_ptr_array_unref(p->hits);g_free(p); } }
static gboolean word(gunichar c) { return g_unichar_isalnum(c) || g_unichar_ismark(c) || c=='_'; }
static gchar *preserve(const gchar *old,const gchar *replacement) {
 guint upper=0,lower=0,cased=0;gboolean initial=TRUE;
 for(const gchar *p=old;*p;p=g_utf8_next_char(p)) { gunichar c=g_utf8_get_char(p);gboolean up=g_unichar_isupper(c)||g_unichar_istitle(c),down=g_unichar_islower(c);if(up||down) { if((cased==0&&!up)||(cased>0&&!down)) initial=FALSE;cased++;upper+=up;lower+=down; } }
 if(upper&&!lower) return g_utf8_strup(replacement,-1);if(lower&&!upper) return g_utf8_strdown(replacement,-1);
 if(initial&&cased) { gchar *low=g_utf8_strdown(replacement,-1);for(gchar *p=low;*p;p=g_utf8_next_char(p)) if(g_unichar_islower(g_utf8_get_char(p))||g_unichar_isupper(g_utf8_get_char(p))) { gchar *next=g_utf8_next_char(p),*up=g_utf8_strup(p,next-p),*prefix=g_strndup(low,p-low),*out=g_strconcat(prefix,up,next,NULL);g_free(up);g_free(prefix);g_free(low);return out; }return low; }return g_strdup(replacement);
}
/* Replacement escapes match the comparison window: \1, \g<name>, \n, \r,
 * \t and \\. Validate captures even when the pattern has no matches. */
static gchar *expand(GRegex *regex,GMatchInfo *info,const gchar *replacement,GError **error) {
 GString *s=g_string_new(NULL);gint captures=g_regex_get_capture_count(regex);
 for(const gchar *p=replacement;*p;) {
  if(*p!='\\') g_string_append_c(s,*p++);
  else { p++;if(!*p) goto invalid;
   if(*p=='n'||*p=='r'||*p=='t'||*p=='\\') { g_string_append_c(s,*p=='n' ? '\n':*p=='r' ? '\r':*p=='t' ? '\t':'\\');p++; }
   else { gint group=-1;
    if(g_ascii_isdigit(*p)) { group=*p++-'0';if(!group&&g_ascii_isdigit(*p)) goto invalid;if(group&&g_ascii_isdigit(*p)) group=10*group+(*p++-'0'); }
    else if(*p=='g'&&p[1]=='<') { p+=2;const gchar *end=strchr(p,'>');if(!end||end==p||end-p>128) goto invalid;gchar *name=g_strndup(p,end-p);gboolean numeric=TRUE;for(const gchar *q=name;*q;q++) if(!g_ascii_isdigit(*q)) numeric=FALSE;group=numeric&&strlen(name)<6 ? (gint)g_ascii_strtoull(name,NULL,10):numeric ? -1:g_regex_get_string_number(regex,name);g_free(name);p=end+1; }
    else goto invalid;
    if(group<0||group>captures) goto invalid;if(info) { gchar *part=g_match_info_fetch(info,group);if(part) { if(!g_utf8_validate(part,-1,NULL)) { g_free(part);goto invalid; }g_string_append(s,part);g_free(part); } }
   }
  }
  if(s->len>INSIGHT_REPLACE_FILE_LIMIT) goto invalid;
 }
 return g_string_free(s,FALSE);
invalid:g_string_free(s,TRUE);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid replacement escape/capture, partial UTF-8 capture or oversized expansion.");return NULL;
}
InsightReplacePlan *insight_replace_plan(const gchar *text,const gchar *query,const gchar *replacement,const InsightReplaceOptions *o,GCancellable *cancel,GError **error) {
 if(cancel&&g_cancellable_set_error_if_cancelled(cancel,error)) return NULL;
 if(!o||o->region<INSIGHT_REGION_ALL||o->region>INSIGHT_REGION_COMMENTS||o->dialect<INSIGHT_CODE_C||o->dialect>INSIGHT_CODE_SHELL||!text||!query||!*query||!replacement||strlen(text)>INSIGHT_REPLACE_FILE_LIMIT||strlen(query)>8192||strlen(replacement)>8192||!g_utf8_validate(text,-1,NULL)||!g_utf8_validate(query,-1,NULL)||!g_utf8_validate(replacement,-1,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Use UTF-8 text up to 1 MiB and nonempty patterns/replacements up to 8 KiB (replacement may be empty).");return NULL; }
 gchar *pattern=o->regex ? g_strdup(query):g_regex_escape_string(query,-1),*limited=g_strconcat("(*LIMIT_MATCH=100000)(*LIMIT_RECURSION=256)",pattern,NULL);g_free(pattern);
 GRegexCompileFlags flags=G_REGEX_MULTILINE|G_REGEX_NEWLINE_ANYCRLF;if(!o->case_sensitive) flags|=G_REGEX_CASELESS;if(o->regex&&o->dotall) flags|=G_REGEX_DOTALL;
 GRegex *regex=g_regex_new(limited,flags,0,error);g_free(limited);if(!regex) return NULL;
 if(o->regex) { gchar *test=expand(regex,NULL,replacement,error);if(!test) { g_regex_unref(regex);return NULL; }g_free(test); }
 guchar *region=NULL;if(o->region!=INSIGHT_REGION_ALL) { region=insight_source_region_membership(text,strlen(text),o->dialect,o->region,error);if(!region) { g_regex_unref(regex);return NULL; } }
 InsightReplacePlan *plan=g_new0(InsightReplacePlan,1);plan->before=g_strdup(text);plan->hits=g_ptr_array_new_with_free_func(hit_free);GMatchInfo *info=NULL;gsize len=strlen(text),cursor=0;guint line=1,column=1,previous_line=0,scanned=0;guint64 expanded=0;
 GError *match_error=NULL;gboolean found=g_regex_match_full(regex,text,len,0,0,&info,&match_error);gboolean failed=match_error!=NULL;
 while(found&&!failed) {
  if(cancel&&g_cancellable_set_error_if_cancelled(cancel,error)) { failed=TRUE;break; }
  gint a,b;g_match_info_fetch_pos(info,0,&a,&b);if(++scanned>100000 || a<0 || b<a || (gsize)b>len || (text[a]&&((guchar)text[a]&0xc0)==0x80) || (text[b]&&((guchar)text[b]&0xc0)==0x80)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Match limit exceeded or expression splits a UTF-8 character.");failed=TRUE;break; }
  while(cursor<(gsize)a) { if(text[cursor]=='\r') { line++;column=1; }else if(text[cursor]=='\n') { if(!cursor||text[cursor-1]!='\r') line++;column=1; }else if(((guchar)text[cursor]&0xc0)!=0x80) column++;cursor++; }
  const gchar *before=a ? g_utf8_find_prev_char(text,text+a):NULL;gboolean accept=!(a==b&&a>0&&text[a-1]=='\r'&&text[a]=='\n');
  if(o->whole&&((before&&word(g_utf8_get_char(before)))||(text[b]&&word(g_utf8_get_char(text+b))))) accept=FALSE;
  if(region) { if(a==b) { if((a>0&&!region[a-1])||((gsize)a<len&&!region[a])) accept=FALSE; }else for(gint i=a;i<b;i++) if(!region[i]) { accept=FALSE;break; } }
  if(o->first_per_line&&line==previous_line) accept=FALSE;
  if(accept) {
   gchar *value=o->regex ? expand(regex,info,replacement,error):g_strdup(replacement);if(!value) { failed=TRUE;break; }
   if(o->preserve_case) { gchar *old=g_strndup(text+a,b-a),*cased=preserve(old,value);g_free(old);g_free(value);value=cased; }
   expanded+=strlen(value);if(plan->hits->len>=10000 || expanded>8u*1024u*1024u) { g_free(value);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Per-file limit: 10,000 matches and 8 MiB expanded replacements.");failed=TRUE;break; }
   InsightReplaceHit *h=g_new0(InsightReplaceHit,1);h->begin=a;h->end=b;h->line=line;h->column=column;h->replacement=value;h->selected=TRUE;g_ptr_array_add(plan->hits,h);previous_line=line;
  }
  found=g_match_info_next(info,&match_error);if(match_error) failed=TRUE;
 }
 if(info) g_match_info_free(info);g_regex_unref(regex);g_free(region);if(match_error) g_propagate_error(error,match_error);if(failed) { insight_replace_plan_free(plan);return NULL; }
 gchar *after=insight_replace_render(plan,NULL,error);if(!after) { insight_replace_plan_free(plan);return NULL; }g_free(after);return plan;
}
gchar *insight_replace_render(InsightReplacePlan *p,guint *count,GError **error) {
 GString *s=g_string_new(NULL);gsize cursor=0;guint n=0;
 for(guint i=0;i<p->hits->len;i++) { InsightReplaceHit *h=g_ptr_array_index(p->hits,i);if(!h->selected) continue;g_string_append_len(s,p->before+cursor,h->begin-cursor);h->after=s->len;g_string_append(s,h->replacement);cursor=h->end;n++;if(s->len>INSIGHT_REPLACE_FILE_LIMIT) goto large; }
 g_string_append(s,p->before+cursor);if(s->len>INSIGHT_REPLACE_FILE_LIMIT) goto large;if(count) *count=n;return g_string_free(s,FALSE);
large:g_string_free(s,TRUE);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Replacement Result exceeds 1 MiB. Nothing changed.");return NULL;
}
gchar *insight_replace_read(const gchar *path,GCancellable *cancel,GError **error) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE,G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,cancel,error);if(!info) { g_object_unref(file);return NULL; }gboolean regular=g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR;g_object_unref(info);if(!regular) { g_object_unref(file);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_REGULAR_FILE,"Only regular local source files are supported.");return NULL; }
 GFileInputStream *in=g_file_read(file,cancel,error);g_object_unref(file);if(!in) return NULL;gchar *data=g_malloc(INSIGHT_REPLACE_FILE_LIMIT+5);gsize n=0;gboolean ok=g_input_stream_read_all(G_INPUT_STREAM(in),data,INSIGHT_REPLACE_FILE_LIMIT+4,&n,cancel,error);g_object_unref(in);data[n]=0;
 if(ok&&n>=3&&!memcmp(data,"\357\273\277",3)) { memmove(data,data+3,n-3);n-=3;data[n]=0; }
 if(ok&&(n>INSIGHT_REPLACE_FILE_LIMIT||memchr(data,0,n)||!g_utf8_validate(data,n,NULL))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Use UTF-8 source up to 1 MiB without NUL bytes.");ok=FALSE; }if(!ok) g_clear_pointer(&data,g_free);return data;
}
gboolean insight_replace_mask(const gchar *path,const gchar *masks,GError **error) {
 if(!path||!masks||strlen(masks)>4096||!g_utf8_validate(masks,-1,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid filename masks (up to 4 KiB).");return FALSE; }
 gchar **items=g_strsplit_set(masks,";",-1),*base=g_path_get_basename(path);gboolean any=FALSE,matched=FALSE;for(guint i=0;items[i];i++) { gchar *s=g_strstrip(items[i]);if(!*s) continue;any=TRUE;
#ifdef G_OS_WIN32
  gchar *fold=g_utf8_casefold(s,-1),*name=g_utf8_casefold(base,-1);matched|=g_pattern_match_simple(fold,name);g_free(fold);g_free(name);
#else
  matched|=g_pattern_match_simple(s,base);
#endif
 }g_strfreev(items);g_free(base);return !any||matched;
}
