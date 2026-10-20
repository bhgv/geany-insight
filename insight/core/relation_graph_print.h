/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RELATION_GRAPH_PRINT_H
#define INSIGHT_RELATION_GRAPH_PRINT_H
/* Points (1/72 inch). Graph geometry uses 96 logical pixels per inch.
 * Keep pagination independent of the screen zoom and printer resolution. */
#define INSIGHT_GRAPH_PRINT_MAX_PAGES 256
#define INSIGHT_GRAPH_PRINT_HEADER 24
#define INSIGHT_GRAPH_PRINT_FOOTER 24
typedef struct { int columns,rows,pages;double scale,width,height; } InsightGraphPrintPlan;
static inline int insight_graph_print_plan(double graph_width,double graph_height,double page_width,double page_height,int tiled,InsightGraphPrintPlan *plan) {
 if(!plan) return 0;
 *plan=(InsightGraphPrintPlan){0};
 double height=page_height-INSIGHT_GRAPH_PRINT_HEADER-INSIGHT_GRAPH_PRINT_FOOTER;
 if(!(graph_width>0 && graph_width<=1000000 && graph_height>0 && graph_height<=1000000 && page_width>=1 && page_width<=100000 && height>=1 && height<=100000)) return 0;
 double scale=0.75;int columns=1,rows=1;
 if(tiled) {
  double x=graph_width*scale/page_width,y=graph_height*scale/height;
  if(!(x<=INSIGHT_GRAPH_PRINT_MAX_PAGES && y<=INSIGHT_GRAPH_PRINT_MAX_PAGES)) return 0;
  columns=(int)x;rows=(int)y;if(columns<x) columns++;if(rows<y) rows++;
  if(columns<1) columns=1;
  if(rows<1) rows=1;
  if(columns>INSIGHT_GRAPH_PRINT_MAX_PAGES/rows) return 0;
 }else {
  double x=page_width/graph_width,y=height/graph_height;
  if(x<scale) scale=x;
  if(y<scale) scale=y;
 }
 *plan=(InsightGraphPrintPlan){columns,rows,columns*rows,scale,page_width,height};return 1;
}
#endif
