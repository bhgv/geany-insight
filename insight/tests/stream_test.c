/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L
#include "../../src/insight-stream.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
static void sparse(const char *path,uint64_t size) { FILE *f=fopen(path,"wb");assert(f);assert(fseeko(f,(off_t)(size-1),SEEK_SET)==0);assert(fputc(0,f)==0);assert(!fclose(f)); }
int main(void) {
 char a[]="./insight-stream-a-XXXXXX",b[]="./insight-stream-b-XXXXXX";int fd=mkstemp(a);assert(fd>=0);close(fd);fd=mkstemp(b);assert(fd>=0);close(fd);
 uint64_t size=130u*1024u*1024u;sparse(a,size);sparse(b,size+5);FILE *f=fopen(b,"r+b");assert(f);assert(!fseeko(f,(off_t)(size-3),SEEK_SET));assert(fwrite("ABCDEFGH",1,8,f)==8);fclose(f);
 InsightStream *s=insight_stream_open(a,b);assert(s && !*insight_stream_error(s));assert(insight_stream_size(s,0)==size);int done;do { done=insight_stream_scan(s,1024*1024);assert(done>=0); }while(!done);
 assert(insight_stream_difference_count(s)==1);InsightStreamRange r;assert(insight_stream_difference(s,0,&r));assert(r.offset==size-3 && r.length==8);
 assert(insight_stream_replace(s,r.offset,3,1,r.offset,r.length,NULL));assert(insight_stream_size(s,2)==size+5);uint64_t mapped_at,mapped_length;assert(insight_stream_map_range(s,&r,&mapped_at,&mapped_length)==2);assert(mapped_at==size-3 && mapped_length==8);char out[9]={0};assert(insight_stream_read(s,2,size-3,out,8));assert(!strcmp(out,"ABCDEFGH"));assert(insight_stream_resident_metadata(s)<4096);
 assert(insight_stream_jump(s,0));assert(insight_stream_size(s,2)==size);assert(insight_stream_map_range(s,&r,&mapped_at,&mapped_length)==0 && mapped_length==3);assert(insight_stream_jump(s,1));assert(insight_stream_size(s,2)==size+5);
 /* Manual payload crosses spill threshold; undo/branch must preserve referenced data. */
 unsigned char *manual=malloc(3*1024*1024);assert(manual);memset(manual,0x5a,3*1024*1024);assert(insight_stream_replace(s,7,10,2,0,3*1024*1024,manual));free(manual);assert(insight_stream_resident_metadata(s)<4096);unsigned char byte=0;assert(insight_stream_read(s,2,100000,&byte,1) && byte==0x5a);
 assert(insight_stream_jump(s,1));assert(insight_stream_replace(s,0,1,2,0,1,"X"));assert(insight_stream_history_count(s)==3);assert(insight_stream_read(s,2,0,&byte,1) && byte=='X');
 f=fopen("insight-stream-output.tmp","w+b");assert(f && insight_stream_write(s,f));assert(!fseeko(f,(off_t)(size-3),SEEK_SET));assert(fread(out,1,8,f)==8 && !memcmp(out,"ABCDEFGH",8));fclose(f);unlink("insight-stream-output.tmp");insight_stream_free(s);
 /* Alternating differences force range metadata to spill; lookup remains random access. */
 f=fopen(a,"wb");assert(f);fclose(f);f=fopen(b,"wb");assert(f);for(unsigned i=0;i<300000;i++) fputc(i%2,f);fclose(f);s=insight_stream_open(b,b);assert(insight_stream_replace(s,0,300000,0,0,0,NULL));insight_stream_free(s);
 f=fopen(a,"wb");assert(f);for(unsigned i=0;i<300000;i++) fputc(0,f);fclose(f);s=insight_stream_open(a,b);do { done=insight_stream_scan(s,32768);assert(done>=0); }while(!done);assert(insight_stream_difference_count(s)==150000);assert(insight_stream_resident_metadata(s)<4096);assert(insight_stream_difference(s,149999,&r));assert(r.offset==299999 && r.length==1);
 /* Large UTF-8 text with an inserted line: bounded line anchors resynchronise. */
 insight_stream_free(s);f=fopen(a,"wb");assert(f);FILE *right=fopen(b,"wb");assert(right);char *block=malloc(1024*1024);assert(block);memset(block,'x',1024*1024);for(unsigned i=79;i<1024*1024;i+=80) block[i]='\n';
 for(unsigned i=0;i<110;i++) { assert(fwrite(block,1,1024*1024,f)==1024*1024);if(i==50) assert(fwrite("inserted line\n",1,14,right)==14);assert(fwrite(block,1,1024*1024,right)==1024*1024); }free(block);fclose(f);fclose(right);
 s=insight_stream_open(a,b);do { done=insight_stream_scan_text(s,1024*1024);assert(done>=0); }while(!done);assert(insight_stream_difference_count(s)==1);assert(insight_stream_difference(s,0,&r));assert(r.offset==50u*1024u*1024u && r.length==0 && r.right_length==14);assert(insight_stream_replace(s,r.offset,r.length,1,r.right_offset,r.right_length,NULL));assert(insight_stream_size(s,2)==110u*1024u*1024u+14);assert(insight_stream_resident_metadata(s)<4096);
 /* Same-length external changes must not silently produce a mixed result. */
 f=fopen(b,"ab");assert(f);fputc(1,f);fclose(f);assert(!insight_stream_read(s,0,0,&byte,1));insight_stream_free(s);/* Deterministic edit oracle exercises piece splits, coalescing and spilled versions. */
 f=fopen(a,"wb");assert(f);for(unsigned i=0;i<64;i++) fputc(i,f);fclose(f);f=fopen(b,"wb");assert(f);for(unsigned i=0;i<64;i++) fputc(255-i,f);fclose(f);s=insight_stream_open(a,b);unsigned char model[8192],actual[8192];size_t model_size=64;for(unsigned i=0;i<64;i++) model[i]=i;
 for(unsigned i=0;i<1200;i++) { size_t at=(i*7919u)%(model_size+1),remove=(i*13u)%7;if(remove>model_size-at) remove=model_size-at;size_t n=i%11;unsigned char insert[11];unsigned side=i%3;size_t source_at=(i*17u)%53;for(size_t j=0;j<n;j++) insert[j]=side==0 ? source_at+j:side==1 ? 255-source_at-j:(i+j)%256;
  assert(insight_stream_replace(s,at,remove,side,source_at,n,side==2 ? insert:NULL));memmove(model+at+n,model+at+remove,model_size-at-remove);memcpy(model+at,insert,n);model_size=model_size-remove+n;assert(model_size<sizeof(model));assert(insight_stream_size(s,2)==model_size);assert(insight_stream_read(s,2,0,actual,model_size));assert(!memcmp(model,actual,model_size)); }
 assert(insight_stream_resident_metadata(s)<5u*1024u*1024u);assert(insight_stream_jump(s,17));assert(insight_stream_jump(s,1200));assert(insight_stream_read(s,2,0,actual,model_size) && !memcmp(model,actual,model_size));insight_stream_free(s);
 /* Base64 padding and block-boundary continuation. */
 f=fopen(a,"wb");assert(f);for(unsigned i=0;i<49153;i++) fputc('A',f);fclose(f);s=insight_stream_open(a,a);f=fopen("insight-stream-output.tmp","w+b");assert(f && insight_stream_write_base64(s,f));assert(!fseeko(f,0,SEEK_END));assert(ftello(f)==65540);assert(!fseeko(f,65532,SEEK_SET));char encoded[9]={0};assert(fread(encoded,1,8,f)==8);assert(!strcmp(encoded,"QUFBQQ=="));fclose(f);unlink("insight-stream-output.tmp");insight_stream_free(s);
 unlink(a);unlink(b);puts("stream: 130 MiB binary / 110 MiB text sources, line resynchronisation, sparse differences, piece merge, manual spill, history branching, streaming save and 150000 spilled ranges passed");return 0;
}
