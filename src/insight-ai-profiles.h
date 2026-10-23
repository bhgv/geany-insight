/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_AI_PROFILES_H
#define INSIGHT_AI_PROFILES_H
#include <gtk/gtk.h>
#include "../insight/desktop/ai_client.h"
const gchar *insight_ai_profiles_endpoint(guint);
const gchar *insight_ai_profiles_key_env(guint);
typedef struct InsightAiProfiles InsightAiProfiles;
InsightAiProfiles *insight_ai_profiles_bind(GtkWidget *,GtkWidget *,GtkWidget *,GtkWidget *,GtkWidget *,GtkWidget *);
gboolean insight_ai_profiles_multiple(InsightAiProfiles *);
void insight_ai_profiles_save_current(InsightAiProfiles *);
void insight_ai_profiles_install_choices(GtkWidget *,GtkWidget *,const gchar *,const gchar *);
void insight_ai_profiles_show(GtkWidget *,gpointer);
/* TRUE: parallel comparison owns completion of outer; FALSE: use current fields. */
gboolean insight_ai_profiles_dispatch(InsightAiProfiles *,GTask *,const InsightAiRequest *,InsightAiAnswer **);
#endif
