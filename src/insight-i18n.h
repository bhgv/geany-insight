/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_I18N_H
#define INSIGHT_I18N_H
#include <gtk/gtk.h>
void insight_i18n_init(void);
void insight_i18n_finalize(void);
void insight_i18n_menu(GtkWidget *);
gboolean insight_i18n_is_applying(void);
void insight_i18n_apply(GtkWidget *);
const gchar *insight_tr(const gchar *);
const gchar *insight_i18n_english(const gchar *);
const gchar *insight_i18n_language(void);
gchar *insight_i18n_printf(const gchar *,...) G_GNUC_PRINTF(1,2);
#ifdef INSIGHT_LOCALIZE_FORMATS
#define g_strdup_printf(...) insight_i18n_printf(__VA_ARGS__)
#endif
#endif
