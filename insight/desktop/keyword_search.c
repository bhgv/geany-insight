/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "keyword_search.h"
#include <string.h>
enum { TERM,AND,OR,NOT };
typedef struct { guint kind; gint left,right; gchar *text; guint flags,term; GArray *matches; gsize first,last; } Node;
typedef struct { const gchar *cursor; Node nodes[128]; guint count,terms,depth,flags; gboolean failed; } Parser;
static void space(Parser *p) { while(g_ascii_isspace(*p->cursor)) p->cursor++; }
static gboolean op(Parser *p,const char *word) {
 space(p); gsize n=strlen(word);
 if(g_ascii_strncasecmp(p->cursor,word,n) || (p->cursor[n] && !g_ascii_isspace(p->cursor[n]) && p->cursor[n]!=')' && p->cursor[n]!='(')) return FALSE;
 p->cursor+=n; return TRUE;
}
static gint node(Parser *p,guint kind,gint a,gint b) {
 if(p->count>=G_N_ELEMENTS(p->nodes)) { p->failed=TRUE; return -1; }
 gint index=(gint)p->count++; p->nodes[index]=(Node){.kind=kind,.left=a,.right=b}; return index;
}
static gint expression(Parser *);
static gboolean vowel(char c) { return strchr("aeiou",g_ascii_tolower(c))!=NULL; }
static void add_form(GPtrArray *forms,const char *stem,const char *suffix,gboolean upper) {
 gchar *ending=upper ? g_ascii_strup(suffix,-1) : g_strdup(suffix),*form=g_strconcat(stem,ending,NULL); g_free(ending);
 if(!g_ptr_array_find_with_equal_func(forms,form,(GEqualFunc)g_str_equal,NULL)) g_ptr_array_add(forms,form); else g_free(form);
}
static gint form_order(gconstpointer a,gconstpointer b) { const char *x=*(const char *const *)a,*y=*(const char *const *)b; gsize nx=strlen(x),ny=strlen(y); return nx==ny ? strcmp(x,y) : (nx>ny ? -1 : 1); }
/* Regular English endings for base keywords, not a dictionary stemmer. Keep
 * phrases, regex and non-ASCII identifiers literal. Prefer the longest form
 * so a whole inflection is highlighted rather than its shorter base prefix. */
static gchar *variation_pattern(const char *word) {
 gsize n=strlen(word); if(n<2 || n>128) return NULL; gboolean upper=TRUE; guint vowel_groups=0;
 for(gsize i=0;i<n;i++) { if(!g_ascii_isalpha(word[i])) return NULL; if(g_ascii_islower(word[i])) upper=FALSE; if(vowel(word[i]) && (!i || !vowel(word[i-1]))) vowel_groups++; }
 gchar *lower=g_ascii_strdown(word,-1); GPtrArray *forms=g_ptr_array_new_with_free_func(g_free); add_form(forms,word,"",upper);
 if(lower[n-1]=='y' && !vowel(lower[n-2])) { gchar *stem=g_strndup(word,n-1); add_form(forms,stem,"ies",upper); add_form(forms,stem,"ied",upper); add_form(forms,word,"ing",upper); g_free(stem); }
 else {
  gboolean es=lower[n-1]=='s' || lower[n-1]=='x' || lower[n-1]=='z' || g_str_has_suffix(lower,"ch") || g_str_has_suffix(lower,"sh"); add_form(forms,word,es ? "es" : "s",upper);
  if(lower[n-1]=='e' && lower[n-2]!='e' && !g_str_has_suffix(lower,"ie")) { gchar *stem=g_strndup(word,n-1); add_form(forms,word,"d",upper); add_form(forms,stem,"ing",upper); g_free(stem); }
  else if(g_str_has_suffix(lower,"ie")) { gchar *stem=g_strndup(word,n-2); add_form(forms,word,"d",upper); add_form(forms,stem,"ying",upper); g_free(stem); }
  else {
   gboolean doubled=n>=3 && vowel_groups==1 && !vowel(lower[n-1]) && !strchr("wxy",lower[n-1]) && vowel(lower[n-2]) && !vowel(lower[n-3]);
   gchar *stem=doubled ? g_strdup_printf("%s%c",word,word[n-1]) : g_strdup(word); add_form(forms,stem,"ed",upper); add_form(forms,stem,"ing",upper); g_free(stem);
  }
 }
 g_ptr_array_sort(forms,form_order); GString *pattern=g_string_new("(?:"); for(guint i=0;i<forms->len;i++) { if(i) g_string_append_c(pattern,'|'); g_string_append(pattern,g_ptr_array_index(forms,i)); } g_string_append_c(pattern,')');
 g_free(lower); g_ptr_array_free(forms,TRUE); return g_string_free(pattern,FALSE);
}
static gint atom(Parser *p) {
 space(p);
 if(++p->depth>16) { p->failed=TRUE; p->depth--; return -1; }
 gint result=-1;
 if(op(p,"NOT") || *p->cursor=='!' || *p->cursor=='-') {
  if(*p->cursor=='!' || *p->cursor=='-') p->cursor++;
  gint child=atom(p); result=node(p,NOT,child,-1);
 } else if(*p->cursor=='(') {
  p->cursor++; result=expression(p); space(p); if(*p->cursor!=')') p->failed=TRUE; else p->cursor++;
 } else {
  guint flags=p->flags&(INSIGHT_SEARCH_CASE|INSIGHT_SEARCH_WORD);
  if(*p->cursor=='=') { flags|=INSIGHT_SEARCH_CASE; p->cursor++; }
  if(*p->cursor=='?') { flags|=INSIGHT_SEARCH_REGEX; flags&=~INSIGHT_SEARCH_WORD; p->cursor++; space(p); }
  GString *term=g_string_new(NULL); gboolean quoted=*p->cursor=='"'; if(quoted) p->cursor++;
  while(*p->cursor && (quoted ? *p->cursor!='"' : (!g_ascii_isspace(*p->cursor) && *p->cursor!='(' && *p->cursor!=')'))) {
   if(quoted && *p->cursor=='\\' && (p->cursor[1]=='"' || p->cursor[1]=='\\')) p->cursor++;
   g_string_append_c(term,*p->cursor++);
  }
  if(quoted) { if(*p->cursor=='"') p->cursor++; else p->failed=TRUE; }
  if(!term->len || term->len>1024 || ++p->terms>32) p->failed=TRUE;
  result=node(p,TERM,-1,-1);
  if(result>=0) { p->nodes[result].text=g_string_free(term,FALSE); if((p->flags&INSIGHT_KEYWORD_VARIATIONS) && !(flags&INSIGHT_SEARCH_REGEX)) { gchar *pattern=variation_pattern(p->nodes[result].text); if(pattern) { g_free(p->nodes[result].text); p->nodes[result].text=pattern; flags|=INSIGHT_SEARCH_REGEX; } } p->nodes[result].flags=flags; p->nodes[result].term=p->terms-1; } else g_string_free(term,TRUE);
 }
 p->depth--; return result;
}
static gint conjunction(Parser *p) {
 gint left=atom(p);
 while(!p->failed) {
  space(p); if(!*p->cursor || *p->cursor==')') break;
  const char *before=p->cursor; if(op(p,"OR")) { p->cursor=before; break; }
  if(!op(p,"AND") && *p->cursor=='+') p->cursor++;
  gint right=atom(p); left=node(p,AND,left,right);
 }
 return left;
}
static gint expression(Parser *p) {
 gint left=conjunction(p);
 while(!p->failed && op(p,"OR")) { gint right=conjunction(p); left=node(p,OR,left,right); }
 return left;
}
typedef struct { gboolean matched; guint32 witnesses; } Proof;
static Proof evaluate(Parser *p,gint index,gboolean negated) {
 Node *n=&p->nodes[index];
 if(n->kind==TERM) { gboolean present=n->last>n->first,matched=present!=negated; return (Proof){matched,matched && !negated ? ((guint32)1<<n->term) : 0}; }
 if(n->kind==NOT) return evaluate(p,n->left,!negated);
 Proof a=evaluate(p,n->left,negated),b=evaluate(p,n->right,negated);
 gboolean conjunction=(n->kind==AND)!=negated,matched=conjunction ? a.matched && b.matched : a.matched || b.matched;
 return (Proof){matched,matched ? (a.matched ? a.witnesses : 0)|(b.matched ? b.witnesses : 0) : 0};
}
static gint compare(gconstpointer a,gconstpointer b) { const InsightSearchMatch *x=a,*y=b; if(x->start!=y->start) return x->start<y->start ? -1 : 1; return x->end==y->end ? 0 : (x->end<y->end ? -1 : 1); }
gboolean insight_keyword_collect(const char *text,gsize size,const char *query,guint flags,guint context,InsightSearchMatch *out,gsize capacity,gsize *total,GError **error) {
 if(total) *total=0;
 if(!text || !query || !total || flags>15 || (capacity && !out) || size>2u*1024u*1024u || context>50 || strlen(query)>4096 || !g_utf8_validate(text,(gssize)size,NULL) || memchr(text,0,size) || !g_utf8_validate(query,-1,NULL)) {
  g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Invalid keyword search input"); return FALSE;
 }
 *total=0; Parser p={.cursor=query,.flags=flags}; gint root=expression(&p); space(&p); gboolean ok=!p.failed && !*p.cursor && root>=0;
 GArray *starts=g_array_new(FALSE,FALSE,sizeof(gsize)),*results=g_array_new(FALSE,FALSE,sizeof(InsightSearchMatch)); guint *eligible[32]={NULL}; gsize zero=0; g_array_append_val(starts,zero);
 if(!ok) g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Invalid keyword expression (up to 32 terms, nesting 16)");
 for(gsize i=0;ok && i<size;i++) if(text[i]=='\n' || (text[i]=='\r' && (i+1==size || text[i+1]!='\n'))) { gsize next=i+1; g_array_append_val(starts,next); if(starts->len>100000) { ok=FALSE; g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Keyword search supports up to 100000 lines per file"); } }
 for(guint i=0;ok && i<p.count;i++) if(p.nodes[i].kind==TERM) {
  Node *n=&p.nodes[i]; gsize count=0;
  ok=insight_context_collect(text,size,n->text,n->flags,NULL,0,&count,error);
  if(ok && count>100000) { ok=FALSE; g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Too many keyword occurrences in one file"); }
  if(ok) { n->matches=g_array_sized_new(FALSE,FALSE,sizeof(InsightSearchMatch),(guint)count); g_array_set_size(n->matches,(guint)count); ok=insight_context_collect(text,size,n->text,n->flags,(InsightSearchMatch *)n->matches->data,count,&count,error); }
 }
 if(ok) {
  for(guint i=0;i<p.terms;i++) eligible[i]=g_new0(guint,starts->len+1);
  for(guint line=1;line<=starts->len;line++) {
   for(guint i=0;i<p.count;i++) if(p.nodes[i].kind==TERM) {
    Node *n=&p.nodes[i];
    while(n->first<n->matches->len && g_array_index(n->matches,InsightSearchMatch,n->first).line<line) n->first++;
    while(n->last<n->matches->len && g_array_index(n->matches,InsightSearchMatch,n->last).line<=line+context) n->last++;
   }
   Proof proof=evaluate(&p,root,FALSE);
   for(guint i=0;i<p.terms;i++) eligible[i][line]=eligible[i][line-1]+!!(proof.witnesses&((guint32)1<<i));
   if(proof.matched && !proof.witnesses) { gsize start=g_array_index(starts,gsize,line-1),end=line<starts->len ? g_array_index(starts,gsize,line) : size; while(end>start && (text[end-1]=='\r' || text[end-1]=='\n')) end--; InsightSearchMatch match={start,end,line,1}; g_array_append_val(results,match); }
  }
  for(guint i=0;i<p.count;i++) if(p.nodes[i].kind==TERM) for(guint j=0;j<p.nodes[i].matches->len;j++) {
   InsightSearchMatch match=g_array_index(p.nodes[i].matches,InsightSearchMatch,j); guint line=(guint)match.line,first=line>context ? line-context : 1;
   const guint *windows=eligible[p.nodes[i].term]; if(windows[line]>windows[first-1]) g_array_append_val(results,match);
  }
  g_array_sort(results,compare); InsightSearchMatch previous={G_MAXSIZE,G_MAXSIZE,0,0};
  for(guint i=0;i<results->len;i++) { InsightSearchMatch match=g_array_index(results,InsightSearchMatch,i); if(match.start==previous.start && match.end==previous.end) continue; if(*total<capacity) out[*total]=match; (*total)++; previous=match; }
 }
 for(guint i=0;i<p.count;i++) { g_free(p.nodes[i].text); if(p.nodes[i].matches) g_array_free(p.nodes[i].matches,TRUE); }
 for(guint i=0;i<G_N_ELEMENTS(eligible);i++) g_free(eligible[i]);
 g_array_free(starts,TRUE); g_array_free(results,TRUE); return ok;
}
