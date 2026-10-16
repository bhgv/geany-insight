/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_DIFF_H
#define GEANY_INSIGHT_DIFF_H
#include <gtk/gtk.h>
/* Main-thread source navigation; native filename, one-based logical line. */
typedef void (*InsightDiffOpen)(const gchar *,guint);
void insight_diff_set_source_opener(InsightDiffOpen);
void insight_diff_read(GKeyFile *);
void insight_diff_write(GKeyFile *);
void insight_diff_show(GtkWidget *parent);
void insight_diff_show_files(GtkWidget *parent,const gchar *left,const gchar *right,guint text_flags);
#endif
