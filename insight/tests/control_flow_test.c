/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../core/control_flow.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static InsightFlow g;
static int build(const char *s) {return insight_control_flow(s,strlen(s),0,strlen(s),&g);}
static unsigned kind(InsightFlowKind k) {unsigned n=0;for(unsigned i=0;i<g.count;i++) n+=g.nodes[i].kind==k;return n;}
static unsigned edges(unsigned id,InsightFlowEdgeKind k) {unsigned n=0;for(unsigned i=0;i<g.edge_count;i++) n+=g.edges[i].from==id && g.edges[i].kind==k;return n;}
static void has(unsigned a,unsigned b,InsightFlowEdgeKind kind) {unsigned n=0;for(unsigned i=0;i<g.edge_count;i++) n+=g.edges[i].from==a && g.edges[i].to==b && g.edges[i].kind==kind;assert(n==1);}
int main(void) {
 assert(build("{ if (x) a(); else if(y) b(); else c(); tail(); }"));assert(kind(INSIGHT_FLOW_IF)==2);assert(edges(2,INSIGHT_FLOW_YES)==1 && edges(2,INSIGHT_FLOW_NO)==1);
 assert(build("{ while(x) { if(y) break; if(z) continue; tick(); } return 1; }"));assert(kind(INSIGHT_FLOW_LOOP)==1);assert(kind(INSIGHT_FLOW_BREAK)==1);assert(kind(INSIGHT_FLOW_CONTINUE)==1);
 for(unsigned i=0;i<g.edge_count;i++) assert(g.edges[i].from<g.count && g.edges[i].to<g.count);
 assert(build("{ for(i=0;i<3;i++) ; after(); }"));assert(edges(2,INSIGHT_FLOW_YES)==1);
 assert(build("{ if(x) if(y) a(); else b(); return 0; }"));assert(kind(INSIGHT_FLOW_IF)==2);
 assert(build("{}") && g.count==2 && g.edge_count==1);
 assert(!build("{break;}"));assert(g.count==0);assert(!build("{goto a;}"));assert(!build("{switch(x) {case 1: a();}}"));assert(!build("{if(x) a() }"));assert(!build("{while(x) {"));
 assert(build("{if(x) a(); else b(); tail();}"));has(0,2,INSIGHT_FLOW_NEXT);has(2,3,INSIGHT_FLOW_YES);has(2,4,INSIGHT_FLOW_NO);has(3,5,INSIGHT_FLOW_NEXT);has(4,5,INSIGHT_FLOW_NEXT);has(5,1,INSIGHT_FLOW_NEXT);
 assert(build("{while(x){if(y)break;continue;}tail();}"));has(2,3,INSIGHT_FLOW_YES);has(2,6,INSIGHT_FLOW_NO);has(3,4,INSIGHT_FLOW_YES);has(3,5,INSIGHT_FLOW_NO);has(4,6,INSIGHT_FLOW_NEXT);has(5,2,INSIGHT_FLOW_BACK);
 assert(build("{if(x) if(y) a(); else b(); tail();}"));has(2,3,INSIGHT_FLOW_YES);has(2,6,INSIGHT_FLOW_NO);has(3,4,INSIGHT_FLOW_YES);has(3,5,INSIGHT_FLOW_NO);
 assert(build("{return 1; dead();}"));has(0,2,INSIGHT_FLOW_NEXT);has(2,1,INSIGHT_FLOW_NEXT);has(3,1,INSIGHT_FLOW_NEXT);
 assert(build("{{a(); b();} c();}"));has(2,3,INSIGHT_FLOW_NEXT);has(3,4,INSIGHT_FLOW_NEXT);
 assert(build("{returnя(); after();}"));assert(kind(INSIGHT_FLOW_RETURN)==0);
 char deep[2048]="{";for(unsigned i=0;i<70;i++) strcat(deep,"if(x)");strcat(deep,"a();}");assert(!build(deep));assert(strstr(g.error,"nesting"));
 char many[4096]="{";for(unsigned i=0;i<600;i++) strcat(many,"a();");strcat(many,"}");assert(!build(many));assert(strstr(g.error,"512"));
 puts("Control flow: branches, dangling else, loops, break/continue, return, limits and unsupported syntax passed");return 0;
}
