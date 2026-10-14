/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../core/structure_select.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void block(const char *s,size_t begin,size_t end,size_t a,size_t z) {
 size_t first=999,last=999;
 assert(insight_select_block(s,strlen(s),begin,end,&first,&last)==1);
 assert(first==a && last==z);
}
int main(void) {
 block("{ f(a[2]); }",6,6,5,8);
 block("{ f(a[2]); }",5,8,3,9);
 block("{ f(a[2]); }",3,9,0,12);
 block("{ (a) + (b) }",3,10,0,13);
 block("я(x)",3,3,2,5); /* Scintilla and the mask both use UTF-8 byte offsets. */
 block("{              (x) }",16,16,15,18); /* Masked content retains offsets. */
 size_t a=0,z=0;
 assert(insight_select_block("(x)",3,0,3,&a,&z)==0);
 assert(insight_select_block("",0,0,0,&a,&z)==0);
 assert(insight_select_block("([)]",4,2,2,&a,&z)==0);
 assert(insight_select_match("(x)",3,0,&a,&z)==1 && a==0 && z==3);
 assert(insight_select_match("(x)",3,1,&a,&z)==1 && a==0 && z==3);
 assert(insight_select_match("(x)",3,2,&a,&z)==1 && a==0 && z==3);
 assert(insight_select_match("(x)",3,3,&a,&z)==1 && a==0 && z==3);
 assert(insight_select_match("x",1,0,&a,&z)==0);
 assert(insight_select_block(NULL,0,0,0,&a,&z)==-1);
 assert(insight_select_block("(x)",3,2,1,&a,&z)==-1);
 assert(insight_select_match("(x)",3,4,&a,&z)==-1);
 for(size_t depth=1;depth<=256;depth++) {
  char text[514];const char *open="([{";
  for(size_t i=0;i<depth;i++) text[i]=open[(i*7+depth)%3];
  text[depth]='x';
  for(size_t i=0;i<depth;i++) text[2*depth-i]=text[i]=='(' ? ')' : (text[i]=='[' ? ']' : '}');
  text[2*depth+1]=0;
  a=z=depth;
  for(size_t i=0;i<depth;i++) {
   size_t first=0,last=0;
   assert(insight_select_block(text,2*depth+1,a,z,&first,&last)==1);
   assert(first==depth-1-i && last==depth+2+i);a=first;z=last;
  }
  assert(insight_select_block(text,2*depth+1,a,z,&a,&z)==0);
  for(size_t i=0;i<depth;i++) {
   assert(insight_select_match(text,2*depth+1,i,&a,&z)==1 && a==i && z==2*depth+1-i);
   assert(insight_select_match(text,2*depth+1,2*depth-i,&a,&z)==1 && a==i && z==2*depth+1-i);
  }
 }
 puts("Structural selection: nested expansion, matching, UTF-8 offsets and malformed input passed.");
 return 0;
}
