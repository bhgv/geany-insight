/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RUNTIME_H
#define INSIGHT_RUNTIME_H
#include <stddef.h>
#include "index.h"
typedef struct {
 unsigned long (*position)(void *);
 int (*goto_line)(void *,unsigned long);
 int (*open_file)(void *,const char *);
 int (*save_file)(void *);
 const char *(*filename)(void *);
 int (*selection_range)(void *,unsigned long *,unsigned long *);
 int (*set_selection)(void *,unsigned long,unsigned long);
 unsigned long (*line_count)(void *);
 const char *(*line_text)(void *,unsigned long);
 int (*replace_range)(void *,unsigned long,unsigned long,const char *);
 int (*insert)(void *,unsigned long,const char *);
 int (*message)(void *,const char *);
 int (*open_waveforms)(void *);
} InsightEditorAPI;
typedef struct {
 void *context;
 /* Borrowed UTF-8 strings, valid until the next host call. */
 const char *(*text)(void *);
 const char *(*selection)(void *);
 int (*replace_selection)(void *, const char *);
 /* Optional project index; borrowed, immutable during script execution. */
 const InsightIndex *index;
 const InsightEditorAPI *api;
 int read_only;
} InsightHost;
/* Fresh interpreter per invocation. Host edits are immediate, not transactional.
 * Returns 0 on success, 1 on script error, 2 on invalid host/allocation failure. */
/* Compile text for diagnostics without running it or opening Lua libraries. */
int insight_lua_check(const char *,size_t,char *,size_t);
int insight_run(const InsightHost *, const char *, size_t, char *, size_t);
/* Single-threaded extension session. Successful loads replace the extension;
 * failed loads retain its commands and state. Editor/project access is blocked
 * during registration. Each invocation binds a temporary host and resets the
 * instruction budget; Lua globals persist, total Lua memory is bounded. */
typedef struct InsightLuaDebug InsightLuaDebug;
typedef enum {
 INSIGHT_LUA_STEP_INTO, INSIGHT_LUA_STEP_OVER, INSIGHT_LUA_STEP_OUT, INSIGHT_LUA_CONTINUE
} InsightLuaStep;
InsightLuaDebug *insight_lua_debug_new(const char *,size_t,char *,size_t);
void insight_lua_debug_free(InsightLuaDebug *);
/* Resume returns 1 when paused, 0 on completion, and -1 on failure.
 * Up to 128 one-based breakpoint lines. Debug sessions have no editor host. */
int insight_lua_debug_resume(InsightLuaDebug *,InsightLuaStep,const unsigned long *,size_t,
 unsigned long *,char *,size_t);
void insight_lua_debug_inspect(InsightLuaDebug *,char *,size_t);
typedef struct InsightSession InsightSession;
InsightSession *insight_session_new(void);
void insight_session_free(InsightSession *);
int insight_session_load(InsightSession *,const char *,size_t,char *,size_t);
size_t insight_session_command_count(const InsightSession *);
int insight_session_is_running(const InsightSession *);
const char *insight_session_command_name(const InsightSession *,size_t);
const char *insight_session_command_label(const InsightSession *,size_t);
const char *insight_session_command_shortcut(const InsightSession *,size_t);
int insight_session_invoke(InsightSession *,const char *,const InsightHost *,char *,size_t);
int insight_session_emit(InsightSession *,const char *,const char *,const InsightHost *,char *,size_t);
#endif
