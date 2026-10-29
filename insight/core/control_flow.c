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
static int word(Parser *p,const char *w) { size_t n=strlen(w);return p->p+n<=p->end && !memcmp(p->s+p->p,w,n) && (p->p+n==p->end || !(isalnum((unsigned char)p->s[p->p+n]) || (unsigned char)p->s[p->p+n]>=128 || p->s[p->p+n]=='_')); }
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
  unsigned paren=0,bracket=0;int assignment=0;
  while(p->p<p->end) {
   char c=p->s[p->p];
   if(!paren && !bracket && c==';') {p->p++;break;}
   if(!paren && !bracket && c=='}') return fail(p,"Missing statement terminator");
   if(c=='=' && (p->p+1==p->end || p->s[p->p+1]!='=') && (p->p==begin || !strchr("=!<>",p->s[p->p-1]))) assignment=1;
   if(c=='{') {if(!assignment) return fail(p,"A nested definition or brace expression requires a full language parser");if(!balanced(p,'{','}')) return 0;continue;}
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
  else {unsigned target=kind==INSIGHT_FLOW_RETURN ? 1:kind==INSIGHT_FLOW_BREAK ? stop:kind==INSIGHT_FLOW_CONTINUE ? again:next;edge(p,i,target,target!=NONE && target==again ? INSIGHT_FLOW_BACK:INSIGHT_FLOW_NEXT);}
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

static int reaches(const InsightFlow *g,const int *head,const int *next,unsigned start,unsigned target,unsigned blocked) {
 unsigned queue[INSIGHT_FLOW_LIMIT],read=0,count=0;unsigned char seen[INSIGHT_FLOW_LIMIT]={0};if(start==blocked) return 0;queue[count++]=start;seen[start]=1;
 while(read<count) {unsigned id=queue[read++];if(id==target) return 1;for(int i=head[id];i>=0;i=next[i]) {unsigned to=g->edges[i].to;if(to!=blocked && !seen[to]) {seen[to]=1;queue[count++]=to;}}}return 0;
}
int insight_control_guards(const InsightFlow *g,size_t offset,InsightFlowEdge *out,unsigned capacity) {
 if(!g || !out || g->count>INSIGHT_FLOW_LIMIT || g->edge_count>INSIGHT_FLOW_EDGE_LIMIT) return -1;
 unsigned target=NONE;for(unsigned i=2;i<g->count;i++) if(g->nodes[i].begin<=offset && offset<g->nodes[i].end) {target=i;break;}if(target==NONE) return -1;
 int head[INSIGHT_FLOW_LIMIT],next[INSIGHT_FLOW_EDGE_LIMIT],parent[INSIGHT_FLOW_LIMIT];for(unsigned i=0;i<g->count;i++) head[i]=parent[i]=-1;
 for(unsigned i=0;i<g->edge_count;i++) {InsightFlowEdge e=g->edges[i];if(e.from>=g->count || e.to>=g->count) return -1;next[i]=head[e.from];head[e.from]=(int)i;}
 unsigned queue[INSIGHT_FLOW_LIMIT],read=0,count=1;queue[0]=0;parent[0]=-2;
 while(read<count && parent[target]==-1) {unsigned id=queue[read++];for(int i=head[id];i>=0;i=next[i]) {unsigned to=g->edges[i].to;if(parent[to]==-1) {parent[to]=i;queue[count++]=to;}}}
 if(parent[target]==-1) return -1;unsigned reverse[INSIGHT_FLOW_LIMIT],n=0;for(unsigned id=target;id;id=g->edges[parent[id]].from) reverse[n++]=(unsigned)parent[id];
 unsigned written=0;while(n) {InsightFlowEdge chosen=g->edges[reverse[--n]];if(chosen.kind!=INSIGHT_FLOW_YES && chosen.kind!=INSIGHT_FLOW_NO) continue;int needed=1;for(int i=head[chosen.from];i>=0;i=next[i]) if(g->edges[i].kind!=chosen.kind && reaches(g,head,next,g->edges[i].to,target,chosen.from)) {needed=0;break;}
  if(needed) {if(written==capacity) return -1;out[written++]=chosen;}}
 return (int)written;
}
