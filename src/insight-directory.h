/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_DIRECTORY_H
#define GEANY_INSIGHT_DIRECTORY_H
#include <gtk/gtk.h>
/* Main-thread provider returns an owned immutable NULL-terminated snapshot. */
typedef gchar **(*InsightDirectoryTypes)(void);
/* Main-thread editor guard. TRUE means a buffer uses this native path. */
typedef gboolean (*InsightDirectoryOpen)(const gchar *);
void insight_directory_set_open_guard(InsightDirectoryOpen);
void insight_directory_set_types_provider(InsightDirectoryTypes);
/* Workbench-owned persistence; no scan or filesystem access on restore. */
void insight_directory_read(GKeyFile *);
void insight_directory_write(GKeyFile *);
void insight_directory_show(GtkWidget *parent);
#endif
