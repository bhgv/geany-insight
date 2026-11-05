/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "runtime.h"
#include "occurrences.h"
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdarg.h>
#define MEMORY_LIMIT (16u * 1024u * 1024u)
#define COMMAND_LIMIT 128u
typedef struct { char name[64], label[256], shortcut[64]; int reference; } Command;
typedef struct {
 size_t used; unsigned ticks; const InsightHost *host;
 lua_State *state;
 Command commands[COMMAND_LIMIT]; size_t command_count;
 struct { char name[32]; int reference; } events[32]; size_t event_count;
 const char *event_name,*event_path; int event_reference;
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
int insight_lua_check(const char *source,size_t length,char *error,size_t capacity) {
 if(error && capacity) error[0]=0;
 if(!source) return 2;
 Runtime state={0};
 lua_State *L=lua_newstate(allocate,&state);
 if(!L) return 2;
 int status=luaL_loadbufferx(L,source,length,"@buffer","t");
 if(status && error && capacity) {
  const char *message=lua_tostring(L,-1);
  snprintf(error,capacity,"%s",message ? message:"Lua compiler failed");
 }
 lua_close(L);
 return status ? 1 : 0;
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
static const InsightHost *write_host(lua_State *L) {
 const InsightHost *host=active_host(L);
 if(host->read_only) luaL_error(L,"event document is read-only");
 return host;
}
static const InsightEditorAPI *editor_api(lua_State *L) {
 const InsightEditorAPI *api=active_host(L)->api;
 if(!api) luaL_error(L,"editor capability unavailable");
 return api;
}
static unsigned long integer_arg(lua_State *L,int arg,unsigned long minimum) {
 lua_Integer value=luaL_checkinteger(L,arg);
 if(value<0 || (lua_Unsigned)value>ULONG_MAX || (lua_Unsigned)value<minimum)
  luaL_error(L,"invalid editor position or line");
 return (unsigned long)value;
}
static int position(lua_State *L) {
 const InsightEditorAPI *api=editor_api(L);
 if(!api->position) return luaL_error(L,"position capability unavailable");
 lua_pushinteger(L,(lua_Integer)api->position(active_host(L)->context)); return 1;
}
static int goto_line(lua_State *L) {
 unsigned long line=integer_arg(L,1,1); const InsightEditorAPI *api=editor_api(L);
 if(!api->goto_line || !api->goto_line(write_host(L)->context,line)) return luaL_error(L,"cannot go to line");
 return 0;
}
static int open(lua_State *L) {
 size_t n; const char *path=luaL_checklstring(L,1,&n);
 if(!n || n>16384 || memchr(path,0,n)) return luaL_error(L,"invalid file path");
 const InsightEditorAPI *api=editor_api(L);
 if(!api->open_file || !api->open_file(write_host(L)->context,path)) return luaL_error(L,"cannot open document");
 return 0;
}
static int waveforms(lua_State *L) {
 const InsightEditorAPI *api=editor_api(L);
 if(!api->open_waveforms || !api->open_waveforms(write_host(L)->context))
  return luaL_error(L,"waveform viewer unavailable");
 return 0;
}
static int save(lua_State *L) {
 const InsightEditorAPI *api=editor_api(L);
 if(!api->save_file || !api->save_file(write_host(L)->context)) return luaL_error(L,"cannot save named document");
 return 0;
}
static int filename(lua_State *L) {
 const InsightEditorAPI *api=editor_api(L);
 if(!api->filename) return luaL_error(L,"filename capability unavailable");
 const char *name=api->filename(active_host(L)->context);
 if(name) lua_pushstring(L,name); else lua_pushnil(L); return 1;
}
static int selection_range(lua_State *L) {
 const InsightEditorAPI *api=editor_api(L); unsigned long start,end;
 if(!api->selection_range || !api->selection_range(active_host(L)->context,&start,&end)) return luaL_error(L,"selection range unavailable");
 lua_pushinteger(L,(lua_Integer)start); lua_pushinteger(L,(lua_Integer)end); return 2;
}
static int set_selection(lua_State *L) {
 unsigned long start=integer_arg(L,1,0),end=integer_arg(L,2,0); const InsightEditorAPI *api=editor_api(L);
 if(start>end || !api->set_selection || !api->set_selection(write_host(L)->context,start,end)) return luaL_error(L,"cannot set selection");
 return 0;
}
static int line_count(lua_State *L) {
 const InsightEditorAPI *api=editor_api(L);
 if(!api->line_count) return luaL_error(L,"line count capability unavailable");
 lua_pushinteger(L,(lua_Integer)api->line_count(active_host(L)->context)); return 1;
}
static int line_text(lua_State *L) {
 unsigned long line=integer_arg(L,1,1); const InsightEditorAPI *api=editor_api(L);
 if(!api->line_text) return luaL_error(L,"line text capability unavailable");
 const char *text=api->line_text(active_host(L)->context,line);
 if(!text) return luaL_error(L,"line is unavailable");
 lua_pushstring(L,text); return 1;
}
static int replace_range(lua_State *L) {
 unsigned long start=integer_arg(L,1,0),end=integer_arg(L,2,0); size_t n; const char *text=luaL_checklstring(L,3,&n);
 const InsightEditorAPI *api=editor_api(L);
 if(start>end || memchr(text,0,n) || !api->replace_range || !api->replace_range(write_host(L)->context,start,end,text)) return luaL_error(L,"cannot replace range");
 return 0;
}
static int insert(lua_State *L) {
 unsigned long at=integer_arg(L,1,0); size_t n; const char *text=luaL_checklstring(L,2,&n); const InsightEditorAPI *api=editor_api(L);
 if(memchr(text,0,n) || !api->insert || !api->insert(write_host(L)->context,at,text)) return luaL_error(L,"cannot insert text");
 return 0;
}
static int message(lua_State *L) {
 size_t n; const char *s=luaL_checklstring(L,1,&n); const InsightEditorAPI *api=editor_api(L);
 if(n>4096 || memchr(s,0,n) || !api->message || !api->message(active_host(L)->context,s)) return luaL_error(L,"cannot show message");
 return 0;
}
static int has(lua_State *L) {
 size_t length; const char *name=luaL_checklstring(L,1,&length);
 int available=0; const InsightHost *host=runtime(L)->host;
 if(memchr(name,0,length)) { lua_pushboolean(L,0); return 1; }
 if(!strcmp(name,"has")) available=1;
 else if(host) {
  if(!strcmp(name,"text") || !strcmp(name,"occurrences")) available=host->text!=NULL;
  else if(!strcmp(name,"selection")) available=host->selection!=NULL;
  else if(!strcmp(name,"replace_selection")) available=host->replace_selection && !host->read_only;
  else if(host->api) {
   if(!strcmp(name,"position")) available=host->api->position!=NULL;
   if(!strcmp(name,"goto_line")) available=host->api->goto_line!=NULL && !host->read_only;
   if(!strcmp(name,"open")) available=host->api->open_file!=NULL && !host->read_only;
   if(!strcmp(name,"waveforms")) available=host->api->open_waveforms!=NULL && !host->read_only;
   if(!strcmp(name,"save")) available=host->api->save_file!=NULL && !host->read_only;
   if(!strcmp(name,"filename")) available=host->api->filename!=NULL;
   if(!strcmp(name,"selection_range")) available=host->api->selection_range!=NULL;
   if(!strcmp(name,"set_selection")) available=host->api->set_selection!=NULL && !host->read_only;
   if(!strcmp(name,"line_count")) available=host->api->line_count!=NULL;
   if(!strcmp(name,"line_text")) available=host->api->line_text!=NULL;
   if(!strcmp(name,"replace_range")) available=host->api->replace_range!=NULL && !host->read_only;
   if(!strcmp(name,"insert")) available=host->api->insert!=NULL && !host->read_only;
   if(!strcmp(name,"message")) available=host->api->message!=NULL;
  }
 }
 lua_pushboolean(L,available); return 1;
}
static int text(lua_State *L) {
 const InsightHost *h=active_host(L);
 const char *s=h->text(h->context);
 if (!s) return luaL_error(L,"no active document");
 lua_pushstring(L,s); return 1;
}
static int occurrences(lua_State *L) {
 size_t n; const char *name=luaL_checklstring(L,1,&n);
 lua_Integer limit=luaL_optinteger(L,2,1000);
 lua_Integer offset=luaL_optinteger(L,3,0);
 if(limit<1 || limit>10000 || offset<0 || (lua_Unsigned)offset>SIZE_MAX) return luaL_error(L,"invalid result limit or offset");
 const InsightHost *h=active_host(L);
 const char *source=h->text ? h->text(h->context) : NULL;
 if(!source) return luaL_error(L,"no active document");
 InsightOccurrence *found=lua_newuserdatauv(L,(size_t)limit*sizeof *found,0);
 size_t total=0;
 if(!insight_occurrences_find(source,strlen(source),name,n,(size_t)offset,found,(size_t)limit,&total))
  return luaL_error(L,"invalid UTF-8 identifier or document (maximum 2 MiB)");
 size_t count=total>(size_t)offset ? total-(size_t)offset : 0;
 if(count>(size_t)limit) count=(size_t)limit;
 lua_createtable(L,(int)count,0);
 for(size_t i=0;i<count;i++) {
  lua_createtable(L,0,4);
  lua_pushinteger(L,(lua_Integer)found[i].start); lua_setfield(L,-2,"start");
  lua_pushinteger(L,(lua_Integer)found[i].end); lua_setfield(L,-2,"finish");
  lua_pushinteger(L,(lua_Integer)found[i].line); lua_setfield(L,-2,"line");
  lua_pushinteger(L,(lua_Integer)found[i].column); lua_setfield(L,-2,"column");
  lua_rawseti(L,-2,(lua_Integer)i+1);
 }
 lua_pushinteger(L,(lua_Integer)total); return 2;
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
 const InsightHost *h=write_host(L);
 if (!h->replace_selection(h->context,s)) return luaL_error(L,"replacement failed");
 return 0;
}
static const char *query_string(lua_State *L,int table,const char *key) {
 lua_getfield(L,table,key);
 size_t length=0; const char *value=lua_isnil(L,-1) ? "" : luaL_checklstring(L,-1,&length);
 if(length>16384 || memchr(value,0,length)) luaL_error(L,"invalid query field %s",key);
 return value; /* Keep field rooted on stack for the lifetime of query. */
}
static lua_Integer query_integer(lua_State *L,int table,const char *key,lua_Integer fallback) {
 lua_getfield(L,table,key); lua_Integer value=lua_isnil(L,-1) ? fallback : luaL_checkinteger(L,-1); lua_pop(L,1); return value;
}
static int symbols(lua_State *L) {
 size_t length; const char *query=luaL_checklstring(L,1,&length);
 if(memchr(query,0,length)) return luaL_error(L,"embedded NUL is unsupported");
 const InsightIndex *index=active_host(L)->index;
 if(!index) return luaL_error(L,"project index unavailable");
 int exact=lua_toboolean(L,2);
 const char *scope="",*kind="",*path=""; lua_Integer limit=256,offset=0;
 if(!lua_isnoneornil(L,3)) {
  luaL_checktype(L,3,LUA_TTABLE);
  scope=query_string(L,3,"scope"); kind=query_string(L,3,"kind"); path=query_string(L,3,"path");
  limit=query_integer(L,3,"limit",256); offset=query_integer(L,3,"offset",0);
 }
 if(limit<1 || limit>1024 || offset<0 || (lua_Unsigned)offset>SIZE_MAX) return luaL_error(L,"invalid symbol page");
 const InsightSymbol **found=lua_newuserdatauv(L,(size_t)limit*sizeof *found,0);
 size_t total=insight_index_query(index,query,exact,scope,kind,path,(size_t)offset,found,(size_t)limit);
 size_t count=total>(size_t)offset ? total-(size_t)offset : 0;
 if(count>(size_t)limit) count=(size_t)limit;
 lua_createtable(L,(int)count,0);
 for(size_t n=0;n<count;n++) {
  lua_createtable(L,0,8);
  lua_pushstring(L,found[n]->signature); lua_setfield(L,-2,"signature");
  lua_pushstring(L,found[n]->type); lua_setfield(L,-2,"type");
  lua_pushstring(L,found[n]->inheritance); lua_setfield(L,-2,"inheritance");
  lua_pushstring(L,found[n]->name); lua_setfield(L,-2,"name");
  lua_pushstring(L,found[n]->path); lua_setfield(L,-2,"path");
  lua_pushstring(L,found[n]->scope); lua_setfield(L,-2,"scope");
  lua_pushstring(L,found[n]->kind); lua_setfield(L,-2,"kind");
  lua_pushinteger(L,(lua_Integer)found[n]->line); lua_setfield(L,-2,"line");
  lua_rawseti(L,-2,(lua_Integer)n+1);
 }
 lua_pushinteger(L,(lua_Integer)total); return 2;
}
static int files(lua_State *L) {
 size_t n; const char *prefix=luaL_optlstring(L,1,"",&n);
 lua_Integer limit=luaL_optinteger(L,2,256),offset=luaL_optinteger(L,3,0);
 if(n>16384 || memchr(prefix,0,n) || limit<1 || limit>1024 || offset<0 || (lua_Unsigned)offset>SIZE_MAX)
  return luaL_error(L,"invalid file query");
 const InsightIndex *index=active_host(L)->index;
 if(!index) return luaL_error(L,"project index unavailable");
 const char **found=lua_newuserdatauv(L,(size_t)limit*sizeof *found,0);
 size_t total=insight_index_files(index,prefix,(size_t)offset,found,(size_t)limit);
 if(total==SIZE_MAX) return luaL_error(L,"unable to enumerate indexed files");
 size_t count=total>(size_t)offset ? total-(size_t)offset : 0;
 if(count>(size_t)limit) count=(size_t)limit;
 lua_createtable(L,(int)count,0);
 for(size_t i=0;i<count;i++) { lua_pushstring(L,found[i]); lua_rawseti(L,-2,(lua_Integer)i+1); }
 lua_pushinteger(L,(lua_Integer)total); return 2;
}
static int stats(lua_State *L) {
 const InsightIndex *index=active_host(L)->index;
 if(!index) return luaL_error(L,"project index unavailable");
 size_t files=insight_index_file_count(index);
 if(files==SIZE_MAX) return luaL_error(L,"unable to compute project statistics");
 lua_createtable(L,0,2); lua_pushinteger(L,(lua_Integer)insight_index_count(index)); lua_setfield(L,-2,"symbols");
 lua_pushinteger(L,(lua_Integer)files); lua_setfield(L,-2,"files_with_symbols"); return 1;
}
static int register_command(lua_State *L) {
 Runtime *r=runtime(L);
 if(!r->registering) return luaL_error(L,"commands can only be registered during extension load");
 size_t name_length,label_length;
 const char *name=luaL_checklstring(L,1,&name_length);
 const char *label=luaL_checklstring(L,2,&label_length);
 luaL_checktype(L,3,LUA_TFUNCTION);
 size_t shortcut_length=0;
 const char *shortcut=luaL_optlstring(L,4,"",&shortcut_length);
 if(shortcut_length>=sizeof r->commands[0].shortcut || memchr(shortcut,0,shortcut_length))
  return luaL_error(L,"invalid command shortcut");
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
 memcpy(command->shortcut,shortcut,shortcut_length+1);
 memcpy(command->name,name,name_length+1); memcpy(command->label,label,label_length+1);
 lua_pushvalue(L,3); command->reference=luaL_ref(L,LUA_REGISTRYINDEX);
 r->command_count++; return 0;
}
static int on_event(lua_State *L) {
 Runtime *r=runtime(L);
 if(!r->registering) return luaL_error(L,"events can only be registered during extension load");
 size_t name_length; const char *name=luaL_checklstring(L,1,&name_length); luaL_checktype(L,2,LUA_TFUNCTION);
 if(name_length>31 || memchr(name,0,name_length)) return luaL_error(L,"invalid event name");
 if(strcmp(name,"document_save") && strcmp(name,"document_activate") && strcmp(name,"document_open") && strcmp(name,"document_close")) return luaL_error(L,"unsupported editor event");
 if(r->event_count==32) return luaL_error(L,"event handler limit exceeded");
 size_t n=r->event_count; snprintf(r->events[n].name,sizeof r->events[n].name,"%s",name);
 lua_pushvalue(L,2); r->events[n].reference=luaL_ref(L,LUA_REGISTRYINDEX); r->event_count++; return 0;
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
 lua_pushcfunction(L,position); lua_setfield(L,-2,"position");
 lua_pushcfunction(L,goto_line); lua_setfield(L,-2,"goto_line");
 lua_pushcfunction(L,open); lua_setfield(L,-2,"open");
 lua_pushcfunction(L,waveforms); lua_setfield(L,-2,"waveforms");
 lua_pushcfunction(L,save); lua_setfield(L,-2,"save");
 lua_pushcfunction(L,filename); lua_setfield(L,-2,"filename");
 lua_pushcfunction(L,selection_range); lua_setfield(L,-2,"selection_range");
 lua_pushcfunction(L,set_selection); lua_setfield(L,-2,"set_selection");
 lua_pushcfunction(L,occurrences); lua_setfield(L,-2,"occurrences");
 lua_pushcfunction(L,line_count); lua_setfield(L,-2,"line_count");
 lua_pushcfunction(L,line_text); lua_setfield(L,-2,"line_text");
 lua_pushcfunction(L,replace_range); lua_setfield(L,-2,"replace_range");
 lua_pushcfunction(L,insert); lua_setfield(L,-2,"insert");
 lua_pushcfunction(L,message); lua_setfield(L,-2,"message");
 lua_pushcfunction(L,has); lua_setfield(L,-2,"has");
 lua_setglobal(L,"editor");
 lua_newtable(L); lua_pushcfunction(L,symbols); lua_setfield(L,-2,"symbols");
 lua_pushcfunction(L,files); lua_setfield(L,-2,"files");
 lua_pushcfunction(L,stats); lua_setfield(L,-2,"stats");
 lua_setglobal(L,"project");
 lua_newtable(L); lua_pushcfunction(L,register_command); lua_setfield(L,-2,"register");
 lua_setglobal(L,"commands");
 lua_newtable(L); lua_pushcfunction(L,on_event); lua_setfield(L,-2,"on"); lua_setglobal(L,"events");
 lua_pushliteral(L,"0.8"); lua_setglobal(L,"INSIGHT_API_VERSION"); return 0;
}
#define DEBUG_BREAKPOINT_LIMIT 128u
#define DEBUG_HOOK_INTERVAL 1000
#define DEBUG_SLICE_HOOKS 20u
#define DEBUG_NONYIELDABLE_HOOK_LIMIT 2000u

struct InsightLuaDebug {
    Runtime runtime;
    lua_State *thread;
    InsightLuaStep mode;
    InsightLuaBreakpoint breakpoints[DEBUG_BREAKPOINT_LIMIT];
    InsightLuaModuleLoader module_loader;
    void *module_context;
    const char *stopped_filename;
    size_t breakpoint_count;
    unsigned depth;
    unsigned target_depth;
    unsigned long line;
    char snapshot[8192];
    InsightLuaWatchGraph watch_graph;
    int finished;
    int slice_pending;
    int pause_requested;
    Runtime *observer;
    int observer_stop;
    const void *observer_paused_frame;
    char observer_log[16384];
    size_t observer_log_length;
    unsigned observer_watch_count;
    struct { char name[64], value[112]; } observer_watches[16];
};

/* Compile an explicitly resolved project module without opening package libraries. */
/* Компилирует найденный модуль проекта без подключения библиотек package. */
static int debug_load_module(lua_State *state)
{
    size_t name_length;
    const char *name = luaL_checklstring(state, 1, &name_length);
    void *context = NULL;
    lua_getallocf(state, &context);
    InsightLuaDebug *debug = context;
    const char *filename = NULL;
    const char *source = NULL;
    size_t length = 0;
    if (!name_length || name_length > 256 || memchr(name, 0, name_length) ||
        !debug->module_loader ||
        !debug->module_loader(debug->module_context, name, &filename, &source, &length))
        return luaL_error(state, "Lua debugger cannot find project module '%s'", name);

    char chunk[4096];
    int written = snprintf(chunk, sizeof chunk, "@%s", filename);
    if (written < 0 || (size_t)written >= sizeof chunk)
        return luaL_error(state, "Lua debugger module filename is too long");
    if (luaL_loadbufferx(state, source, length, chunk, "t") != LUA_OK)
        return lua_error(state);
    return 1;
}

/* Bind the resolver and its borrowed editor context before starting execution. */
/* Подключает поиск модулей и контекст редактора перед выполнением. */
void insight_lua_debug_set_module_loader(InsightLuaDebug *debug, InsightLuaModuleLoader loader, void *context)
{
    if (!debug)
        return;
    debug->module_loader = loader;
    debug->module_context = context;
}

/* Return the source name of the current stop while the VM owns it. */
/* Возвращает имя исходника текущей остановки, пока оно принадлежит VM. */
const char *insight_lua_debug_filename(const InsightLuaDebug *debug)
{
    return debug && debug->line ? debug->stopped_filename : NULL;
}

/* Install the sandbox and anchor the debug coroutine on the main state. */
/* Устанавливает песочницу и закрепляет корутину отладки в основном состоянии. */
static int debug_setup(lua_State *state)
{
    setup(state);
    /* Run the module body from Lua, where line hooks can yield.
     * Выполняем тело модуля из Lua, чтобы хук строки мог приостановить VM. */
    static const char module_wrapper[] =
        "local load_module = ...\n"
        "local loaded, loading = {}, {}\n"
        "return function(name)\n"
        "  if loading[name] then error('circular project require: ' .. name) end\n"
        "  if loaded[name] then return loaded[name] end\n"
        "  local chunk = load_module(name)\n"
        "  loading[name] = true\n"
        "  local result = chunk(name)\n"
        "  loading[name] = nil\n"
        "  if result == nil then result = true end\n"
        "  loaded[name] = result\n"
        "  return result\n"
        "end\n";
    if (luaL_loadbufferx(state, module_wrapper, sizeof module_wrapper - 1, "=debug require", "t") != LUA_OK)
        return lua_error(state);
    lua_pushcfunction(state, debug_load_module);
    lua_call(state, 1, 1);
    lua_setglobal(state, "require");
    lua_newthread(state);
    return 1;
}

/* Count frames to distinguish entering, staying in, and leaving a function. */
/* Считает кадры, чтобы отличить вход в функцию, шаг внутри и выход из неё. */
static unsigned debug_depth(lua_State *state)
{
    lua_Debug frame;
    unsigned depth = 0;
    while (lua_getstack(state, (int)depth, &frame))
        depth++;
    return depth;
}

static void debug_capture(lua_State *state, int selected, char *output, size_t capacity);
static void debug_watch_capture(lua_State *state, int selected, InsightLuaWatchGraph *graph);

/* Append one bounded log entry to the debugger's observer output. */
/* Добавляет ограниченную по размеру запись в журнал сценария отладки. */
static int debug_observer_log(lua_State *state)
{
    InsightLuaDebug *debug = lua_touserdata(state, lua_upvalueindex(1));
    size_t length;
    const char *text = luaL_checklstring(state, 1, &length);
    size_t available = sizeof debug->observer_log - debug->observer_log_length - 1;
    if (available < 2)
        return 0;
    if (length >= available)
        length = available - 1;
    char *destination = debug->observer_log + debug->observer_log_length;
    for (size_t i = 0; i < length; i++)
        destination[i] = text[i] ? text[i] : '?';
    destination[length] = '\n';
    destination[length + 1] = 0;
    debug->observer_log_length += length + 1;
    return 0;
}

/* Publish a named scalar to the debugger's watch graph. */
/* Публикует именованное простое значение в графе наблюдения. */
static int debug_observer_watch(lua_State *state)
{
    InsightLuaDebug *debug = lua_touserdata(state, lua_upvalueindex(1));
    size_t length;
    const char *name = luaL_checklstring(state, 1, &length);
    if (!length || length >= sizeof debug->observer_watches[0].name || memchr(name, 0, length))
        return luaL_error(state, "watch name must contain 1 to 63 bytes");

    unsigned index = 0;
    while (index < debug->observer_watch_count && strcmp(debug->observer_watches[index].name, name))
        index++;
    if (lua_isnil(state, 2))
    {
        if (index < debug->observer_watch_count)
        {
            debug->observer_watch_count--;
            memmove(&debug->observer_watches[index], &debug->observer_watches[index + 1],
                    (debug->observer_watch_count - index) * sizeof debug->observer_watches[0]);
        }
        return 0;
    }
    if (index == sizeof debug->observer_watches / sizeof debug->observer_watches[0])
        return luaL_error(state, "observer watch limit reached");
    char *value = debug->observer_watches[index].value;
    size_t capacity = sizeof debug->observer_watches[index].value;
    if (lua_type(state, 2) == LUA_TBOOLEAN)
        snprintf(value, capacity, "%s", lua_toboolean(state, 2) ? "true" : "false");
    else if (lua_type(state, 2) == LUA_TSTRING || lua_type(state, 2) == LUA_TNUMBER)
        snprintf(value, capacity, "%s", lua_tostring(state, 2));
    else
        return luaL_error(state, "watch expects a string, number, Boolean or nil");
    memcpy(debug->observer_watches[index].name, name, length + 1);
    if (index == debug->observer_watch_count)
        debug->observer_watch_count++;
    return 0;
}

/* Add observer cards below the current graph root without changing the VM snapshot. */
/* Добавляет карточки сценария под корень графа, сохраняя исходный снимок VM. */
void insight_lua_debug_observer_watch(const InsightLuaDebug *debug, InsightLuaWatchGraph *graph)
{
    if (!debug || !graph || !debug->observer_watch_count)
        return;
    if (!graph->node_count)
    {
        graph->node_count = 1;
        snprintf(graph->nodes[0].label, sizeof graph->nodes[0].label, "Observer");
    }
    for (unsigned i = 0; i < debug->observer_watch_count; i++)
    {
        if (graph->node_count >= INSIGHT_LUA_WATCH_NODES || graph->edge_count >= INSIGHT_LUA_WATCH_EDGES)
        {
            graph->limited = 1;
            break;
        }
        unsigned id = graph->node_count++;
        InsightLuaWatchNode *node = &graph->nodes[id];
        const char *name = debug->observer_watches[i].name;
        snprintf(node->label, sizeof node->label, "%s", name);
        snprintf(node->value, sizeof node->value, "%s", debug->observer_watches[i].value);
        snprintf(node->path, sizeof node->path, "observer:%s", name);
        InsightLuaWatchEdge *edge = &graph->edges[graph->edge_count++];
        edge->from = 0;
        edge->to = id;
        snprintf(edge->label, sizeof edge->label, "observer: %s", name);
    }
}

static void debug_hook(lua_State *state, lua_Debug *event);

typedef struct {
    const char *source;
    size_t length;
    InsightLuaDebug *debug;
} DebugObserverSetup;

/* Load a callback into a separate VM with a bounded instruction budget. */
/* Загружает обработчик в отдельную VM с ограничением числа инструкций. */
static int debug_observer_setup(lua_State *state)
{
    const DebugObserverSetup *config = lua_touserdata(state, 1);
    setup(state);
    const char *blocked[] = {"editor", "project", "commands", "events", NULL};
    for (size_t index = 0; blocked[index]; index++)
    {
        lua_pushnil(state);
        lua_setglobal(state, blocked[index]);
    }
    if (luaL_loadbufferx(state, config->source, config->length, "=debug observer", "t") != LUA_OK)
        return lua_error(state);
    lua_call(state, 0, 1);
    if (!lua_isfunction(state, -1))
        return luaL_error(state, "The debug observer must return a function");
    lua_setfield(state, LUA_REGISTRYINDEX, "insight.debug.observer");
    lua_pushlightuserdata(state, config->debug);
    lua_pushcclosure(state, debug_observer_log, 1);
    lua_setglobal(state, "log");
    lua_pushlightuserdata(state, config->debug);
    lua_pushcclosure(state, debug_observer_watch, 1);
    lua_setglobal(state, "watch");
    return 0;
}

/* Create a session for events supplied by another debug engine. */
/* Создаёт сеанс для событий от другого движка отладки. */
InsightLuaDebug *insight_lua_debug_new_observer(void)
{
    return calloc(1, sizeof(InsightLuaDebug));
}

/* Replace or clear the observer without restarting the debugged program. */
/* Заменяет или отключает сценарий, не перезапуская отлаживаемую программу. */
int insight_lua_debug_set_observer(InsightLuaDebug *debug, const char *source,
                                  size_t length, char *error, size_t capacity)
{
    if (!debug)
        return 0;
    Runtime *replacement = NULL;
    if (source && length)
    {
        replacement = calloc(1, sizeof *replacement);
        if (replacement)
            replacement->state = lua_newstate(allocate, replacement);
        if (!replacement || !replacement->state)
        {
            free(replacement);
            if (error && capacity)
                snprintf(error, capacity, "Cannot allocate debugger observer");
            return 0;
        }
    }
    if (replacement)
    {
        lua_State *state = replacement->state;
        DebugObserverSetup config = {source, length, debug};
        lua_sethook(state, budget, LUA_MASKCOUNT, 100);
        lua_pushcfunction(state, debug_observer_setup);
        lua_pushlightuserdata(state, &config);
        if (lua_pcall(state, 1, 0, 0) != LUA_OK)
        {
            if (error && capacity)
            {
                const char *message = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : NULL;
                snprintf(error, capacity, "%s", message ? message : "Debugger observer setup failed");
            }
            lua_close(state);
            free(replacement);
            return 0;
        }
    }
    if (debug->observer)
    {
        lua_close(debug->observer->state);
        free(debug->observer);
    }
    debug->observer = replacement;
    debug->observer_stop = 0;
    debug->observer_log[0] = 0;
    debug->observer_log_length = 0;
    debug->observer_watch_count = 0;
    int mask = LUA_MASKLINE | LUA_MASKCOUNT;
    if (replacement)
        mask |= LUA_MASKCALL | LUA_MASKRET;
    if (debug->thread)
        lua_sethook(debug->thread, debug_hook, mask, DEBUG_HOOK_INTERVAL);
    return 1;
}

/* Report whether the observer is ready to receive events. */
/* Сообщает, готов ли обработчик принимать события. */
int insight_lua_debug_observer_active(const InsightLuaDebug *debug)
{
    return debug && debug->observer != NULL;
}

/* Expose the observer log until the next reconfiguration or restart. */
/* Возвращает журнал до следующей настройки сценария или перезапуска. */
const char *insight_lua_debug_observer_log(const InsightLuaDebug *debug)
{
    return debug ? debug->observer_log : "";
}

typedef struct {
    lua_State *target;
    lua_Debug *event;
} DebugObserverEvent;

/* Copy scalar values only, without sharing mutable objects between VMs. */
/* Копирует только простые значения и не передаёт изменяемые объекты между VM. */
static void debug_observer_copy_value(lua_State *state, lua_State *target)
{
    switch (lua_type(target, -1))
    {
        case LUA_TBOOLEAN:
            lua_pushboolean(state, lua_toboolean(target, -1));
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(target, -1))
                lua_pushinteger(state, lua_tointeger(target, -1));
            else
                lua_pushnumber(state, lua_tonumber(target, -1));
            break;
        case LUA_TSTRING:
        {
            size_t length;
            const char *text = lua_tolstring(target, -1, &length);
            if (length <= 4096)
                lua_pushlstring(state, text, length);
            else
                lua_pushnil(state);
            break;
        }
        default:
            lua_pushnil(state);
            break;
    }
}

/* Invoke the observer with a snapshot of the current source event. */
/* Вызывает обработчик со снимком текущего события исходника. */
static int debug_observer_dispatch(lua_State *state)
{
    const DebugObserverEvent *input = lua_touserdata(state, 1);
    lua_Debug *event = input->event;
    lua_State *target = input->target;
    lua_getfield(state, LUA_REGISTRYINDEX, "insight.debug.observer");
    lua_newtable(state);
    lua_pushstring(state, event->event == LUA_HOOKLINE ? "line" :
                   event->event == LUA_HOOKRET ? "return" : "call");
    lua_setfield(state, -2, "kind");
    lua_pushstring(state, event->source + 1);
    lua_pushvalue(state, -1);
    lua_setfield(state, -3, "file_key");
    lua_setfield(state, -2, "file");
    lua_pushinteger(state, event->currentline > 0 ? event->currentline : event->linedefined);
    lua_setfield(state, -2, "line");
    lua_pushstring(state, event->name ? event->name : "");
    lua_setfield(state, -2, "name");

    lua_newtable(state);
    lua_newtable(state);
    for (int index = 1; index <= 128; index++)
    {
        const char *name = lua_getlocal(target, event, index);
        if (!name)
            break;
        if (name[0] != '(')
        {
            debug_observer_copy_value(state, target);
            lua_setfield(state, -3, name);
            if (index <= event->nparams)
            {
                debug_observer_copy_value(state, target);
                lua_setfield(state, -2, name);
            }
        }
        lua_pop(target, 1);
    }
    lua_setfield(state, -3, "args");
    lua_setfield(state, -2, "locals");
    lua_newtable(state);
    lua_pushglobaltable(target);
    lua_pushnil(target);
    for (unsigned index = 0; index < 256 && lua_next(target, -2); index++)
    {
        if (lua_type(target, -2) == LUA_TSTRING)
        {
            size_t length;
            const char *name = lua_tolstring(target, -2, &length);
            if (length <= 256)
            {
                lua_pushlstring(state, name, length);
                debug_observer_copy_value(state, target);
                lua_rawset(state, -3);
            }
        }
        lua_pop(target, 1);
    }
    lua_setfield(state, -2, "globals");
    lua_call(state, 1, 1);
    return 1;
}

/* Build the same event table from scalars copied by the Python trace process. */
/* Создаёт такую же таблицу события из значений, скопированных процессом Python. */
static int debug_observer_dispatch_external(lua_State *state)
{
    const InsightObserverEvent *event = lua_touserdata(state, 1);
    lua_getfield(state, LUA_REGISTRYINDEX, "insight.debug.observer");
    lua_newtable(state);
    lua_pushstring(state, event->kind);
    lua_setfield(state, -2, "kind");
    lua_pushstring(state, event->file);
    lua_setfield(state, -2, "file");
    lua_pushstring(state, event->file_key ? event->file_key : event->file);
    lua_setfield(state, -2, "file_key");
    lua_pushstring(state, event->name);
    lua_setfield(state, -2, "name");
    lua_pushinteger(state, event->line);
    lua_setfield(state, -2, "line");
    const char *scopes[] = {"locals", "args", "globals"};
    for (unsigned scope = 0; scope < 3; scope++)
    {
        lua_newtable(state);
        for (size_t i = 0; i < event->count; i++)
        {
            const InsightObserverValue *value = &event->values[i];
            if (value->scope != scope)
                continue;
            switch (value->type)
            {
                case 'b': lua_pushboolean(state, value->integer != 0); break;
                case 'i': lua_pushinteger(state, value->integer); break;
                case 'f': lua_pushnumber(state, value->number); break;
                case 's': lua_pushlstring(state, value->text, value->length); break;
                default: continue;
            }
            lua_setfield(state, -2, value->name);
        }
        lua_setfield(state, -2, scopes[scope]);
    }
    lua_call(state, 1, 1);
    return 1;
}

/* Call the observer in its isolated VM and disable it on a script error. */
/* Вызывает обработчик в изолированной VM и отключает его при ошибке. */
static int debug_call_observer(InsightLuaDebug *debug, lua_CFunction dispatch, void *input)
{
    if (!debug || !debug->observer)
        return 0;
    lua_State *state = debug->observer->state;
    debug->observer->ticks = 0;
    lua_pushcfunction(state, dispatch);
    lua_pushlightuserdata(state, input);
    if (lua_pcall(state, 1, 1, 0) != LUA_OK)
    {
        const char *message = lua_type(state, -1) == LUA_TSTRING ? lua_tostring(state, -1) : NULL;
        size_t available = sizeof debug->observer_log - debug->observer_log_length;
        snprintf(debug->observer_log + debug->observer_log_length, available,
                 "Observer stopped: %s", message ? message : "Lua error");
        lua_close(state);
        free(debug->observer);
        debug->observer = NULL;
        return -1;
    }
    int stop = lua_isboolean(state, -1) && lua_toboolean(state, -1);
    lua_pop(state, 1);
    return stop;
}

/* Feed a copied Python event to the Lua observer. */
/* Передаёт копию события Python обработчику Lua. */
int insight_lua_debug_observe(InsightLuaDebug *debug, const InsightObserverEvent *event)
{
    return event ? debug_call_observer(debug, debug_observer_dispatch_external, (void *)event) : -1;
}

/* Run the observer under protection and latch its next-line stop request. */
/* Запускает обработчик под защитой и запоминает запрос остановки на строке. */
static void debug_run_observer(InsightLuaDebug *debug, lua_State *target, lua_Debug *event)
{
    DebugObserverEvent input = {target, event};
    int target_top = lua_gettop(target);
    int result = debug_call_observer(debug, debug_observer_dispatch, &input);
    lua_settop(target, target_top);
    if (result < 0)
        lua_sethook(target, debug_hook, LUA_MASKLINE | LUA_MASKCOUNT, DEBUG_HOOK_INTERVAL);
    if (result)
        debug->observer_stop = 1;
}

/* Yield at source stops or when a VM time slice runs out. */
/* Приостанавливает VM на строке исходника или по окончании порции выполнения. */
static void debug_hook(lua_State *state, lua_Debug *event)
{
    void *context = NULL;
    lua_getallocf(state, &context);
    InsightLuaDebug *debug = context;
    int yieldable = lua_isyieldable(state);

    if (event->event == LUA_HOOKCOUNT)
    {
        unsigned ticks = ++debug->runtime.ticks;
        if (yieldable && ticks >= DEBUG_SLICE_HOOKS)
            lua_yield(state, 0);
        else if (!yieldable && ticks > DEBUG_NONYIELDABLE_HOOK_LIMIT)
            luaL_error(state, "Lua debugger instruction limit exceeded in a non-yieldable call");
        return;
    }
    if (!lua_getinfo(state, debug->observer ? "Slnu" : "S", event) ||
        !event->source || event->source[0] != '@')
        return;
    /* Lua can replay the entry hook when resuming a yielded first line. */
    /* Lua может повторить хук входа при возобновлении с первой строки. */
    if (event->event == LUA_HOOKCALL && event->i_ci == debug->observer_paused_frame)
        return;
    debug->observer_paused_frame = NULL;
    if (debug->observer && lua_checkstack(state, 3))
        debug_run_observer(debug, state, event);
    if (event->event != LUA_HOOKLINE || !yieldable || event->currentline < 1)
        return;

    const char *filename = event->source + 1;
    unsigned long line = (unsigned long)event->currentline;
    int pause = debug->observer_stop || debug->pause_requested || debug->mode == INSIGHT_LUA_STEP_INTO;
    if (!pause && (debug->mode == INSIGHT_LUA_STEP_OVER || debug->mode == INSIGHT_LUA_STEP_OUT))
    {
        unsigned depth = debug_depth(state);
        if (debug->mode == INSIGHT_LUA_STEP_OVER)
            pause = depth <= debug->target_depth;
        else
            pause = depth < debug->target_depth;
    }
    for (size_t i = 0; !pause && i < debug->breakpoint_count; i++)
        pause = debug->breakpoints[i].line == line &&
            !strcmp(debug->breakpoints[i].filename, filename);

    if (pause)
    {
        debug->observer_paused_frame = event->i_ci;
        debug->observer_stop = 0;
        debug->pause_requested = 0;
        debug->depth = debug_depth(state);
        debug->line = line;
        debug->stopped_filename = filename;
        debug_capture(state, 0, debug->snapshot, sizeof debug->snapshot);
        debug_watch_capture(state, 0, &debug->watch_graph);
        lua_yield(state, 0);
    }
}

/* Close the debug VM and release its state. */
/* Закрывает виртуальную машину отладчика и освобождает её состояние. */
void insight_lua_debug_free(InsightLuaDebug *debug)
{
    if (!debug)
        return;
    if (debug->observer)
    {
        lua_close(debug->observer->state);
        free(debug->observer);
    }
    if (debug->runtime.state)
        lua_close(debug->runtime.state);
    free(debug);
}

/* Compile a source snapshot into a fresh restricted VM. */
/* Компилирует снимок исходника в новой виртуальной машине с ограничениями. */
InsightLuaDebug *insight_lua_debug_new(const char *filename, const char *source,
                                      size_t length, char *error, size_t capacity)
{
    if (error && capacity)
        error[0] = 0;
    if (!source)
        return NULL;

    InsightLuaDebug *debug = calloc(1, sizeof *debug);
    if (!debug)
        return NULL;
    lua_State *state = lua_newstate(allocate, debug);
    debug->runtime.state = state;
    if (!state)
    {
        insight_lua_debug_free(debug);
        return NULL;
    }

    lua_pushcfunction(state, debug_setup);
    int status = lua_pcall(state, 0, 1, 0);
    if (!status)
    {
        debug->thread = lua_tothread(state, -1);
        char chunk[4096];
        int written = snprintf(chunk, sizeof chunk, "@%s", filename ? filename : "<buffer>");
        if (written < 0 || (size_t)written >= sizeof chunk)
        {
            if (error && capacity)
                snprintf(error, capacity, "Lua debugger filename is too long");
            insight_lua_debug_free(debug);
            return NULL;
        }
        status = luaL_loadbufferx(debug->thread, source, length, chunk, "t");
    }
    if (status)
    {
        if (error && capacity)
        {
            const char *message = lua_tostring(debug->thread ? debug->thread : state, -1);
            snprintf(error, capacity, "%s", message ? message : "Lua debugger setup failed");
        }
        insight_lua_debug_free(debug);
        return NULL;
    }

    lua_sethook(debug->thread, debug_hook, LUA_MASKLINE | LUA_MASKCOUNT, DEBUG_HOOK_INTERVAL);
    return debug;
}

/* Stop at the next yieldable Lua line of the current action. */
/* Запрашивает остановку на ближайшей строке Lua, где допустима пауза. */
void insight_lua_debug_request_pause(InsightLuaDebug *debug)
{
    if (debug && !debug->finished)
        debug->pause_requested = 1;
}

/* Run one bounded VM slice with a borrowed read-only host. */
/* Выполняет одну ограниченную порцию VM с контекстом только для чтения. */
int insight_lua_debug_resume(InsightLuaDebug *debug, InsightLuaStep mode,
    const InsightLuaBreakpoint *breakpoints, size_t breakpoint_count, const InsightHost *host,
    unsigned long *line, char *error, size_t error_capacity)
{
    if (line)
        *line = 0;
    if (error && error_capacity)
        error[0] = 0;
    if (!debug || debug->finished || breakpoint_count > DEBUG_BREAKPOINT_LIMIT ||
        (breakpoint_count && !breakpoints))
        return -1;

    if (!debug->slice_pending)
    {
        debug->mode = mode;
        debug->target_depth = debug->depth;
        debug->breakpoint_count = breakpoint_count;
        if (breakpoint_count)
            memcpy(debug->breakpoints, breakpoints, breakpoint_count * sizeof *breakpoints);
    }
    debug->slice_pending = 0;
    debug->line = 0;
    debug->stopped_filename = NULL;
    debug->runtime.ticks = 0;

    InsightHost read_only_host;
    if (host)
    {
        read_only_host = *host;
        read_only_host.read_only = 1;
        debug->runtime.host = &read_only_host;
    }
    int results = 0;
    int status = lua_resume(debug->thread, debug->runtime.state, 0, &results);
    debug->runtime.host = NULL;

    if (status == LUA_YIELD)
    {
        if (debug->line)
        {
            if (line)
                *line = debug->line;
            return 1;
        }
        debug->slice_pending = 1;
        return 2;
    }

    debug->finished = 1;
    if (status != LUA_OK && error && error_capacity)
    {
        const char *message = lua_tostring(debug->thread, -1);
        snprintf(error, error_capacity, "%s", message ? message : "Lua debugger execution failed");
    }
    return status == LUA_OK ? 0 : -1;
}

static void debug_append(char **output,size_t *capacity,const char *format,...) {
 if(*capacity<=1) return;
 va_list args;
 va_start(args,format);
 int written=vsnprintf(*output,*capacity,format,args);
 va_end(args);
 if(written<0) return;
 size_t used=(size_t)written<*capacity ? (size_t)written:*capacity-1;
 *output+=used;
 *capacity-=used;
}

/* List source frames, then show raw locals of the selected frame. */
/* Показывает кадры исходников и простые локальные значения выбранного кадра. */
static void debug_capture(lua_State *state, int selected, char *output, size_t capacity)
{
    *output = 0;
    lua_Debug frame;
    for (int level = 0; level < 16 && capacity > 1; level++)
    {
        if (!lua_getstack(state, level, &frame))
            break;
        if (!lua_getinfo(state, "nSl", &frame) || !frame.source ||
            frame.source[0] != '@' || frame.currentline < 1)
            continue;
        char heading[256];
        snprintf(heading, sizeof heading, "%s (%s:%d)",
                 frame.name ? frame.name : frame.what, frame.short_src, frame.currentline);
        for (char *byte = heading; *byte; byte++)
            if (*byte == '\n' || *byte == '\r')
                *byte = ' ';
        debug_append(&output, &capacity, "#%d %s\n", level, heading);
    }
    if (!lua_getstack(state, selected, &frame))
        return;
    debug_append(&output, &capacity, "\nLocals in frame #%d\n", selected);
    for (int slot = 1; slot <= 64 && capacity > 1; slot++)
    {
        const char *name = lua_getlocal(state, &frame, slot);
        if (!name)
            break;
        if (name[0] != '(')
        {
            debug_append(&output, &capacity, "    %s = ", name);
            switch (lua_type(state, -1))
            {
                case LUA_TSTRING:
                    debug_append(&output, &capacity, "\"%.120s\"", lua_tostring(state, -1));
                    break;
                case LUA_TBOOLEAN:
                    debug_append(&output, &capacity, "%s", lua_toboolean(state, -1) ? "true" : "false");
                    break;
                case LUA_TNUMBER:
                    if (lua_isinteger(state, -1))
                        debug_append(&output, &capacity, "%lld", (long long)lua_tointeger(state, -1));
                    else
                        debug_append(&output, &capacity, "%.17g", (double)lua_tonumber(state, -1));
                    break;
                default:
                    debug_append(&output, &capacity, "%s", luaL_typename(state, -1));
                    break;
            }
            debug_append(&output, &capacity, "\n");
        }
        lua_pop(state, 1);
    }
}

void insight_lua_debug_inspect(InsightLuaDebug *debug,char *output,size_t capacity) {
 if(!output || !capacity) return;
 snprintf(output,capacity,"%s",debug && !debug->finished && debug->line ? debug->snapshot:"");
}

#define WATCH_DEPTH_LIMIT 6u
#define WATCH_MEMBERS_LIMIT 16u
#define WATCH_LOCAL_LIMIT 32

typedef struct {
    InsightLuaWatchGraph *graph;
    const void *identity[INSIGHT_LUA_WATCH_NODES];
} WatchCapture;

/* Format raw values without calling Lua code or converting table keys. */
/* Форматирует значения без вызова Lua-кода и преобразования ключей таблицы. */
static void watch_value_text(lua_State *state, int index, char *output, size_t capacity)
{
    switch (lua_type(state, index))
    {
        case LUA_TSTRING:
        {
            size_t length = 0;
            const char *value = lua_tolstring(state, index, &length);
            size_t used = 0;
            output[used++] = '"';
            for (size_t i = 0; i < length && used + 6 < capacity; i++)
            {
                unsigned char byte = (unsigned char)value[i];
                if (byte < 32 || byte == 127 || byte == '\\' || byte == '"')
                {
                    int written = snprintf(output + used, capacity - used, "\\x%02x", (unsigned)byte);
                    if (written > 0)
                        used += (size_t)written;
                }
                else
                    output[used++] = (char)byte;
            }
            output[used++] = '"';
            output[used] = '\0';
            break;
        }
        case LUA_TBOOLEAN:
            snprintf(output, capacity, "%s", lua_toboolean(state, index) ? "true" : "false");
            break;
        case LUA_TNUMBER:
            if (lua_isinteger(state, index))
                snprintf(output, capacity, "%lld", (long long)lua_tointeger(state, index));
            else
                snprintf(output, capacity, "%.17g", (double)lua_tonumber(state, index));
            break;
        default:
            snprintf(output, capacity, "%s", luaL_typename(state, index));
            break;
    }
}

static void watch_add_value(lua_State *state, WatchCapture *capture, unsigned parent,
                            const char *label, int index, unsigned depth);

/* Visit table values, the actual metatable, and closure upvalues. */
/* Обходит значения таблицы, её метатаблицу и захваченные переменные замыкания. */
static void watch_expand(lua_State *state, WatchCapture *capture, unsigned node,
                         int index, unsigned depth)
{
    InsightLuaWatchGraph *graph = capture->graph;
    if (!lua_checkstack(state, 4))
    {
        graph->limited = 1;
        return;
    }

    index = lua_absindex(state, index);
    if (lua_istable(state, index))
    {
        if (lua_getmetatable(state, index))
        {
            watch_add_value(state, capture, node, "<metatable>", -1, depth);
            lua_pop(state, 1);
        }
        unsigned members = 0;
        lua_pushnil(state);
        while (lua_next(state, index))
        {
            if (members++ == WATCH_MEMBERS_LIMIT || graph->node_count == INSIGHT_LUA_WATCH_NODES ||
                graph->edge_count == INSIGHT_LUA_WATCH_EDGES)
            {
                graph->limited = 1;
                lua_pop(state, 2);
                break;
            }
            char label[96];
            watch_value_text(state, -2, label, sizeof label);
            if (lua_type(state, -2) != LUA_TSTRING && lua_type(state, -2) != LUA_TNUMBER &&
                lua_type(state, -2) != LUA_TBOOLEAN)
                snprintf(label, sizeof label, "[%s %p]", luaL_typename(state, -2), lua_topointer(state, -2));
            watch_add_value(state, capture, node, label, -1, depth);
            lua_pop(state, 1);
        }
    }
    else if (lua_isfunction(state, index))
    {
        for (int slot = 1; slot <= (int)WATCH_MEMBERS_LIMIT + 1; slot++)
        {
            const char *name = lua_getupvalue(state, index, slot);
            if (!name)
                break;
            if (slot > (int)WATCH_MEMBERS_LIMIT)
            {
                graph->limited = 1;
                lua_pop(state, 1);
                break;
            }
            if (strcmp(name, "_ENV"))
            {
                char label[96];
                snprintf(label, sizeof label, "upvalue %s", name);
                watch_add_value(state, capture, node, label, -1, depth);
            }
            lua_pop(state, 1);
        }
    }
}

/* Record a labeled edge and reuse existing nodes for aliases and cycles. */
/* Сохраняет подписанную стрелку и переиспользует узлы для общих ссылок и циклов. */
static void watch_add_value(lua_State *state, WatchCapture *capture, unsigned parent,
                            const char *label, int index, unsigned depth)
{
    InsightLuaWatchGraph *graph = capture->graph;
    if (graph->edge_count >= INSIGHT_LUA_WATCH_EDGES)
    {
        graph->limited = 1;
        return;
    }

    const void *identity = lua_istable(state, index) || lua_isfunction(state, index) ?
        lua_topointer(state, index) : NULL;
    unsigned target = graph->node_count;
    if (identity)
    {
        for (unsigned i = 1; i < graph->node_count; i++)
            if (capture->identity[i] == identity)
            {
                target = i;
                break;
            }
    }

    int is_new = target == graph->node_count;
    if (is_new)
    {
        if (target >= INSIGHT_LUA_WATCH_NODES)
        {
            graph->limited = 1;
            return;
        }
        capture->identity[target] = identity;
        graph->node_count++;
        InsightLuaWatchNode *node = &graph->nodes[target];
        snprintf(node->label, sizeof node->label, "%s", label);
        watch_value_text(state, index, node->value, sizeof node->value);
        if (lua_isfunction(state, index) && lua_checkstack(state, 1))
        {
            lua_pushvalue(state, index);
            lua_Debug function;
            if (lua_getinfo(state, ">S", &function) && function.linedefined > 0)
                snprintf(node->value, sizeof node->value, "function (line %d)", function.linedefined);
        }
        const char *parent_path = graph->nodes[parent].path;
        size_t parent_length = strlen(parent_path), label_length = strlen(label);
        if ((parent == 0 || parent_length) && parent_length + label_length + 2 <= sizeof node->path)
        {
            memcpy(node->path, parent_path, parent_length);
            node->path[parent_length] = '\x1f';
            memcpy(node->path + parent_length + 1, label, label_length + 1);
        }
    }

    InsightLuaWatchEdge *edge = &graph->edges[graph->edge_count++];
    edge->from = parent;
    edge->to = target;
    snprintf(edge->label, sizeof edge->label, "%s", label);
    if (is_new && identity)
    {
        if (depth < WATCH_DEPTH_LIMIT)
            watch_expand(state, capture, target, index, depth + 1);
        else
            graph->limited = 1;
    }
}

/* Capture the current Lua frame while its line hook still owns the PC. */
/* Снимает текущий кадр Lua, пока хук строки ещё видит правильную позицию VM. */
static void debug_watch_capture(lua_State *state, int selected, InsightLuaWatchGraph *graph)
{
    memset(graph, 0, sizeof *graph);
    graph->node_count = 1;
    snprintf(graph->nodes[0].label, sizeof graph->nodes[0].label, "Current frame");
    WatchCapture capture = {0};
    capture.graph = graph;
    lua_Debug frame;
    if (!lua_checkstack(state, 4) || !lua_getstack(state, selected, &frame))
        return;

    for (int slot = 1; slot <= WATCH_LOCAL_LIMIT + 1; slot++)
    {
        const char *name = lua_getlocal(state, &frame, slot);
        if (!name)
            break;
        if (slot > WATCH_LOCAL_LIMIT)
            graph->limited = 1;
        else if (name[0] != '(')
        {
            char label[96];
            snprintf(label, sizeof label, "local %s", name);
            watch_add_value(state, &capture, 0, label, -1, 0);
        }
        lua_pop(state, 1);
    }
    if (lua_getinfo(state, "f", &frame))
    {
        watch_expand(state, &capture, 0, -1, 1);
        lua_pop(state, 1);
    }
}

/* Inspect another source frame without changing the VM's resume position. */
/* Просматривает другой кадр исходника, сохраняя позицию продолжения VM. */
int insight_lua_debug_select_frame(InsightLuaDebug *debug, unsigned level,
                                   const char **filename, unsigned long *line)
{
    if (!debug || !debug->line || debug->finished || level >= 16 || !filename || !line)
        return 0;
    lua_Debug frame;
    if (!lua_getstack(debug->thread, (int)level, &frame) ||
        !lua_getinfo(debug->thread, "Sl", &frame) || !frame.source ||
        frame.source[0] != '@' || frame.currentline < 1)
        return 0;
    *filename = frame.source + 1;
    *line = (unsigned long)frame.currentline;
    debug_capture(debug->thread, (int)level, debug->snapshot, sizeof debug->snapshot);
    debug_watch_capture(debug->thread, (int)level, &debug->watch_graph);
    return 1;
}

/* Return a graph only while the VM is stopped at a source line. */
/* Возвращает граф только пока VM остановлена на строке исходника. */
const InsightLuaWatchGraph *insight_lua_debug_watch_graph(const InsightLuaDebug *debug)
{
    if (!debug || debug->finished || !debug->line)
        return NULL;
    return &debug->watch_graph;
}

static int traceback(lua_State *L) {
 const char *message=lua_type(L,1)==LUA_TSTRING ? lua_tostring(L,1) : "Lua error (non-string value)";
 luaL_traceback(L,L,message,1); return 1;
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
 lua_pushcfunction(r->state,traceback); lua_pushcfunction(r->state,setup);
 int status=lua_pcall(r->state,0,0,1);
 report(r->state,status,error,capacity);
 lua_settop(r->state,0); return status ? 1 : 0;
}
static int execute(Runtime *r,const char *source,size_t length,char *error,size_t capacity) {
 lua_State *L=r->state; r->ticks=0; lua_sethook(L,budget,LUA_MASKCOUNT,1000);
 lua_pushcfunction(L,traceback);
 int status=luaL_loadbufferx(L,source,length,"script","t");
 if(!status) status=lua_pcall(L,0,0,1);
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
const char *insight_session_command_shortcut(const InsightSession *s,size_t n) {
 return n<insight_session_command_count(s) ? s->current->commands[n].shortcut : NULL;
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
 lua_pushcfunction(L,traceback);
 lua_rawgeti(L,LUA_REGISTRYINDEX,r->commands[n].reference);
 int status=lua_pcall(L,0,0,1);
 report(L,status,error,capacity); lua_settop(L,0); r->host=NULL;
 return status ? 1 : 0;
}

static int emit_one(lua_State *L) {
 Runtime *r=runtime(L); lua_rawgeti(L,LUA_REGISTRYINDEX,r->event_reference);
 lua_createtable(L,0,2); lua_pushstring(L,r->event_name); lua_setfield(L,-2,"name");
 lua_pushstring(L,r->event_path ? r->event_path : ""); lua_setfield(L,-2,"path");
 lua_call(L,1,0); return 0;
}
int insight_session_emit(InsightSession *s,const char *name,const char *path,const InsightHost *host,char *error,size_t capacity) {
 if(error && capacity) error[0]=0;
 if(!s || !name || !valid_host(host) || insight_session_is_running(s)) return 2;
 Runtime *r=s->current; if(!r) return 0;
 InsightHost readonly=*host; readonly.read_only=1; r->host=&readonly;
 r->event_name=name; r->event_path=path; int result=0;
 for(size_t n=0;n<r->event_count;n++) {
  if(strcmp(name,r->events[n].name)) continue;
  r->event_reference=r->events[n].reference; r->ticks=0;
  lua_sethook(r->state,budget,LUA_MASKCOUNT,1000); lua_pushcfunction(r->state,traceback); lua_pushcfunction(r->state,emit_one);
  int status=lua_pcall(r->state,0,0,1);
  if(status && !result) { report(r->state,status,error,capacity); result=1; }
  lua_settop(r->state,0);
 }
 r->host=NULL; r->event_name=NULL; r->event_path=NULL; return result;
}
