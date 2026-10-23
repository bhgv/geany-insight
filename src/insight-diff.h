/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_DIFF_H
#define GEANY_INSIGHT_DIFF_H
#include <gtk/gtk.h>
/* Main-thread source navigation; native filename, one-based logical line. */
typedef void (*InsightDiffOpen)(const gchar *,guint);
void insight_diff_set_source_opener(InsightDiffOpen);
typedef void (*InsightDiffColorize)(GtkTextBuffer *,const gchar *);
void insight_diff_set_colorizer(InsightDiffColorize);
void insight_diff_review_language(GtkWidget *,const gchar *);
void insight_diff_review_choices(GtkWidget *,const gchar *,const gchar *const *,const gchar *const *,const gchar *const *,guint,guint);
void insight_diff_read(GKeyFile *);
void insight_diff_write(GKeyFile *);
void insight_diff_show(GtkWidget *parent);
void insight_diff_show_files(GtkWidget *parent,const gchar *left,const gchar *right,guint text_flags);
typedef gboolean (*InsightDiffApply)(const gchar *,gpointer,GError **);
typedef void (*InsightDiffAsk)(const gchar *,gpointer);
void insight_diff_review(GtkWidget *,const gchar *,const gchar *,const gchar *,InsightDiffApply,InsightDiffAsk,gpointer,GDestroyNotify);
GtkWidget *insight_diff_review_create(GtkWidget *,const gchar *,const gchar *,const gchar *,InsightDiffApply,InsightDiffAsk,gpointer,GDestroyNotify);
/* Owned snapshot; NULL while comparing or after failure. All calls are main-thread. */
gchar *insight_diff_review_result(GtkWidget *);
gboolean insight_diff_review_update(GtkWidget *,const gchar *,const gchar *,const gchar *,const gchar *);
void insight_diff_review_lock(GtkWidget *,gboolean);
void insight_diff_review_compact(GtkWidget *);
#endif
