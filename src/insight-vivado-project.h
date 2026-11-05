/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_VIVADO_PROJECT_H
#define INSIGHT_VIVADO_PROJECT_H
#include "../insight/desktop/project_files.h"

gboolean insight_vivado_is_xpr(const gchar *path);
gchar *insight_vivado_open(const gchar *filename, GError **error);
InsightProjectFiles *insight_vivado_sync(const gchar *filename, GError **error);
#endif
