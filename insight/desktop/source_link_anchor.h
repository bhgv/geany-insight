/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SOURCE_LINK_ANCHOR_H
#define INSIGHT_SOURCE_LINK_ANCHOR_H
#include <stddef.h>
#include <stdbool.h>
typedef struct {
 size_t start, end, line_start, line_end;
 bool valid;
} InsightSourceLinkAnchor;
/* Byte coordinates in the pre-edit buffer. A deleted source line is terminal:
 * undo does not silently reconnect an unrelated result. */
void insight_source_link_anchor_edit(InsightSourceLinkAnchor *,size_t position,size_t removed,size_t inserted);
#endif
