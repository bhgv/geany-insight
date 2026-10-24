/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _WIN32
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#endif
#include "insight-stream.h"
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#ifndef S_ISREG
#define S_ISREG(mode) (((mode)&_S_IFMT)==_S_IFREG)
#endif
#define seek64 _fseeki64
#define tell64 _ftelli64
#define stream_stat _stat64
#define stream_fstat _fstat64
#define stream_fileno _fileno
#else
#include <unistd.h>
#define seek64 fseeko
#define tell64 ftello
#define stream_stat stat
#define stream_fstat fstat
#define stream_fileno fileno
#endif
#define SPILL (2u*1024u*1024u)
#define BLOCK 65536u
typedef struct { uint64_t offset,length;unsigned source; } Piece;
typedef struct { Piece *pieces;size_t count;uint64_t disk,size;InsightStreamEvent event; } Version;
struct InsightStream {
 FILE *source[2],*manual,*metadata,*ranges_file;
 struct stream_stat stamp[2];uint64_t sizes[2],manual_size,scanned,run_start;
 uint64_t text_at[2];unsigned char *manual_memory;InsightStreamRange *ranges;size_t range_count;
 Version *versions;size_t version_count,position,resident;char error[160];int scanning_run,scan_done;
};
static int fail(InsightStream *s,const char *message) { snprintf(s->error,sizeof(s->error),"%s",message);return 0; }
static int read_at(InsightStream *s,FILE *f,uint64_t offset,void *out,size_t size) {
 if(offset>INT64_MAX || seek64(f,(int64_t)offset,SEEK_SET) || (size && fread(out,1,size,f)!=size)) return fail(s,"Cannot read source or temporary data.");return 1;
}
static int stable(InsightStream *s) {
 for(unsigned i=0;i<2;i++) if(s->source[i]) { struct stream_stat now;if(stream_fstat(stream_fileno(s->source[i]),&now) || now.st_size!=s->stamp[i].st_size || now.st_mtime!=s->stamp[i].st_mtime
#ifndef _WIN32
 || now.st_mtim.tv_nsec!=s->stamp[i].st_mtim.tv_nsec
#endif
) return fail(s,"Source changed on disk. Reopen comparison before reading or saving."); }return 1;
}
static FILE *private_temp(void) {
#ifdef _WIN32
 return tmpfile();
#else
 const char *dir=getenv("TMPDIR");if(!dir || !*dir) dir="/tmp";
 size_t n=strlen(dir)+40;char *path=malloc(n);if(!path) return NULL;
 snprintf(path,n,"%s/insight-pieces-XXXXXX",dir);int fd=mkstemp(path);
 if(fd<0 && !getenv("TMPDIR")) { snprintf(path,n,"./insight-pieces-XXXXXX");fd=mkstemp(path); }
 FILE *f=NULL;if(fd>=0) { unlink(path);f=fdopen(fd,"w+b");if(!f) close(fd); }free(path);return f;
#endif
}
static int append_file(InsightStream *s,FILE **file,const void *data,size_t bytes,uint64_t *offset) {
 if(!*file) *file=private_temp();if(!*file || seek64(*file,0,SEEK_END)) return fail(s,"Cannot create or seek private temporary storage.");
 int64_t at=tell64(*file);if(at<0 || (bytes && fwrite(data,1,bytes,*file)!=bytes) || fflush(*file)) return fail(s,"Cannot write temporary storage (disk full?).");if(offset) *offset=(uint64_t)at;return 1;
}
static int piece_get(InsightStream *s,Version *v,size_t index,Piece *p) {
 if(index>=v->count) return fail(s,"Invalid piece index.");if(v->pieces) { *p=v->pieces[index];return 1; }return read_at(s,s->metadata,v->disk+index*sizeof(Piece),p,sizeof(*p));
}
static int spill_versions(InsightStream *s) {
 for(size_t i=0;i<s->version_count && s->resident>SPILL;i++) { Version *v=&s->versions[i];if(v->pieces) { if(!append_file(s,&s->metadata,v->pieces,v->count*sizeof(Piece),&v->disk)) return 0;s->resident-=v->count*sizeof(Piece);free(v->pieces);v->pieces=NULL; } }return 1;
}
static FILE *source_open(const char *path) {
#ifdef _WIN32
 int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);if(!n) return NULL;wchar_t *wide=malloc((size_t)n*sizeof(wchar_t));if(!wide) return NULL;MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,wide,n);FILE *file=_wfopen(wide,L"rb");free(wide);return file;
#else
 return fopen(path,"rb");
#endif
}
InsightStream *insight_stream_open(const char *left,const char *right) {
 InsightStream *s=calloc(1,sizeof(*s));if(!s) return NULL;const char *paths[]={left,right};
 for(unsigned i=0;i<2;i++) if(paths[i] && *paths[i]) {
  s->source[i]=source_open(paths[i]);if(!s->source[i] || stream_fstat(stream_fileno(s->source[i]),&s->stamp[i]) || !S_ISREG(s->stamp[i].st_mode) || s->stamp[i].st_size<0) { fail(s,"Cannot open regular input file.");return s; }s->sizes[i]=(uint64_t)s->stamp[i].st_size;
 }
 s->versions=calloc(1,sizeof(Version));if(!s->versions) { fail(s,"Out of memory.");return s; }s->version_count=1;s->versions[0].size=s->sizes[0];
 if(s->sizes[0]) { s->versions[0].pieces=malloc(sizeof(Piece));if(!s->versions[0].pieces) { fail(s,"Out of memory.");return s; }s->versions[0].pieces[0]=(Piece){0,s->sizes[0],0};s->versions[0].count=1;s->resident=sizeof(Piece); }return s;
}
void insight_stream_free(InsightStream *s) { if(!s) return;for(unsigned i=0;i<2;i++) if(s->source[i]) fclose(s->source[i]);if(s->manual) fclose(s->manual);if(s->metadata) fclose(s->metadata);if(s->ranges_file) fclose(s->ranges_file);for(size_t i=0;i<s->version_count;i++) free(s->versions[i].pieces);free(s->versions);free(s->manual_memory);free(s->ranges);free(s); }
const char *insight_stream_error(InsightStream *s) { return s ? s->error:"Out of memory."; }
uint64_t insight_stream_size(InsightStream *s,unsigned side) { return side<2 ? s->sizes[side]:s->version_count ? s->versions[s->position].size:0; }
static int data_read(InsightStream *s,unsigned source,uint64_t offset,void *out,size_t size) {
 if(source<2) return read_at(s,s->source[source],offset,out,size);
 if(s->manual) return read_at(s,s->manual,offset,out,size);if(size) memcpy(out,s->manual_memory+(size_t)offset,size);return 1;
}
int insight_stream_read(InsightStream *s,unsigned side,uint64_t offset,void *out,size_t size) {
 uint64_t total=insight_stream_size(s,side);if(side>2 || offset>total || size>total-offset) return fail(s,"Read outside file.");if(!stable(s)) return 0;if(!size) return 1;if(side<2) return data_read(s,side,offset,out,size);
 Version *v=&s->versions[s->position];uint64_t begin=0;unsigned char *dest=out;
 for(size_t i=0;i<v->count && size;i++) { Piece p;if(!piece_get(s,v,i,&p)) return 0;if(offset>=begin+p.length) { begin+=p.length;continue; }uint64_t relative=offset-begin;size_t n=(size_t)((p.length-relative)<size ? p.length-relative:size);if(!data_read(s,p.source,p.offset+relative,dest,n)) return 0;dest+=n;size-=n;offset+=n;begin+=p.length; }return size==0 ? 1:fail(s,"Incomplete piece table.");
}
static void push_piece(Piece *out,size_t *count,Piece p) { if(!p.length) return;if(*count && out[*count-1].source==p.source && out[*count-1].offset+out[*count-1].length==p.offset) out[*count-1].length+=p.length;else out[(*count)++]=p; }
int insight_stream_replace(InsightStream *s,uint64_t at,uint64_t remove,unsigned source,uint64_t offset,uint64_t length,const void *bytes) {
 if(!s->version_count || !stable(s)) return 0;Version *v=&s->versions[s->position];if(at>v->size || remove>v->size-at || source>2 || length>INT64_MAX-(v->size-remove) || (source<2 && (offset>s->sizes[source] || length>s->sizes[source]-offset)) || (source==2 && length && !bytes)) return fail(s,"Replacement outside file or supported 64-bit range.");
 if(v->count>(SIZE_MAX/sizeof(Piece)-3)/2) return fail(s,"Too many pieces.");Piece *out=malloc((2*v->count+3)*sizeof(Piece));if(!out) return fail(s,"Out of memory for pieces.");
 if(source==2 && length) {
  if(length>SIZE_MAX || length>INT64_MAX-s->manual_size) { free(out);return fail(s,"Manual data too large."); }offset=s->manual_size;
  if(s->manual || s->manual_size+length>SPILL) { if(!s->manual && s->manual_size && !append_file(s,&s->manual,s->manual_memory,(size_t)s->manual_size,NULL)) { free(out);return 0; }if(!append_file(s,&s->manual,bytes,(size_t)length,NULL)) { free(out);return 0; }free(s->manual_memory);s->manual_memory=NULL; }
  else { void *m=realloc(s->manual_memory,(size_t)(s->manual_size+length));if(!m) { free(out);return fail(s,"Out of memory for manual bytes."); }s->manual_memory=m;memcpy(s->manual_memory+s->manual_size,bytes,(size_t)length); }s->manual_size+=length;
 }
 size_t count=0;uint64_t begin=0;int inserted=0;
 for(size_t i=0;i<v->count;i++) { Piece p;if(!piece_get(s,v,i,&p)) { free(out);return 0; }uint64_t end=begin+p.length;
  if(begin<at) { Piece prefix=p;prefix.length=(end<at ? end:at)-begin;push_piece(out,&count,prefix); }
  if(!inserted && end>=at) { push_piece(out,&count,(Piece){offset,length,source});inserted=1; }
  if(end>at+remove) { uint64_t cut=begin>at+remove ? begin:at+remove;Piece suffix=p;suffix.offset+=cut-begin;suffix.length=end-cut;push_piece(out,&count,suffix); }begin=end;
 }
 if(!inserted) push_piece(out,&count,(Piece){offset,length,source});uint64_t new_size=v->size-remove+length;
 /* Allocate before truncating redo so failure leaves the current table intact. */
 size_t capacity=s->version_count>s->position+2 ? s->version_count:s->position+2;
 Version *versions=realloc(s->versions,capacity*sizeof(Version));
 if(!versions) { free(out);return fail(s,"Out of memory for history."); }s->versions=versions;
 for(size_t i=s->position+1;i<s->version_count;i++) { if(s->versions[i].pieces) s->resident-=s->versions[i].count*sizeof(Piece);free(s->versions[i].pieces); }
 s->versions[s->position+1]=(Version){out,count,0,new_size,{at,remove,offset,length,source}};s->version_count=s->position+2;s->position++;s->resident+=count*sizeof(Piece);return spill_versions(s);
}
static uint64_t moved(uint64_t p,InsightStreamEvent e,int end) {
 if(p<e.target) return p;if(p>e.target+e.removed || (p==e.target+e.removed && e.removed)) return p-e.removed+e.length;return e.target+(end ? e.length:0);
}
int insight_stream_map_range(InsightStream *s,const InsightStreamRange *range,uint64_t *at,uint64_t *length) {
 uint64_t begin=range->offset<s->sizes[0] ? range->offset:s->sizes[0],end=begin+(range->length<s->sizes[0]-begin ? range->length:s->sizes[0]-begin);int status=0;
 for(size_t i=1;i<=s->position;i++) { InsightStreamEvent e=s->versions[i].event;
  int touches=begin==end ? e.target<=begin && e.target+e.removed>=begin:e.target<end && e.target+e.removed>begin;
  if(!e.removed && e.length && e.target>=begin && e.target<=end) touches=1;
  if(touches) { uint64_t source_at=e.source ? range->right_offset:range->offset,source_len=e.source ? range->right_length:range->length;
   status=e.source==2 ? 3:(e.target<=begin && e.target+e.removed>=end && e.offset<=source_at && e.length>=source_at-e.offset+source_len ? (int)e.source+1:4);
  }
  begin=moved(begin,e,0);end=moved(end,e,1);if(end<begin) end=begin;
 }
 *at=begin;*length=end-begin;return status;
}
int insight_stream_event(InsightStream *s,size_t index,InsightStreamEvent *event) { if(index>=s->version_count) return 0;*event=s->versions[index].event;return 1; }
size_t insight_stream_history_count(InsightStream *s) { return s->version_count; }
size_t insight_stream_history_position(InsightStream *s) { return s->position; }
int insight_stream_jump(InsightStream *s,size_t position) { if(position>=s->version_count) return fail(s,"History event no longer exists.");s->position=position;return 1; }
static int append_range(InsightStream *s,InsightStreamRange r) {if(s->ranges_file || (s->range_count+1)*sizeof(r)>SPILL) {
  if(!s->ranges_file && s->range_count && !append_file(s,&s->ranges_file,s->ranges,s->range_count*sizeof(r),NULL)) return 0;
  if(!append_file(s,&s->ranges_file,&r,sizeof(r),NULL)) return 0;free(s->ranges);s->ranges=NULL;
 }else { void *p=realloc(s->ranges,(s->range_count+1)*sizeof(r));if(!p) return fail(s,"Out of memory for difference ranges.");s->ranges=p;s->ranges[s->range_count]=r; }s->range_count++;return 1;
}
static int add_range(InsightStream *s,uint64_t begin,uint64_t end) {
 uint64_t a=begin<s->sizes[0] ? begin:s->sizes[0],b=begin<s->sizes[1] ? begin:s->sizes[1];
 (void)a;return append_range(s,(InsightStreamRange){begin,end-begin,b,(end<s->sizes[1] ? end:s->sizes[1])-b});
}
int insight_stream_scan(InsightStream *s,size_t budget) {
 if(!stable(s)) return -1;if(s->scan_done) return 1;uint64_t end=s->sizes[0]>s->sizes[1] ? s->sizes[0]:s->sizes[1];unsigned char a[BLOCK],b[BLOCK];
 while(s->scanned<end && budget) { size_t n=(size_t)((end-s->scanned)<BLOCK ? end-s->scanned:BLOCK);if(n>budget) n=budget;
  size_t lengths[2];for(unsigned side=0;side<2;side++) { lengths[side]=(size_t)(s->scanned<s->sizes[side] ? ((s->sizes[side]-s->scanned)<n ? s->sizes[side]-s->scanned:n):0);if(lengths[side] && !data_read(s,side,s->scanned,side ? b:a,lengths[side])) return -1; }
  for(size_t i=0;i<n;i++) { int different=i>=lengths[0] || i>=lengths[1] || a[i]!=b[i];if(different && !s->scanning_run) { s->run_start=s->scanned+i;s->scanning_run=1; }else if(!different && s->scanning_run) { if(!add_range(s,s->run_start,s->scanned+i)) return -1;s->scanning_run=0; } }s->scanned+=n;budget-=n;
 }
 if(s->scanned==end) { if(s->scanning_run && !add_range(s,s->run_start,end)) return -1;s->scanning_run=0;s->scan_done=1; }return s->scan_done;
}
/* Bounded line-start anchors resynchronise insertions/deletions without indexing
 * every line. This is a streaming heuristic, not a minimum edit script. */
static unsigned anchor_hash(const unsigned char *p) { unsigned h=2166136261u;for(unsigned i=0;i<32;i++) h=(h^p[i])*16777619u;return h&8191u; }
int insight_stream_scan_text(InsightStream *s,size_t budget) {
 if(!stable(s)) return -1;if(s->scan_done) return 1;
 unsigned char a[BLOCK],b[BLOCK];int table[8192];
 do {
  size_t an=(size_t)((s->sizes[0]-s->text_at[0])<BLOCK ? s->sizes[0]-s->text_at[0]:BLOCK),bn=(size_t)((s->sizes[1]-s->text_at[1])<BLOCK ? s->sizes[1]-s->text_at[1]:BLOCK);
  if(!an && !bn) { s->scan_done=1;return 1; }
  if((an && !data_read(s,0,s->text_at[0],a,an)) || (bn && !data_read(s,1,s->text_at[1],b,bn))) return -1;
  size_t common=0;while(common<an && common<bn && a[common]==b[common]) common++;
  s->text_at[0]+=common;s->text_at[1]+=common;
  if(common==an || common==bn) {
   if((!an || !bn) && common==0) { /* A final unmatched tail, bounded by one block. */
    if(!append_range(s,(InsightStreamRange){s->text_at[0],an,s->text_at[1],bn})) return -1;s->text_at[0]+=an;s->text_at[1]+=bn;
   }
  }else {
   memset(table,0xff,sizeof(table));for(size_t j=common+1;j+32<=bn;j++) if(b[j-1]=='\n') { unsigned h=anchor_hash(b+j);if(table[h]<0) table[h]=(int)j; }
   size_t best_a=an,best_b=bn,best=SIZE_MAX;
   for(size_t i=common+1;i+32<=an;i++) if(a[i-1]=='\n') { int j=table[anchor_hash(a+i)];if(j>=0 && !memcmp(a+i,b+j,32) && i+(size_t)j<best) { best=i+j;best_a=i;best_b=j; } }
   /* Also test the current right/left line start for a whole inserted line. */
   if(bn-common>=32) for(size_t i=common+1;i+32<=an;i++) if(a[i-1]=='\n' && !memcmp(a+i,b+common,32) && i+common<best) { best=i+common;best_a=i;best_b=common; }
   if(an-common>=32) for(size_t j=common+1;j+32<=bn;j++) if(b[j-1]=='\n' && !memcmp(a+common,b+j,32) && j+common<best) { best=j+common;best_a=common;best_b=j; }
   size_t al=best_a-common,bl=best_b-common;if(!append_range(s,(InsightStreamRange){s->text_at[0],al,s->text_at[1],bl})) return -1;s->text_at[0]+=al;s->text_at[1]+=bl;
  }
  s->scanned=s->text_at[0]>s->text_at[1] ? s->text_at[0]:s->text_at[1];size_t used=an>bn ? an:bn;if(used>=budget) break;budget-=used;
 }while(budget);
 return 0;
}
uint64_t insight_stream_scanned(InsightStream *s) { return s->scanned; }
size_t insight_stream_difference_count(InsightStream *s) { return s->range_count; }
int insight_stream_difference(InsightStream *s,size_t index,InsightStreamRange *range) { if(index>=s->range_count) return fail(s,"Difference index outside range.");if(s->ranges_file) return read_at(s,s->ranges_file,index*sizeof(*range),range,sizeof(*range));*range=s->ranges[index];return 1; }
int insight_stream_write(InsightStream *s,FILE *output) { unsigned char bytes[BLOCK];uint64_t total=insight_stream_size(s,2);for(uint64_t at=0;at<total;) { size_t n=(size_t)((total-at)<BLOCK ? total-at:BLOCK);if(!insight_stream_read(s,2,at,bytes,n) || fwrite(bytes,1,n,output)!=n) return fail(s,"Cannot stream result (source changed or output disk full).");at+=n; }return fflush(output)==0 ? 1:fail(s,"Cannot flush result."); }
size_t insight_stream_resident_metadata(InsightStream *s) { return s->resident+(s->manual ? 0:(size_t)s->manual_size)+(s->ranges_file ? 0:s->range_count*sizeof(InsightStreamRange)); }

int insight_stream_write_base64(InsightStream *s,FILE *output) {
 static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";unsigned char input[49152];char encoded[65536];uint64_t total=insight_stream_size(s,2);
 for(uint64_t at=0;at<total;) { size_t n=(size_t)((total-at)<sizeof(input) ? total-at:sizeof(input));if(!insight_stream_read(s,2,at,input,n)) return 0;size_t used=0;
  for(size_t i=0;i<n;i+=3) { unsigned x=(unsigned)input[i]<<16;if(i+1<n) x|=(unsigned)input[i+1]<<8;if(i+2<n) x|=input[i+2];encoded[used++]=alphabet[x>>18];encoded[used++]=alphabet[(x>>12)&63];encoded[used++]=i+1<n ? alphabet[(x>>6)&63]:'=';encoded[used++]=i+2<n ? alphabet[x&63]:'='; }
  if(fwrite(encoded,1,used,output)!=used) return fail(s,"Cannot write Base64 output.");at+=n;
 }return fflush(output)==0 ? 1:fail(s,"Cannot flush Base64 output.");
}
