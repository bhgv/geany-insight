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
/* Bound exported raster allocation before creating a Cairo surface. */
static inline int insight_graph_image_size(int count,int vertical,double scale,int *width,int *height) {
 if(!width || !height || count<0 || !(scale>=0.5 && scale<=2.0)) return 0;
 if(count>INSIGHT_GRAPH_LIMIT) count=INSIGHT_GRAPH_LIMIT;
 InsightGraphRect r=insight_graph_rect(count ? count-1 : -1,vertical);
 int w=(int)((r.x+r.width+16)*scale+0.999),h=(int)((r.y+r.height+16)*scale+0.999);
 if(w>16384 || h>16384 || (double)w*h>32000000) return 0;
 *width=w;*height=h;return 1;
}
#endif
