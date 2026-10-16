/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_COMPLETION_PREFERENCES_H
#define INSIGHT_COMPLETION_PREFERENCES_H
#include <gtk/gtk.h>
#include "completion_settings.h"
GtkWidget *insight_completion_preferences_new(const InsightCompletionSettings *);
void insight_completion_preferences_get(GtkWidget *,InsightCompletionSettings *);
#endif
