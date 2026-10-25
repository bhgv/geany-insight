/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "window_motion.h"
#include <math.h>
static int minimum(int a,int b) { return a<b ? a:b; }
static int maximum(int a,int b) { return a>b ? a:b; }
static int mix(int a,int b,double t) { return (int)lround((double)a+((double)b-a)*t); }
InsightWindowRect insight_window_rect_clamp(InsightWindowRect rect,InsightWindowRect area) {
 area.width=maximum(1,area.width);area.height=maximum(1,area.height);
 rect.width=minimum(maximum(1,rect.width),area.width);rect.height=minimum(maximum(1,rect.height),area.height);
 rect.x=maximum(area.x,minimum(rect.x,area.x+area.width-rect.width));rect.y=maximum(area.y,minimum(rect.y,area.y+area.height-rect.height));return rect;
}
void insight_window_edge_layout(InsightWindowRect area,const InsightWindowRect *sizes,InsightWindowRect out[INSIGHT_FLOAT_COUNT]) {
 int w=maximum(1,area.width),h=maximum(1,area.height);
 out[0]=(InsightWindowRect){0,0,minimum(1100,w),100};
 out[1]=(InsightWindowRect){0,0,minimum(280,maximum(180,w/6)),maximum(220,h*2/3)};
 out[2]=(InsightWindowRect){0,0,minimum(330,maximum(200,w/5)),maximum(220,h*2/3)};
 out[3]=(InsightWindowRect){0,0,minimum(1000,maximum(420,w*3/5)),minimum(320,maximum(180,h/4))};
 if(sizes) for(int n=0;n<INSIGHT_FLOAT_EDITOR;n++) {if(sizes[n].width>0) out[n].width=sizes[n].width;if(sizes[n].height>0) out[n].height=sizes[n].height;}
 int side=maximum(out[1].width,out[2].width),cap=maximum(out[0].height,out[3].height);
 out[4]=(InsightWindowRect){0,0,maximum(360,w-2*side-24),maximum(220,h-2*cap-24)};
 if(sizes) {if(sizes[4].width>0) out[4].width=sizes[4].width;if(sizes[4].height>0) out[4].height=sizes[4].height;}
 for(int n=0;n<INSIGHT_FLOAT_COUNT;n++) {out[n].width=minimum(w,maximum(1,out[n].width));out[n].height=minimum(h,maximum(1,out[n].height));out[n].x=area.x+(w-out[n].width)/2;out[n].y=area.y+(h-out[n].height)/2;}
 out[0].y=area.y;out[1].x=area.x;out[2].x=area.x+w-out[2].width;out[3].y=area.y+h-out[3].height;
}
double insight_window_motion_plan(InsightWindowMotion *motions,const InsightWindowRect *from,const InsightWindowRect *to,size_t count,double nominal_seconds,double speed) {
 double total=0,longest=0;size_t moving=0;if(!isfinite(nominal_seconds) || nominal_seconds<=0) nominal_seconds=2;
 for(size_t n=0;n<count;n++) {double distance=hypot((double)to[n].x-from[n].x,(double)to[n].y-from[n].y);motions[n]=(InsightWindowMotion){from[n],to[n],distance,0};if(distance>0) {total+=distance;moving++;if(distance>longest) longest=distance;}}
 if(!isfinite(speed) || speed<=0) {speed=moving ? total/moving/nominal_seconds:1;if(speed<longest/(3*nominal_seconds)) speed=longest/(3*nominal_seconds);}
 for(size_t n=0;n<count;n++) {InsightWindowMotion *m=&motions[n];m->seconds=m->distance>0 ? m->distance/speed:((m->from.width!=m->to.width || m->from.height!=m->to.height) ? nominal_seconds:0);}
 return speed;
}
int insight_window_motion_sample(const InsightWindowMotion *motion,double elapsed,InsightWindowRect *out) {
 if(!isfinite(elapsed) || elapsed<0) elapsed=0;
 double t=motion->seconds>0 ? elapsed/motion->seconds:1;if(t>1) t=1;
 *out=(InsightWindowRect){mix(motion->from.x,motion->to.x,t),mix(motion->from.y,motion->to.y,t),mix(motion->from.width,motion->to.width,t),mix(motion->from.height,motion->to.height,t)};return t>=1;
}

int insight_window_motion_sample_eased(const InsightWindowMotion *motion,double elapsed,InsightWindowRect *out) {
 if(!isfinite(elapsed) || elapsed<0) elapsed=0;
 double t=motion->seconds>0 ? elapsed/motion->seconds:1;if(t>1) t=1;
 /* Quintic smootherstep: zero velocity and acceleration at both ends. */
 double eased=t*t*t*(t*(t*6-15)+10);
 *out=(InsightWindowRect){mix(motion->from.x,motion->to.x,eased),mix(motion->from.y,motion->to.y,eased),mix(motion->from.width,motion->to.width,eased),mix(motion->from.height,motion->to.height,eased)};return t>=1;
}
