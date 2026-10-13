/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_REMOTE_H
#define GEANY_INSIGHT_REMOTE_H
#include <glib.h>
void insight_remote_ui_init(gboolean (*)(const char *));
void insight_remote_ui_finalize(void);
#endif
