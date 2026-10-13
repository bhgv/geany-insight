/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "keyword_search.h"
#include <string.h>
enum { TERM,AND,OR,NOT };
typedef struct { guint kind; gint left,right; gchar *text; guint flags; gboolean positive; GArray *matches; gsize first,last; } Node;
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
  if(result>=0) { p->nodes[result].text=g_string_free(term,FALSE); p->nodes[result].flags=flags; } else g_string_free(term,TRUE);
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
static void polarity(Parser *p,gint index,gboolean positive) {
 Node *n=&p->nodes[index];
 if(n->kind==TERM) n->positive|=positive;
 else { polarity(p,n->left,n->kind==NOT ? !positive : positive); if(n->kind!=NOT) polarity(p,n->right,positive); }
}
static gboolean evaluate(Parser *p,gint index) {
 Node *n=&p->nodes[index];
 switch(n->kind) { case TERM:return n->last>n->first; case AND:return evaluate(p,n->left)&&evaluate(p,n->right); case OR:return evaluate(p,n->left)||evaluate(p,n->right); default:return !evaluate(p,n->left); }
}
static gint compare(gconstpointer a,gconstpointer b) { const InsightSearchMatch *x=a,*y=b; if(x->start!=y->start) return x->start<y->start ? -1 : 1; return x->end==y->end ? 0 : (x->end<y->end ? -1 : 1); }
gboolean insight_keyword_collect(const char *text,gsize size,const char *query,guint flags,guint context,InsightSearchMatch *out,gsize capacity,gsize *total,GError **error) {
 if(!text || !query || !total || (capacity && !out) || size>2u*1024u*1024u || context>50 || strlen(query)>4096 || !g_utf8_validate(text,(gssize)size,NULL) || memchr(text,0,size) || !g_utf8_validate(query,-1,NULL)) {
  g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Invalid keyword search input"); return FALSE;
 }
 *total=0; Parser p={.cursor=query,.flags=flags}; gint root=expression(&p); space(&p); gboolean ok=!p.failed && !*p.cursor && root>=0;
 GArray *starts=g_array_new(FALSE,FALSE,sizeof(gsize)),*results=g_array_new(FALSE,FALSE,sizeof(InsightSearchMatch)); guint *good=NULL; gsize zero=0; g_array_append_val(starts,zero);
 if(!ok) g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Invalid keyword expression (up to 32 terms, nesting 16)");
 for(gsize i=0;ok && i<size;i++) if(text[i]=='\n' || (text[i]=='\r' && (i+1==size || text[i+1]!='\n'))) { gsize next=i+1; g_array_append_val(starts,next); if(starts->len>100000) { ok=FALSE; g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Keyword search supports up to 100000 lines per file"); } }
 gboolean positive=FALSE;
 if(ok) polarity(&p,root,TRUE);
 for(guint i=0;ok && i<p.count;i++) if(p.nodes[i].kind==TERM) {
  Node *n=&p.nodes[i]; gsize count=0;
  ok=insight_context_collect(text,size,n->text,n->flags,NULL,0,&count,error);
  if(ok && count>100000) { ok=FALSE; g_set_error_literal(error,G_KEY_FILE_ERROR,G_KEY_FILE_ERROR_INVALID_VALUE,"Too many keyword occurrences in one file"); }
  if(ok) { n->matches=g_array_sized_new(FALSE,FALSE,sizeof(InsightSearchMatch),(guint)count); g_array_set_size(n->matches,(guint)count); ok=insight_context_collect(text,size,n->text,n->flags,(InsightSearchMatch *)n->matches->data,count,&count,error); positive|=n->positive; }
 }
 if(ok) {
  good=g_new0(guint,starts->len+1);
  for(guint line=1;line<=starts->len;line++) {
   for(guint i=0;i<p.count;i++) if(p.nodes[i].kind==TERM) {
    Node *n=&p.nodes[i];
    while(n->first<n->matches->len && g_array_index(n->matches,InsightSearchMatch,n->first).line<line) n->first++;
    while(n->last<n->matches->len && g_array_index(n->matches,InsightSearchMatch,n->last).line<=line+context) n->last++;
   }
   gboolean matched=evaluate(&p,root); good[line]=good[line-1]+matched;
   if(matched && !positive) { gsize start=g_array_index(starts,gsize,line-1),end=line<starts->len ? g_array_index(starts,gsize,line) : size; while(end>start && (text[end-1]=='\r' || text[end-1]=='\n')) end--; InsightSearchMatch match={start,end,line,1}; g_array_append_val(results,match); }
  }
  if(positive) for(guint i=0;i<p.count;i++) if(p.nodes[i].kind==TERM && p.nodes[i].positive) for(guint j=0;j<p.nodes[i].matches->len;j++) {
   InsightSearchMatch match=g_array_index(p.nodes[i].matches,InsightSearchMatch,j); guint line=(guint)match.line,first=line>context ? line-context : 1;
   if(good[line]>good[first-1]) g_array_append_val(results,match);
  }
  g_array_sort(results,compare); InsightSearchMatch previous={G_MAXSIZE,G_MAXSIZE,0,0};
  for(guint i=0;i<results->len;i++) { InsightSearchMatch match=g_array_index(results,InsightSearchMatch,i); if(match.start==previous.start && match.end==previous.end) continue; if(*total<capacity) out[*total]=match; (*total)++; previous=match; }
 }
 for(guint i=0;i<p.count;i++) { g_free(p.nodes[i].text); if(p.nodes[i].matches) g_array_free(p.nodes[i].matches,TRUE); }
 g_free(good); g_array_free(starts,TRUE); g_array_free(results,TRUE); return ok;
}
