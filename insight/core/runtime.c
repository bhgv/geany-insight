/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define MEMORY_LIMIT (16u * 1024u * 1024u)
#define COMMAND_LIMIT 128u
typedef struct { char name[64], label[256]; int reference; } Command;
typedef struct {
 size_t used; unsigned ticks; const InsightHost *host;
 lua_State *state;
 Command commands[COMMAND_LIMIT]; size_t command_count;
 int registering;
} Runtime;
struct InsightSession { Runtime *current; };
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
static const InsightHost *active_host(lua_State *L) {
 const InsightHost *host=runtime(L)->host;
 if(!host) luaL_error(L,"editor/project API unavailable during extension load");
 return host;
}
static int text(lua_State *L) {
 const InsightHost *h=active_host(L);
 const char *s=h->text(h->context);
 if (!s) return luaL_error(L,"no active document");
 lua_pushstring(L,s); return 1;
}
static int selection(lua_State *L) {
 const InsightHost *h=active_host(L);
 const char *s=h->selection(h->context);
 if (!s) return luaL_error(L,"no active document");
 lua_pushstring(L,s); return 1;
}
static int replace(lua_State *L) {
 size_t n; const char *s=luaL_checklstring(L,1,&n);
 if (memchr(s,0,n)) return luaL_error(L,"embedded NUL is unsupported");
 const InsightHost *h=active_host(L);
 if (!h->replace_selection(h->context,s)) return luaL_error(L,"replacement failed");
 return 0;
}
static int symbols(lua_State *L) {
 size_t length; const char *query=luaL_checklstring(L,1,&length);
 if(memchr(query,0,length)) return luaL_error(L,"embedded NUL is unsupported");
 const InsightIndex *index=active_host(L)->index;
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
static int register_command(lua_State *L) {
 Runtime *r=runtime(L);
 if(!r->registering) return luaL_error(L,"commands can only be registered during extension load");
 size_t name_length,label_length;
 const char *name=luaL_checklstring(L,1,&name_length);
 const char *label=luaL_checklstring(L,2,&label_length);
 luaL_checktype(L,3,LUA_TFUNCTION);
 if(!name_length || name_length>=sizeof r->commands[0].name || memchr(name,0,name_length))
  return luaL_error(L,"invalid command ID (maximum 63 ASCII characters)");
 for(size_t n=0;n<name_length;n++) {
  unsigned char c=(unsigned char)name[n];
  int letter=(c>='a' && c<='z') || (c>='A' && c<='Z');
  if(!letter && (n==0 || !((c>='0' && c<='9') || c=='_' || c=='.' || c=='-')))
   return luaL_error(L,"invalid command ID");
 }
 if(!label_length || label_length>=sizeof r->commands[0].label || memchr(label,0,label_length))
  return luaL_error(L,"invalid command label (maximum 255 bytes)");
 for(size_t n=0;n<r->command_count;n++)
  if(!strcmp(name,r->commands[n].name)) return luaL_error(L,"duplicate command ID");
 if(r->command_count==COMMAND_LIMIT) return luaL_error(L,"command limit exceeded");
 Command *command=&r->commands[r->command_count];
 memcpy(command->name,name,name_length+1); memcpy(command->label,label,label_length+1);
 lua_pushvalue(L,3); command->reference=luaL_ref(L,LUA_REGISTRYINDEX);
 r->command_count++; return 0;
}
/* Lua disables instruction hooks inside __gc. Prevent registration of script
 * finalizers so an extension cannot hang the editor during GC or unloading. */
static int safe_setmetatable(lua_State *L) {
 if(lua_type(L,2)==LUA_TTABLE) {
  lua_pushliteral(L,"__gc"); lua_rawget(L,2);
  int finalizer=!lua_isnil(L,-1); lua_pop(L,1);
  if(finalizer) return luaL_error(L,"Lua GC finalizers are unavailable");
 }
 int arguments=lua_gettop(L);
 lua_pushvalue(L,lua_upvalueindex(1)); lua_insert(L,1);
 lua_call(L,arguments,1); return 1;
}
static int setup(lua_State *L) {
 luaL_requiref(L,"_G",luaopen_base,1); lua_pop(L,1);
 lua_getglobal(L,"setmetatable"); lua_pushcclosure(L,safe_setmetatable,1);
 lua_setglobal(L,"setmetatable");
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
 lua_setglobal(L,"project");
 lua_newtable(L); lua_pushcfunction(L,register_command); lua_setfield(L,-2,"register");
 lua_setglobal(L,"commands");
 lua_pushliteral(L,"0.5"); lua_setglobal(L,"INSIGHT_API_VERSION"); return 0;
}
static void report(lua_State *L,int status,char *error,size_t capacity) {
 if(status && error && capacity) {
  const char *message=lua_type(L,-1)==LUA_TSTRING ? lua_tostring(L,-1) : "Lua error (non-string value)";
  snprintf(error,capacity,"%s",message);
 }
}
static int valid_host(const InsightHost *host) {
 return host && host->text && host->selection && host->replace_selection;
}
static int initialize(Runtime *r,char *error,size_t capacity) {
 r->state=lua_newstate(allocate,r); if(!r->state) return 2;
 lua_pushcfunction(r->state,setup);
 int status=lua_pcall(r->state,0,0,0);
 report(r->state,status,error,capacity);
 lua_settop(r->state,0); return status ? 1 : 0;
}
static int execute(Runtime *r,const char *source,size_t length,char *error,size_t capacity) {
 lua_State *L=r->state; r->ticks=0; lua_sethook(L,budget,LUA_MASKCOUNT,1000);
 int status=luaL_loadbufferx(L,source,length,"script","t");
 if(!status) status=lua_pcall(L,0,0,0);
 report(L,status,error,capacity); lua_settop(L,0); return status ? 1 : 0;
}
int insight_run(const InsightHost *host,const char *source,size_t length,char *error,size_t capacity) {
 if(error && capacity) error[0]=0;
 if(!valid_host(host) || !source) return 2;
 Runtime r={0}; r.host=host;
 int status=initialize(&r,error,capacity);
 if(!status) status=execute(&r,source,length,error,capacity);
 r.host=NULL;
 if(r.state) lua_close(r.state);
 return status;
}
InsightSession *insight_session_new(void) { return calloc(1,sizeof(InsightSession)); }
static void destroy(Runtime *r) {
 if(!r) return;
 if(r->state) lua_close(r->state);
 free(r);
}
void insight_session_free(InsightSession *s) {
 if(!s) return;
 destroy(s->current); free(s);
}
int insight_session_load(InsightSession *s,const char *source,size_t length,char *error,size_t capacity) {
 if(error && capacity) error[0]=0;
 if(!s || !source || (s->current && s->current->host)) return 2;
 Runtime *next=calloc(1,sizeof *next); if(!next) return 2;
 next->registering=1;
 int status=initialize(next,error,capacity);
 if(!status) status=execute(next,source,length,error,capacity);
 next->registering=0;
 if(status) { destroy(next); return status; }
 destroy(s->current); s->current=next; return 0;
}
int insight_session_is_running(const InsightSession *s) {
 return s && s->current && s->current->host;
}
size_t insight_session_command_count(const InsightSession *s) {
 return s && s->current ? s->current->command_count : 0;
}
const char *insight_session_command_name(const InsightSession *s,size_t n) {
 return n<insight_session_command_count(s) ? s->current->commands[n].name : NULL;
}
const char *insight_session_command_label(const InsightSession *s,size_t n) {
 return n<insight_session_command_count(s) ? s->current->commands[n].label : NULL;
}
int insight_session_invoke(InsightSession *s,const char *name,const InsightHost *host,char *error,size_t capacity) {
 if(error && capacity) error[0]=0;
 if(!s || !name || !valid_host(host) || (s->current && s->current->host)) return 2;
 Runtime *r=s->current; size_t n=0;
 if(r) for(;n<r->command_count;n++) if(!strcmp(name,r->commands[n].name)) break;
 if(!r || n==r->command_count) {
  if(error && capacity) snprintf(error,capacity,"unknown Lua command");
  return 1;
 }
 r->host=host; r->ticks=0; lua_State *L=r->state;
 lua_sethook(L,budget,LUA_MASKCOUNT,1000);
 lua_rawgeti(L,LUA_REGISTRYINDEX,r->commands[n].reference);
 int status=lua_pcall(L,0,0,0);
 report(L,status,error,capacity); lua_settop(L,0); r->host=NULL;
 return status ? 1 : 0;
}
