/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "control_flow.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#define NONE ((unsigned)-1)
typedef struct { unsigned next,yes,no; } Branch;
typedef struct { const char *s;size_t p,end;InsightFlow *g;Branch branches[INSIGHT_FLOW_LIMIT];unsigned depth; } Parser;
typedef struct { unsigned first,last; } Sequence;
static int fail(Parser *p,const char *why) { if(!*p->g->error) snprintf(p->g->error,sizeof(p->g->error),"%s",why);return 0; }
static void space(Parser *p) { while(p->p<p->end && isspace((unsigned char)p->s[p->p])) p->p++; }
static int word(Parser *p,const char *w) { size_t n=strlen(w);return p->p+n<=p->end && !memcmp(p->s+p->p,w,n) && (p->p+n==p->end || !(isalnum((unsigned char)p->s[p->p+n]) || p->s[p->p+n]=='_')); }
static unsigned node(Parser *p,InsightFlowKind kind,size_t begin,size_t end,unsigned lane) {
 if(p->g->count==INSIGHT_FLOW_LIMIT) {fail(p,"Control-flow graph exceeds 512 nodes");return NONE;}
 unsigned i=p->g->count++;p->g->nodes[i]=(InsightFlowNode){begin,end,kind,lane};p->branches[i]=(Branch){NONE,NONE,NONE};return i;
}
static int balanced(Parser *p,char open,char close) {
 if(p->p>=p->end || p->s[p->p]!=open) return fail(p,"Expected a balanced condition or block");
 unsigned depth=0;do {char c=p->s[p->p++];if(c==open) depth++;else if(c==close) depth--;if(depth>128) return fail(p,"Expression nesting exceeds 128 levels");}while(depth && p->p<p->end);
 return !depth || fail(p,"Unclosed condition or block");
}
static int statement(Parser *,Sequence *,unsigned);
static void append(Parser *p,Sequence *out,Sequence child) { if(child.first==NONE) return;if(out->first==NONE) *out=child;else {p->branches[out->last].next=child.first;out->last=child.last;} }
static int block(Parser *p,Sequence *out,unsigned lane) {
 if(p->s[p->p++]!='{') return fail(p,"Expected function body");
 while(1) {space(p);if(p->p==p->end) return fail(p,"Unclosed function block");if(p->s[p->p]=='}') {p->p++;return 1;}Sequence child={NONE,NONE};if(!statement(p,&child,lane)) return 0;append(p,out,child);}
}
static int statement(Parser *p,Sequence *out,unsigned lane) {
 if(++p->depth>64) return fail(p,"Statement nesting exceeds 64 levels");space(p);size_t begin=p->p;unsigned id=NONE;
 if(begin==p->end) return fail(p,"Missing statement");
 if(p->s[p->p]=='{') {int ok=block(p,out,lane);p->depth--;return ok;}
 if(p->s[p->p]==';') {p->p++;p->depth--;return 1;}
 if(p->s[p->p]=='#' || word(p,"switch") || word(p,"do") || word(p,"goto") || word(p,"try") || word(p,"catch") || word(p,"throw") || word(p,"else") || word(p,"case")) return fail(p,"Unsupported control syntax: switch/do/goto/exceptions/preprocessor");
 int is_if=word(p,"if"),is_loop=word(p,"while") || word(p,"for");
 if(is_if || is_loop) {
  p->p+=is_if ? 2:word(p,"while") ? 5:3;space(p);if(!balanced(p,'(',')')) return 0;
  id=node(p,is_if ? INSIGHT_FLOW_IF:INSIGHT_FLOW_LOOP,begin,p->p,lane);if(id==NONE) return 0;
  Sequence yes={NONE,NONE};if(!statement(p,&yes,lane+1)) return 0;p->branches[id].yes=yes.first;space(p);
  if(is_if && word(p,"else")) {p->p+=4;Sequence no={NONE,NONE};if(!statement(p,&no,lane+1)) return 0;p->branches[id].no=no.first;}
 } else {
  InsightFlowKind kind=word(p,"return") ? INSIGHT_FLOW_RETURN:word(p,"break") ? INSIGHT_FLOW_BREAK:word(p,"continue") ? INSIGHT_FLOW_CONTINUE:INSIGHT_FLOW_STEP;
  unsigned paren=0,bracket=0;
  while(p->p<p->end) {
   char c=p->s[p->p];
   if(!paren && !bracket && c==';') {p->p++;break;}
   if(!paren && !bracket && c=='}') return fail(p,"Missing statement terminator");
   if(c=='{') return fail(p,"Nested definitions/initializers/lambdas require a full language parser");
   if(c=='(') paren++;else if(c==')') {if(!paren) return fail(p,"Unmatched parenthesis");paren--;}
   if(c=='[') bracket++;else if(c==']') {if(!bracket) return fail(p,"Unmatched bracket");bracket--;}
   if(paren>128 || bracket>128) return fail(p,"Expression nesting exceeds 128 levels");
   if(c==':' && !paren && !bracket) return fail(p,"Labels and conditional expressions are not expanded");p->p++;
  }
  if(p->p==p->end && (p->p==begin || p->s[p->p-1]!=';')) return fail(p,"Missing statement terminator");
  id=node(p,kind,begin,p->p,lane);if(id==NONE) return 0;
 }
 *out=(Sequence){id,id};p->depth--;return 1;
}
static int edge(Parser *p,unsigned from,unsigned to,InsightFlowEdgeKind kind) {
 if(to==NONE) return fail(p,"break/continue outside a loop");
 if(p->g->edge_count==INSIGHT_FLOW_EDGE_LIMIT) return fail(p,"Control-flow graph exceeds edge limit");
 p->g->edges[p->g->edge_count++]=(InsightFlowEdge){from,to,kind};return 1;
}
static unsigned compile(Parser *p,unsigned first,unsigned next,unsigned stop,unsigned again) {
 unsigned order[INSIGHT_FLOW_LIMIT],n=0;for(unsigned i=first;i!=NONE;i=p->branches[i].next) order[n++]=i;
 while(n && !*p->g->error) {
  unsigned i=order[--n];InsightFlowKind kind=p->g->nodes[i].kind;Branch b=p->branches[i];
  if(kind==INSIGHT_FLOW_IF) {unsigned yes=compile(p,b.yes,next,stop,again),no=compile(p,b.no,next,stop,again);edge(p,i,yes,INSIGHT_FLOW_YES);edge(p,i,no,INSIGHT_FLOW_NO);}
  else if(kind==INSIGHT_FLOW_LOOP) {unsigned body=compile(p,b.yes,i,next,i);edge(p,i,body,INSIGHT_FLOW_YES);edge(p,i,next,INSIGHT_FLOW_NO);}
  else edge(p,i,kind==INSIGHT_FLOW_RETURN ? 1:kind==INSIGHT_FLOW_BREAK ? stop:kind==INSIGHT_FLOW_CONTINUE ? again:next,(kind==INSIGHT_FLOW_CONTINUE || next==again) ? INSIGHT_FLOW_BACK:INSIGHT_FLOW_NEXT);
  next=i;
 }
 return next;
}
int insight_control_flow(const char *mask,size_t length,size_t body,size_t end,InsightFlow *out) {
 if(!out) return 0;memset(out,0,sizeof(*out));Parser p={0};p.s=mask;p.p=body;p.end=end;p.g=out;
 if(!mask || length>2u*1024u*1024u || body>=end || end>length || mask[body]!='{') {fail(&p,"Expected a C-like function body up to 2 MiB");return 0;}
 node(&p,INSIGHT_FLOW_ENTRY,body,body+1,0);node(&p,INSIGHT_FLOW_EXIT,end-1,end,0);
 Sequence seq={NONE,NONE};int ok=block(&p,&seq,0);space(&p);if(ok && p.p!=end) ok=fail(&p,"Unexpected text after function body");
 if(ok) {unsigned first=compile(&p,seq.first,1,NONE,NONE);edge(&p,0,first,INSIGHT_FLOW_NEXT);ok=!*out->error;}
 if(!ok) out->count=out->edge_count=0;return ok;
}
