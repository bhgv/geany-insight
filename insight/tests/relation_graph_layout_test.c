/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../core/relation_graph_layout.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
 for(int vertical=0;vertical<2;vertical++) for(int i=0;i<INSIGHT_GRAPH_LIMIT;i++) {
  InsightGraphRect r=insight_graph_rect(i,vertical);
  assert(insight_graph_hit(r.x,r.y,128,vertical)==i);
  assert(insight_graph_hit(r.x+r.width-0.01,r.y+r.height-0.01,128,vertical)==i);
  assert(insight_graph_hit(r.x+r.width,r.y,128,vertical)==-1);
  assert(insight_graph_hit(r.x,r.y+r.height,128,vertical)==-1);
  assert(insight_graph_hit(r.x,r.y,i,vertical)==-1);
  for(int zoom=50;zoom<=200;zoom+=25) { double scale=zoom/100.0,x=(r.x+20)*scale,y=(r.y+20)*scale;assert(insight_graph_hit(x/scale,y/scale,128,vertical)==i); }
 }
 assert(insight_graph_hit(9,9,0,0)==-1);
 assert(insight_graph_hit(9,9,128,1)==-1);
 InsightGraphRect r=insight_graph_rect(128,0);assert(insight_graph_hit(r.x,r.y,10000,0)==-1);
 int width=0,height=0;
 assert(insight_graph_image_size(0,0,1,&width,&height));assert(width==224 && height==72);
 for(int count=0;count<=128;count++) for(int vertical=0;vertical<2;vertical++) for(int zoom=50;zoom<=200;zoom+=25) {
  if(insight_graph_image_size(count,vertical,zoom/100.0,&width,&height)) { assert(width>0 && height>0 && width<=16384 && height<=16384);assert((double)width*height<=32000000); }
 }
 assert(!insight_graph_image_size(-1,0,1,&width,&height));assert(!insight_graph_image_size(1,0,0,&width,&height));assert(!insight_graph_image_size(1,0,1,0,&height));
 assert(!insight_graph_image_size(128,1,2,&width,&height));assert(insight_graph_image_size(128,1,0.5,&width,&height));
 for(int vertical=0;vertical<2;vertical++) for(int depth=0;depth<=32;depth++) {
  InsightGraphRect tree=insight_graph_tree_rect(3,depth,vertical),flat=insight_graph_rect(3,vertical);
  assert(tree.x==flat.x+(vertical ? 0 : depth*240));assert(tree.y==flat.y+(vertical ? depth*80 : 0));
  assert(tree.width==200 && tree.height==48);
  assert(insight_graph_extent_image_size(tree,0.5,&width,&height));
 }
 assert(!insight_graph_extent_image_size((InsightGraphRect){-1,0,200,48},1,&width,&height));
 assert(!insight_graph_extent_image_size((InsightGraphRect){0,0,1e30,48},1,&width,&height));
 puts("Graph layout: orientations, zoom, gaps, bounds and node limit passed");return 0;
}
