/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PYTHON_DEBUG_H
#define INSIGHT_PYTHON_DEBUG_H

#include <gio/gio.h>
#include "../insight/core/runtime.h"

typedef struct InsightPythonDebug InsightPythonDebug;

InsightPythonDebug *insight_python_debug_new(const gchar *interpreter, const gchar *helper,
    const gchar *filename, const gchar *project_root, const gchar *source, gsize length, GError **error);
void insight_python_debug_free(InsightPythonDebug *debugger);
gboolean insight_python_debug_send(InsightPythonDebug *debugger, const gchar *command, GError **error);
GDataInputStream *insight_python_debug_output(InsightPythonDebug *debugger);
gint insight_python_debug_parse(const gchar *packet, gsize length, unsigned long *line,
    gchar **filename, gchar **stack, InsightLuaWatchGraph **graph, gchar **message);

#endif
