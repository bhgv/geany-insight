/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_EXTENSIONS_H
#define INSIGHT_EXTENSIONS_H
#include <gio/gio.h>
#include "runtime.h"
typedef struct InsightExtensions InsightExtensions;
InsightExtensions *insight_extensions_new(const char *settings_path);
void insight_extensions_free(InsightExtensions *);
/* Selection changes live commands only after successful validation and settings
 * replacement. Restore failures preserve the current session. Missing settings
 * are a successful no-op. Unloading persists an empty startup selection. */
gboolean insight_extensions_select(InsightExtensions *,const char *,GError **);
gboolean insight_extensions_restore(InsightExtensions *,GError **);
gboolean insight_extensions_unload(InsightExtensions *,GError **);
InsightSession *insight_extensions_session(InsightExtensions *);
/* Reads regular local Lua files only, <=256 KiB; g_free the returned source. */
gboolean insight_read_lua_file(const char *,char **,gsize *,GError **);
gboolean insight_read_source_file(const char *,char **,gsize *,GError **);
#endif
