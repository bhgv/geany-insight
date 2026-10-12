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
int insight_run(const InsightHost *, const char *, size_t, char *, size_t);
/* Single-threaded extension session. Successful loads replace the extension;
 * failed loads retain its commands and state. Editor/project access is blocked
 * during registration. Each invocation binds a temporary host and resets the
 * instruction budget; Lua globals persist, total Lua memory is bounded. */
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
