/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MEMORY_LIMIT (16u * 1024u * 1024u)
typedef struct { size_t used; unsigned ticks; const InsightHost *host; } Runtime;
static void *allocate(void *ud, void *ptr, size_t old, size_t size) {
 Runtime *r=ud;
 if (!ptr) old=0;
 if (!size) { free(ptr); r->used-=old; return NULL; }
 if (size > MEMORY_LIMIT || r->used-old > MEMORY_LIMIT-size) return NULL;
 void *next=realloc(ptr,size);
 if (next) r->used=r->used-old+size;
 return next;
}
static Runtime *runtime(lua_State *L) {
 void *ud; lua_getallocf(L,&ud); return ud;
}
static void budget(lua_State *L, lua_Debug *ar) {
 (void)ar;
 if (++runtime(L)->ticks > 1000) luaL_error(L,"instruction budget exceeded");
}
static int text(lua_State *L) {
 const InsightHost *h=runtime(L)->host;
 const char *s=h->text(h->context);
 if (!s) return luaL_error(L,"no active document");
 lua_pushstring(L,s); return 1;
}
static int selection(lua_State *L) {
 const InsightHost *h=runtime(L)->host;
 const char *s=h->selection(h->context);
 if (!s) return luaL_error(L,"no active document");
 lua_pushstring(L,s); return 1;
}
static int replace(lua_State *L) {
 size_t n; const char *s=luaL_checklstring(L,1,&n);
 if (memchr(s,0,n)) return luaL_error(L,"embedded NUL is unsupported");
 const InsightHost *h=runtime(L)->host;
 if (!h->replace_selection(h->context,s)) return luaL_error(L,"replacement failed");
 return 0;
}
static int symbols(lua_State *L) {
 size_t length; const char *query=luaL_checklstring(L,1,&length);
 if(memchr(query,0,length)) return luaL_error(L,"embedded NUL is unsupported");
 const InsightIndex *index=runtime(L)->host->index;
 if(!index) return luaL_error(L,"project index unavailable");
 int exact=lua_toboolean(L,2);
 const InsightSymbol *found[256];
 size_t total=insight_index_find(index,query,exact,found,256);
 size_t count=total<256 ? total : 256;
 lua_createtable(L,(int)count,0);
 for(size_t n=0;n<count;n++) {
  lua_createtable(L,0,5);
  lua_pushstring(L,found[n]->name); lua_setfield(L,-2,"name");
  lua_pushstring(L,found[n]->path); lua_setfield(L,-2,"path");
  lua_pushstring(L,found[n]->scope); lua_setfield(L,-2,"scope");
  lua_pushstring(L,found[n]->kind); lua_setfield(L,-2,"kind");
  lua_pushinteger(L,(lua_Integer)found[n]->line); lua_setfield(L,-2,"line");
  lua_rawseti(L,-2,(lua_Integer)n+1);
 }
 lua_pushinteger(L,(lua_Integer)total); return 2;
}
static int setup(lua_State *L) {
 luaL_requiref(L,"_G",luaopen_base,1); lua_pop(L,1);
 luaL_requiref(L,"string",luaopen_string,1); lua_pop(L,1);
 luaL_requiref(L,"table",luaopen_table,1); lua_pop(L,1);
 luaL_requiref(L,"math",luaopen_math,1); lua_pop(L,1);
 luaL_requiref(L,"utf8",luaopen_utf8,1); lua_pop(L,1);
 const char *blocked[]={"dofile","loadfile","load","collectgarbage","print","pcall","xpcall",NULL};
 for (int i=0;blocked[i];i++) { lua_pushnil(L); lua_setglobal(L,blocked[i]); }
 lua_newtable(L);
 lua_pushcfunction(L,text); lua_setfield(L,-2,"text");
 lua_pushcfunction(L,selection); lua_setfield(L,-2,"selection");
 lua_pushcfunction(L,replace); lua_setfield(L,-2,"replace_selection");
 lua_setglobal(L,"editor");
 lua_newtable(L); lua_pushcfunction(L,symbols); lua_setfield(L,-2,"symbols");
 lua_setglobal(L,"project"); return 0;
}
int insight_run(const InsightHost *host,const char *source,size_t length,char *error,size_t capacity) {
 if (error && capacity) error[0]=0;
 if (!host || !host->text || !host->selection || !host->replace_selection || !source) return 2;
 Runtime r={0,0,host}; lua_State *L=lua_newstate(allocate,&r);
 if (!L) return 2;
 lua_pushcfunction(L,setup);
 int status=lua_pcall(L,0,0,0);
 lua_sethook(L,budget,LUA_MASKCOUNT,1000);
 if (!status) status=luaL_loadbufferx(L,source,length,"script","t");
 if (!status) status=lua_pcall(L,0,0,0);
 if (status && error && capacity) {
  const char *message=lua_type(L,-1)==LUA_TSTRING ? lua_tostring(L,-1) : "Lua error (non-string value)";
  snprintf(error,capacity,"%s",message);
 }
 lua_close(L); return status ? 1 : 0;
}
