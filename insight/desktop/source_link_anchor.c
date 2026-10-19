/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "source_link_anchor.h"
#include <stdint.h>
static size_t after_delete(size_t value,size_t position,size_t end,size_t length) {
 return value<=position ? value : value<end ? position : value-length;
}
void insight_source_link_anchor_edit(InsightSourceLinkAnchor *a,size_t position,size_t removed,size_t inserted) {
 if(!a || !a->valid) return;
 if(removed>SIZE_MAX-position || inserted>SIZE_MAX-a->end || inserted>SIZE_MAX-a->line_end) { a->valid=false;return; }
 size_t limit=position+removed;
 if(removed && position<=a->line_start && limit>=a->line_end && limit>a->line_start) { a->valid=false;return; }
 a->start=after_delete(a->start,position,limit,removed);
 a->end=after_delete(a->end,position,limit,removed);
 a->line_start=after_delete(a->line_start,position,limit,removed);
 a->line_end=after_delete(a->line_end,position,limit,removed);
 if(inserted) {
  bool before=position<=a->start;
  if(before) a->start+=inserted;
  if(before || position<a->end) a->end+=inserted;
  if(position<=a->line_start) a->line_start+=inserted;
  if(position<a->line_end) a->line_end+=inserted;
 }
}
