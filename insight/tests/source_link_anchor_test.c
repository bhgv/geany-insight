/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "source_link_anchor.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static InsightSourceLinkAnchor anchor(void) { return (InsightSourceLinkAnchor){12,18,10,24,true}; }
int main(void) {
 InsightSourceLinkAnchor a=anchor();insight_source_link_anchor_edit(&a,0,0,5);assert(a.valid && a.start==17 && a.end==23);
 insight_source_link_anchor_edit(&a,0,5,0);assert(a.start==12 && a.end==18);
 a=anchor();insight_source_link_anchor_edit(&a,12,0,3);assert(a.start==15 && a.end==21);
 a=anchor();insight_source_link_anchor_edit(&a,18,0,3);assert(a.start==12 && a.end==18);
 a=anchor();insight_source_link_anchor_edit(&a,14,0,3);assert(a.start==12 && a.end==21);
 a=anchor();insight_source_link_anchor_edit(&a,12,6,0);assert(a.valid && a.start==12 && a.end==12);
 a=anchor();insight_source_link_anchor_edit(&a,10,14,0);assert(!a.valid);
 insight_source_link_anchor_edit(&a,10,0,14);assert(!a.valid); /* Undo requires a fresh search. */
 a=anchor();insight_source_link_anchor_edit(&a,0,SIZE_MAX,0);assert(!a.valid);
 a=anchor();insight_source_link_anchor_edit(&a,30,0,SIZE_MAX);assert(!a.valid);
 a=(InsightSourceLinkAnchor){10,10,10,11,true};insight_source_link_anchor_edit(&a,10,0,4);assert(a.start==14 && a.end==14);
 a=anchor();insight_source_link_anchor_edit(&a,11,3,0);assert(a.valid && a.start==11 && a.end==15);
 /* Round trips for every insertion/deletion strictly before or after the
  * tracked source line: coordinates, including UTF-8 byte counts, must agree. */
 for(size_t position=0;position<40;position++) for(size_t length=1;length<128;length++) {
  if(position>=10 && position<24) continue;
  a=anchor();insight_source_link_anchor_edit(&a,position,0,length);
  insight_source_link_anchor_edit(&a,position,length,0);
  assert(a.valid && a.start==12 && a.end==18 && a.line_start==10 && a.line_end==24);
 }
 puts("Source link byte anchors: boundaries, line deletion, zero-width, overflow and edit round trips passed.");
 return 0;
}
