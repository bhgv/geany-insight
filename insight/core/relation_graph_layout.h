/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_GRAPH_LAYOUT_H
#define INSIGHT_RELATION_GRAPH_LAYOUT_H
#define INSIGHT_GRAPH_LIMIT 128
/* Logical pixels, before zoom. Root uses index -1; results use 0..count-1. */
typedef struct { double x,y,width,height; } InsightGraphRect;
static inline InsightGraphRect insight_graph_rect(int index,int vertical) {
 InsightGraphRect r={8,8,200,48};
 if(index>=0) { if(vertical) { r.x+=index*220;r.y=96; } else { r.x=248;r.y+=index*64; } }
 return r;
}
static inline int insight_graph_hit(double x,double y,int count,int vertical) {
 if(count<0) return -1;
 if(count>INSIGHT_GRAPH_LIMIT) count=INSIGHT_GRAPH_LIMIT;
 for(int i=0;i<count;i++) { InsightGraphRect r=insight_graph_rect(i,vertical);if(x>=r.x && x<r.x+r.width && y>=r.y && y<r.y+r.height) return i; }
 return -1;
}
#endif
