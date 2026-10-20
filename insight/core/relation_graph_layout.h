/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_GRAPH_LAYOUT_H
#define INSIGHT_RELATION_GRAPH_LAYOUT_H
#define INSIGHT_GRAPH_LIMIT 128
#define INSIGHT_GRAPH_MIN_ZOOM 5
#define INSIGHT_GRAPH_MAX_ZOOM 200
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
 if(!width || !height || !(scale>=0.05 && scale<=2.0) || !(r.x>=0 && r.y>=0 && r.width>0 && r.height>0)) return 0;
 double raw_width=(r.x+r.width+16)*scale,raw_height=(r.y+r.height+16)*scale;
 if(!(raw_width<=16384 && raw_height<=16384)) return 0;
 int w=(int)(raw_width+0.999),h=(int)(raw_height+0.999);if((double)w*h>32000000) return 0;
 *width=w;*height=h;return 1;
}
/* Fit only the currently drawn bounded graph. Floor the percentage so the
 * requested content fits, subject to the explicit minimum zoom. */
static inline int insight_graph_fit_zoom(InsightGraphRect r,double width,double height) {
 double w=r.x+r.width+16,h=r.y+r.height+16;
 if(!(width>0 && height>0 && w>0 && h>0)) return 100;
 double x=width/w*100,y=height/h*100,fit=x<y ? x : y;
 if(!(fit>=0)) return 100;
 if(fit<INSIGHT_GRAPH_MIN_ZOOM) return INSIGHT_GRAPH_MIN_ZOOM;
 if(fit>INSIGHT_GRAPH_MAX_ZOOM) return INSIGHT_GRAPH_MAX_ZOOM;
 return (int)fit;
}
static inline double insight_graph_scroll_value(double value,double upper,double page) {
 double maximum=upper>page ? upper-page : 0;
 if(!(value>=0)) return 0;
 return value>maximum ? maximum : value;
}
/* Keep the logical point beneath a viewport coordinate fixed across zoom. */
static inline double insight_graph_zoom_scroll(double old_value,double viewport_point,double old_scale,double new_scale,double upper,double page) {
 if(!(old_scale>0 && new_scale>0)) return 0;
 return insight_graph_scroll_value((old_value+viewport_point)/old_scale*new_scale-viewport_point,upper,page);
}
/* Bound exported raster allocation before creating a Cairo surface. */
static inline int insight_graph_image_size(int count,int vertical,double scale,int *width,int *height) {
 if(!width || !height || count<0 || !(scale>=0.05 && scale<=2.0)) return 0;
 if(count>INSIGHT_GRAPH_LIMIT) count=INSIGHT_GRAPH_LIMIT;
 InsightGraphRect r=insight_graph_rect(count ? count-1 : -1,vertical);
 return insight_graph_extent_image_size(r,scale,width,height);
}
#endif
