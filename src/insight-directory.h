/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_DIRECTORY_H
#define GEANY_INSIGHT_DIRECTORY_H
#include <gtk/gtk.h>
/* Main-thread provider returns an owned immutable NULL-terminated snapshot. */
typedef gchar **(*InsightDirectoryTypes)(void);
void insight_directory_set_types_provider(InsightDirectoryTypes);
void insight_directory_show(GtkWidget *parent);
#endif
