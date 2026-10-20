/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../core/relation_graph_layout.h"
#include <assert.h>
#include "../core/relation_graph_print.h"
#include <stdio.h>
#include <math.h>
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
 InsightGraphRect fit={8,8,440,2560};
 assert(insight_graph_fit_zoom(fit,800,400)==15);
 assert(insight_graph_fit_zoom(fit,1,1)==5);
 assert(insight_graph_fit_zoom(fit,100000,100000)==200);
 assert(insight_graph_fit_zoom(fit,0,400)==100);
 assert(insight_graph_fit_zoom((InsightGraphRect){0,0,INFINITY,INFINITY},INFINITY,INFINITY)==100);
 assert(insight_graph_scroll_value(-4,1000,200)==0);
 assert(insight_graph_scroll_value(900,1000,200)==800);
 assert(insight_graph_scroll_value(5,100,200)==0);
 for(int old=5;old<=200;old+=5) for(int zoom=5;zoom<=200;zoom+=5) {
  double offset=insight_graph_zoom_scroll(500,120,old/100.0,zoom/100.0,1000000,300);
  double expected=(500+120)/(old/100.0)*(zoom/100.0)-120;
  if(expected<0) expected=0;
  assert(fabs(offset-expected)<0.0001);
 }
 assert(insight_graph_image_size(128,0,0.05,&width,&height));
 assert(!insight_graph_image_size(1,0,0.049,&width,&height));
 InsightGraphPrintPlan plan;
 assert(insight_graph_print_plan(464,2576,540,780,0,&plan));assert(plan.pages==1);assert(plan.scale>0 && plan.scale<0.75);
 assert(plan.scale*2576<=plan.height && plan.scale*464<=plan.width);
 assert(insight_graph_print_plan(464,2576,540,780,1,&plan));assert(plan.pages==3 && plan.columns==1 && plan.rows==3 && plan.scale==0.75);
 assert(insight_graph_print_plan(1440,976,540,780,1,&plan));assert(plan.columns==2 && plan.rows==1 && plan.pages==2);
 assert(insight_graph_print_plan(1440,1952,540,780,1,&plan));assert(plan.columns==2 && plan.rows==2 && plan.pages==4);
 assert(insight_graph_print_plan(100,100,540,780,0,&plan));assert(plan.scale==0.75);
 assert(!insight_graph_print_plan(100,100,540,48,0,&plan));assert(plan.pages==0);
 assert(!insight_graph_print_plan(NAN,100,540,780,0,&plan));
 assert(!insight_graph_print_plan(100,INFINITY,540,780,1,&plan));
 assert(!insight_graph_print_plan(1000000,1000000,1,49,1,&plan));
 assert(insight_graph_print_plan(1000000,1000000,1,49,0,&plan));assert(plan.pages==1);
 for(int w=100;w<30000;w+=220) for(int h=100;h<10000;h+=640) {
  assert(insight_graph_print_plan(w,h,540,780,0,&plan));assert(plan.pages==1 && plan.scale*w<=540.000001 && plan.scale*h<=732.000001);
  if(insight_graph_print_plan(w,h,540,780,1,&plan)) {
   assert(plan.pages==plan.columns*plan.rows && plan.pages<=256);assert(plan.columns*540>=w*0.75 && plan.rows*732>=h*0.75);
   assert((plan.columns-1)*540<w*0.75 && (plan.rows-1)*732<h*0.75);
  }
 }
 puts("Graph layout: orientations, zoom, gaps, bounds and node limit passed");return 0;
}
