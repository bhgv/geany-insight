/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_WINDOW_MOTION_H
#define INSIGHT_WINDOW_MOTION_H
#include <stddef.h>
enum { INSIGHT_FLOAT_TOOLS,INSIGHT_FLOAT_LEFT,INSIGHT_FLOAT_RIGHT,INSIGHT_FLOAT_BOTTOM,INSIGHT_FLOAT_EDITOR,INSIGHT_FLOAT_COUNT };
typedef struct { int x,y,width,height; } InsightWindowRect;
typedef struct { InsightWindowRect from,to;double distance,seconds; } InsightWindowMotion;
/* Optional sizes describe measured outer frames, including native decorations. */
void insight_window_edge_layout(InsightWindowRect area,const InsightWindowRect *sizes,InsightWindowRect out[INSIGHT_FLOAT_COUNT]);
InsightWindowRect insight_window_rect_clamp(InsightWindowRect rect,InsightWindowRect area);
/* One speed for the entire transition. Typical travel takes nominal_seconds;
 * longer paths take longer (bounded to three times nominal_seconds). */
double insight_window_motion_plan(InsightWindowMotion *motions,const InsightWindowRect *from,const InsightWindowRect *to,size_t count,double nominal_seconds,double speed);
/* Pure absolute-time sampling, no per-frame accumulation or easing. */
int insight_window_motion_sample(const InsightWindowMotion *motion,double elapsed,InsightWindowRect *out);
/* S-shaped absolute-time sampling with zero endpoint speed/acceleration. */
int insight_window_motion_sample_eased(const InsightWindowMotion *motion,double elapsed,InsightWindowRect *out);
#endif
