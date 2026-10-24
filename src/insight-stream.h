/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_STREAM_H
#define INSIGHT_STREAM_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
/* File-backed piece table. All offsets and lengths are bytes, including text. */
typedef struct InsightStream InsightStream;
typedef struct { uint64_t offset,length,right_offset,right_length; } InsightStreamRange;
InsightStream *insight_stream_open(const char *left,const char *right);
void insight_stream_free(InsightStream *s);
const char *insight_stream_error(InsightStream *s);
uint64_t insight_stream_size(InsightStream *s,unsigned side); /* 0/1 sources, 2 result */
int insight_stream_read(InsightStream *s,unsigned side,uint64_t offset,void *out,size_t size);
int insight_stream_replace(InsightStream *s,uint64_t at,uint64_t remove,unsigned source,uint64_t offset,uint64_t length,const void *bytes);
/* source=0/1 references original blocks; source=2 appends manual bytes. */
typedef struct { uint64_t target,removed,offset,length;unsigned source; } InsightStreamEvent;
int insight_stream_map_range(InsightStream *s,const InsightStreamRange *range,uint64_t *at,uint64_t *length);
int insight_stream_event(InsightStream *s,size_t index,InsightStreamEvent *event);
size_t insight_stream_history_count(InsightStream *s);
size_t insight_stream_history_position(InsightStream *s);
int insight_stream_jump(InsightStream *s,size_t position);
/* One bounded scan step; 1 finished, 0 more, -1 failure. Ranges spill at 2 MiB. */
int insight_stream_scan(InsightStream *s,size_t budget);
int insight_stream_scan_text(InsightStream *s,size_t budget);
uint64_t insight_stream_scanned(InsightStream *s);
size_t insight_stream_difference_count(InsightStream *s);
int insight_stream_difference(InsightStream *s,size_t index,InsightStreamRange *range);
/* Output supplied by caller: sources must never be truncated before this returns. */
int insight_stream_write(InsightStream *s,FILE *output);
int insight_stream_write_base64(InsightStream *s,FILE *output);
size_t insight_stream_resident_metadata(InsightStream *s);
#endif
