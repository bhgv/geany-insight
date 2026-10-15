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
/* Tree depth is zero for an immediate child of the query root. */
static inline InsightGraphRect insight_graph_tree_rect(int index,int depth,int vertical) {
 InsightGraphRect r=insight_graph_rect(index,vertical);if(index>=0 && depth>0) { if(vertical) r.y+=depth*80;else r.x+=depth*240; }return r;
}
static inline int insight_graph_extent_image_size(InsightGraphRect r,double scale,int *width,int *height) {
 if(!width || !height || !(scale>=0.5 && scale<=2.0) || !(r.x>=0 && r.y>=0 && r.width>0 && r.height>0)) return 0;
 double raw_width=(r.x+r.width+16)*scale,raw_height=(r.y+r.height+16)*scale;
 if(!(raw_width<=16384 && raw_height<=16384)) return 0;
 int w=(int)(raw_width+0.999),h=(int)(raw_height+0.999);if((double)w*h>32000000) return 0;
 *width=w;*height=h;return 1;
}
/* Bound exported raster allocation before creating a Cairo surface. */
static inline int insight_graph_image_size(int count,int vertical,double scale,int *width,int *height) {
 if(!width || !height || count<0 || !(scale>=0.5 && scale<=2.0)) return 0;
 if(count>INSIGHT_GRAPH_LIMIT) count=INSIGHT_GRAPH_LIMIT;
 InsightGraphRect r=insight_graph_rect(count ? count-1 : -1,vertical);
 return insight_graph_extent_image_size(r,scale,width,height);
}
#endif
