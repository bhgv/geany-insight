/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "diff.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
typedef struct { const char *text; size_t *offsets, count; char *keys; size_t *key_offsets; } Lines;
static size_t next_line(const char *text,size_t length,size_t at) {
 while(at<length && text[at]!='\r' && text[at]!='\n') at++;
 if(at<length) { char c=text[at++];if(c=='\r' && at<length && text[at]=='\n') at++; }
 return at;
}
static int split(const char *text, unsigned options, Lines *out) {
 size_t n=0,len=strlen(text);out->text=text;
 for(size_t at=0;at<len;at=next_line(text,len,at)) n++;
 if(n>=SIZE_MAX/sizeof(size_t)) return -1;
 out->offsets=malloc((n+1)*sizeof(size_t));if(!out->offsets) return -1;
 out->count=n;out->offsets[0]=0;n=0;
 for(size_t at=0;at<len;) { at=next_line(text,len,at);out->offsets[++n]=at; }
 if(options) {
  out->keys=malloc(len+1);out->key_offsets=malloc((n+1)*sizeof(size_t));
  if(!out->keys || !out->key_offsets) return -1;
  size_t key=0;out->key_offsets[0]=0;
  for(size_t line=0;line<n;line++) {
   for(size_t at=out->offsets[line];at<out->offsets[line+1];at++) {
    char c=text[at];
    if((options&INSIGHT_DIFF_IGNORE_SPACE) && (c==' ' || c=='\t' || c=='\v' || c=='\f')) continue;
    if((options&INSIGHT_DIFF_IGNORE_EOL) && (c=='\r' || c=='\n')) {
     out->keys[key++]='\n';
     if(c=='\r' && at+1<out->offsets[line+1] && text[at+1]=='\n') at++;
    } else out->keys[key++]=c;
   }
   out->key_offsets[line+1]=key;
  }
 }
 return 0;
}
static int equal(const Lines *a,size_t i,const Lines *b,size_t j) {
 const size_t *x=a->keys ? a->key_offsets : a->offsets,*y=b->keys ? b->key_offsets : b->offsets;
 const char *at=a->keys ? a->keys : a->text,*bt=b->keys ? b->keys : b->text;
 size_t n=x[i+1]-x[i];return n==y[j+1]-y[j] && !memcmp(at+x[i],bt+y[j],n);
}
static size_t previous_line(const Lines *lines,size_t offset) {
 size_t lo=0,hi=lines->count;
 while(lo<hi) { size_t mid=lo+(hi-lo)/2;if(lines->offsets[mid]<offset) lo=mid+1;else hi=mid; }
 return lines->offsets[lo ? lo-1 : 0];
}
/* Mixed CR/LF endings must not fuse across a merge seam. Include adjacent
 * equal lines where necessary, then coalesce overlapping blocks. */
static void safe_seams(InsightDiff *out,const Lines *a,const Lines *b) {
 size_t count=0,alen=a->offsets[a->count],blen=b->offsets[b->count];
 for(size_t i=0;i<out->count;i++) {
  InsightDiffHunk h=out->hunks[i];
  while(h.a_begin && h.b_begin &&
    ((a->text[h.a_begin-1]=='\r' && b->text[h.b_begin]=='\n') ||
     (b->text[h.b_begin-1]=='\r' && a->text[h.a_begin]=='\n'))) {
   h.a_begin=previous_line(a,h.a_begin);h.b_begin=previous_line(b,h.b_begin);
  }
  while(h.a_end<alen && h.b_end<blen &&
    ((h.b_end && b->text[h.b_end-1]=='\r' && a->text[h.a_end]=='\n') ||
     (h.a_end && a->text[h.a_end-1]=='\r' && b->text[h.b_end]=='\n'))) {
   h.a_end=next_line(a->text,alen,h.a_end);h.b_end=next_line(b->text,blen,h.b_end);
  }
  if(count && (h.a_begin<=out->hunks[count-1].a_end || h.b_begin<=out->hunks[count-1].b_end)) {
   InsightDiffHunk *last=&out->hunks[count-1];
   if(h.a_end>last->a_end) last->a_end=h.a_end;
   if(h.b_end>last->b_end) last->b_end=h.b_end;
  } else out->hunks[count++]=h;
 }
 out->count=count;
}
void insight_diff_clear(InsightDiff *diff) { free(diff->hunks); diff->hunks=NULL; diff->count=0; }
int insight_diff_compute_cancellable(const char *a,const char *b,unsigned options,InsightDiffCancel cancelled,void *data,InsightDiff *out) {
 Lines x={0},y={0}; unsigned *table=NULL; int status=-1;
 out->hunks=NULL; out->count=0;
 if(cancelled && cancelled(data)) { status=-3;goto done; }
 if(split(a,options,&x) || split(b,options,&y)) goto done;
 /* Trim common edges before allocating the quadratic LCS table. */
 size_t start=0,xe=x.count,ye=y.count;
 while(start<xe && start<ye && equal(&x,start,&y,start)) start++;
 while(xe>start && ye>start && equal(&x,xe-1,&y,ye-1)) { xe--; ye--; }
 if(cancelled && cancelled(data)) { status=-3;goto done; }
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
 for(size_t i=nx;i-->0;) {
  if(cancelled && cancelled(data)) { status=-3;goto done; }
  for(size_t j=ny;j-->0;)
  table[i*width+j]=equal(&x,start+i,&y,start+j) ? 1+table[(i+1)*width+j+1] :
   (table[(i+1)*width+j]>=table[i*width+j+1] ? table[(i+1)*width+j] : table[i*width+j+1]);
 }
 size_t i=0,j=0;
 while(i<nx || j<ny) {
  if(cancelled && cancelled(data)) { status=-3;goto done; }
  if(i<nx && j<ny && equal(&x,start+i,&y,start+j)) { i++;j++;continue; }
  size_t ib=i,jb=j;
  do {
   if(j==ny || (i<nx && table[(i+1)*width+j]>=table[i*width+j+1])) i++; else j++;
  } while((i<nx || j<ny) && !(i<nx && j<ny && equal(&x,start+i,&y,start+j)));
  out->hunks[out->count++]=(InsightDiffHunk){x.offsets[start+ib],x.offsets[start+i],y.offsets[start+jb],y.offsets[start+j]};
 }
 status=0;
done:
 if(!status && options) safe_seams(out,&x,&y);
 free(x.offsets);free(y.offsets);free(x.keys);free(y.keys);free(x.key_offsets);free(y.key_offsets);free(table);
 if(status) insight_diff_clear(out);
 return status;
}

size_t insight_diff_map_offset(const InsightDiff *diff, int side, size_t offset) {
 size_t from=0,to=0;
 for(size_t i=0;i<diff->count;i++) {
  const InsightDiffHunk *h=&diff->hunks[i];
  size_t a=side ? h->b_begin : h->a_begin,z=side ? h->b_end : h->a_end;
  size_t b=side ? h->a_begin : h->b_begin,y=side ? h->a_end : h->b_end;
  if(offset<a) return to+(offset-from);
  if(offset<z) return b;
  from=z;to=y;
 }
 return to+(offset-from);
}

int insight_diff_compute_options(const char *a,const char *b,unsigned options,InsightDiff *out) {
 return insight_diff_compute_cancellable(a,b,options,NULL,NULL,out);
}
int insight_diff_compute(const char *a,const char *b,InsightDiff *out) {
 return insight_diff_compute_options(a,b,0,out);
}
static size_t line_at(const char *text,size_t offset) {
 size_t line=0;
 for(size_t at=0;at<offset;line++) at=next_line(text,offset,at);
 return line;
}
size_t insight_diff_map_line(const InsightDiff *diff,int side,const char *source,const char *target,size_t line) {
 size_t from=0,to=0,source_at=0,target_at=0;
 for(size_t i=0;i<diff->count;i++) {
  const InsightDiffHunk *h=&diff->hunks[i];
  size_t ab=side ? h->b_begin : h->a_begin,az=side ? h->b_end : h->a_end;
  size_t bb=side ? h->a_begin : h->b_begin,bz=side ? h->a_end : h->b_end;
  size_t a=from+line_at(source+source_at,ab-source_at),z=a+line_at(source+ab,az-ab);
  size_t b=to+line_at(target+target_at,bb-target_at),y=b+line_at(target+bb,bz-bb);
  source_at=az;target_at=bz;
  if(line<a) return to+(line-from);
  if(line<z) return b;
  from=z;to=y;
 }
 return to+(line-from);
}
