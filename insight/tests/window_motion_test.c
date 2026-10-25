/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/window_motion.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static void layout(InsightWindowRect area) {
 InsightWindowRect r[5];insight_window_edge_layout(area,NULL,r);
 for(int n=0;n<5;n++) {assert(r[n].width>0 && r[n].height>0);assert(r[n].x>=area.x && r[n].y>=area.y);assert(r[n].x+r[n].width<=area.x+area.width);assert(r[n].y+r[n].height<=area.y+area.height);}
 assert(r[0].y==area.y);assert(r[1].x==area.x);assert(r[2].x+r[2].width==area.x+area.width);assert(r[3].y+r[3].height==area.y+area.height);
 for(int n=0;n<5;n++) {if(n!=1 && n!=2) assert(abs(2*r[n].x+r[n].width-2*area.x-area.width)<=1);if(n!=0 && n!=3) assert(abs(2*r[n].y+r[n].height-2*area.y-area.height)<=1);}
}
int main(void) {
 InsightWindowRect visible=insight_window_visible_frame(0,24,(InsightWindowRect){1,44,1100,100});assert(visible.x==0 && visible.y==24 && visible.width==1102 && visible.height==121);
 visible=insight_window_visible_frame(-1600,40,(InsightWindowRect){-1599,60,280,600});assert(visible.x==-1600 && visible.y==40 && visible.width==282 && visible.height==621);
 layout((InsightWindowRect){0,0,1600,1200});layout((InsightWindowRect){-1920,48,1920,1032});layout((InsightWindowRect){0,24,640,456});
 InsightWindowRect sizes[5]={{0,0,950,130},{0,0,310,550},{0,0,360,600},{0,0,920,360},{0,0,700,400}},out[5];insight_window_edge_layout((InsightWindowRect){-1600,40,1600,900},sizes,out);assert(out[0].x==-1275);assert(out[2].x==-360);assert(out[3].y==580);assert(out[4].x==-1150 && out[4].y==290);
 InsightWindowRect a[3]={{0,0,100,100},{0,100,200,200},{0,200,300,300}},b[3]={{100,0,100,100},{200,100,200,200},{300,200,300,300}};InsightWindowMotion motion[3];double speed=insight_window_motion_plan(motion,a,b,3,2,0);assert(fabs(speed-100)<1e-9);assert(motion[0].seconds==1 && motion[1].seconds==2 && motion[2].seconds==3);
 for(int n=0;n<3;n++) {InsightWindowRect value;assert(!insight_window_motion_sample(&motion[n],.5,&value));assert(value.x==50);assert(insight_window_motion_sample(&motion[n],9,&value));assert(value.x==b[n].x);}
 InsightWindowRect diagonal={600,800,100,100},origin={0,0,100,100};InsightWindowMotion d;insight_window_motion_plan(&d,&origin,&diagonal,1,2,100);InsightWindowRect value;assert(!insight_window_motion_sample(&d,.5,&value));assert(value.x==30 && value.y==40);
 double reverse=insight_window_motion_plan(&d,&diagonal,&origin,1,2,speed);assert(reverse==speed && d.seconds==10);assert(!insight_window_motion_sample(&d,.5,&value));assert(value.x==570 && value.y==760);
 origin=(InsightWindowRect){-40,-20,100,100};diagonal=(InsightWindowRect){-40,-20,300,200};insight_window_motion_plan(&d,&origin,&diagonal,1,2,0);assert(!insight_window_motion_sample(&d,1,&value));assert(value.width==200 && value.height==150);assert(value.x==-40);assert(insight_window_motion_sample(&d,2,&value));insight_window_motion_plan(&d,&origin,&origin,1,2,0);assert(insight_window_motion_sample(&d,0,&value));
 origin=(InsightWindowRect){0,0,100,100};diagonal=(InsightWindowRect){10000,0,100,100};insight_window_motion_plan(&d,&origin,&diagonal,1,.8,12500);
 assert(!insight_window_motion_sample_eased(&d,.2,&value));assert(value.x==1035);assert(!insight_window_motion_sample_eased(&d,.4,&value));assert(value.x==5000);assert(!insight_window_motion_sample_eased(&d,.6,&value));assert(value.x==8965);assert(insight_window_motion_sample_eased(&d,.8,&value));assert(value.x==10000);
 int previous=0;for(int n=0;n<=800;n++) {insight_window_motion_sample_eased(&d,n/1000.0,&value);assert(value.x>=previous && value.x<=10000);previous=value.x;}
 insight_window_motion_sample_eased(&d,.001,&value);assert(value.x<=1);insight_window_motion_sample_eased(&d,.799,&value);assert(value.x>=9999);
 puts("S-shaped acceleration/deceleration and exact 0.8-second arrival passed");
 puts("Workarea edge/center anchors, native measured sizes, equal vector speed, independent arrivals, reverse sampling and resize-only motion passed");return 0;
}
