/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "diff.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef struct { const char *text; size_t *offsets, count; } Lines;
static size_t next_line(const char *text,size_t length,size_t at) {
 while(at<length && text[at]!='\r' && text[at]!='\n') at++;
 if(at<length) { char c=text[at++];if(c=='\r' && at<length && text[at]=='\n') at++; }
 return at;
}
static int split(const char *text, Lines *out) {
 size_t n=0,len=strlen(text);out->text=text;
 for(size_t at=0;at<len;at=next_line(text,len,at)) n++;
 if(n>=SIZE_MAX/sizeof(size_t)) return -1;
 out->offsets=malloc((n+1)*sizeof(size_t));if(!out->offsets) return -1;
 out->count=n;out->offsets[0]=0;n=0;
 for(size_t at=0;at<len;) { at=next_line(text,len,at);out->offsets[++n]=at; }
 return 0;
}
static int equal(const Lines *a,size_t i,const Lines *b,size_t j) {
 size_t n=a->offsets[i+1]-a->offsets[i];
 return n==b->offsets[j+1]-b->offsets[j] && !memcmp(a->text+a->offsets[i],b->text+b->offsets[j],n);
}
void insight_diff_clear(InsightDiff *diff) { free(diff->hunks); diff->hunks=NULL; diff->count=0; }
int insight_diff_compute(const char *a,const char *b,InsightDiff *out) {
 Lines x={0},y={0}; unsigned *table=NULL; int status=-1;
 out->hunks=NULL; out->count=0;
 if(split(a,&x) || split(b,&y)) goto done;
 /* Trim common edges before allocating the quadratic LCS table. */
 size_t start=0,xe=x.count,ye=y.count;
 while(start<xe && start<ye && equal(&x,start,&y,start)) start++;
 while(xe>start && ye>start && equal(&x,xe-1,&y,ye-1)) { xe--; ye--; }
 size_t nx=xe-start,ny=ye-start,width=ny+1;
 /* After common-edge trimming, an empty side is one exact edit. */
 if(!nx || !ny) {
  if(nx || ny) {
   out->hunks=malloc(sizeof *out->hunks);if(!out->hunks) goto done;
   out->hunks[0]=(InsightDiffHunk){x.offsets[start],x.offsets[xe],y.offsets[start],y.offsets[ye]};out->count=1;
  }
  status=0;goto done;
 }
 if(nx+1>4000000/width) { status=-2; goto done; }
 table=calloc((nx+1)*width,sizeof(unsigned));
 /* Every separation consumes an equal line from both sides. */
 out->hunks=malloc(((nx<ny ? nx : ny)+1)*sizeof(InsightDiffHunk));
 if(!table || !out->hunks) goto done;
 for(size_t i=nx;i-->0;) for(size_t j=ny;j-->0;)
  table[i*width+j]=equal(&x,start+i,&y,start+j) ? 1+table[(i+1)*width+j+1] :
   (table[(i+1)*width+j]>=table[i*width+j+1] ? table[(i+1)*width+j] : table[i*width+j+1]);
 size_t i=0,j=0;
 while(i<nx || j<ny) {
  if(i<nx && j<ny && equal(&x,start+i,&y,start+j)) { i++;j++;continue; }
  size_t ib=i,jb=j;
  do {
   if(j==ny || (i<nx && table[(i+1)*width+j]>=table[i*width+j+1])) i++; else j++;
  } while((i<nx || j<ny) && !(i<nx && j<ny && equal(&x,start+i,&y,start+j)));
  out->hunks[out->count++]=(InsightDiffHunk){x.offsets[start+ib],x.offsets[start+i],y.offsets[start+jb],y.offsets[start+j]};
 }
 status=0;
done:
 free(x.offsets);free(y.offsets);free(table);
 if(status) insight_diff_clear(out);
 return status;
}
