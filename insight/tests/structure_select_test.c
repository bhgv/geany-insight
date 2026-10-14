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
static void calls_test(void) {
 const char *s="if (ready()) obj.send (x); sizeof (x); foo\n(bar());";
 size_t cursor=0,a=0,b=0;const char *names[]={"ready","send","foo","bar"};
 for(size_t i=0;i<4;i++) { assert(insight_next_call(s,strlen(s),&cursor,&a,&b)==1);assert(b-a==strlen(names[i]));assert(!memcmp(s+a,names[i],b-a)); }
 assert(insight_next_call(s,strlen(s),&cursor,&a,&b)==0);
 assert(insight_next_call(s,strlen(s),NULL,&a,&b)==-1);
 s="MACRO(x); café(); f(); f();";cursor=0;
 const char *extra[]={"MACRO","café","f","f"};
 for(size_t i=0;i<4;i++) { assert(insight_next_call(s,strlen(s),&cursor,&a,&b)==1);assert(b-a==strlen(extra[i]));assert(!memcmp(s+a,extra[i],b-a)); }
 assert(insight_next_call(s,strlen(s),&cursor,&a,&b)==0);
 s="      hidden        visible (x)";cursor=0;
 assert(insight_next_call(s,strlen(s),&cursor,&a,&b)==1);assert(b-a==7 && !memcmp(s+a,"visible",7));
 cursor=strlen(s)+1;assert(insight_next_call(s,strlen(s),&cursor,&a,&b)==-1);
}
int main(void) {
 calls_test();
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
 const char *function="int f(int x) { if(x) { return x; } return 0; }\nint g();";
 assert(insight_function_extent(function,strlen(function),0,&z)==1 && function[z-1]=='}' && function[z]=='\n');
 assert(insight_function_extent("int f();",8,0,&z)==0);
 const char *constructor="C::C() : a{1}, b(2) { body(); }";
 assert(insight_function_extent(constructor,strlen(constructor),0,&z)==1 && z==strlen(constructor));
 const char *requires="void f() requires requires { check(); } { body(); }";
 assert(insight_function_extent(requires,strlen(requires),0,&z)==1 && z==strlen(requires));
 const char *expression="int f() => new X { 1 };";
 assert(insight_function_extent(expression,strlen(expression),0,&z)==0);
 assert(insight_function_extent("int f() {",9,0,&z)==0);
 const size_t positions[]={50,10,50,30};
 assert(insight_neighbor_position(positions,4,20,1,&z)==1 && z==30);
 assert(insight_neighbor_position(positions,4,20,-1,&z)==1 && z==10);
 assert(insight_neighbor_position(positions,4,50,-1,&z)==1 && z==30);
 assert(insight_neighbor_position(positions,4,50,1,&z)==0);
 assert(insight_neighbor_position(NULL,0,0,1,&z)==0);
 assert(insight_neighbor_position(positions,4,0,0,&z)==-1);
 puts("Structural selection: nested expansion, matching, UTF-8 offsets and malformed input passed.");
 return 0;
}
