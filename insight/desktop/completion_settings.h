/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_COMPLETION_SETTINGS_H
#define INSIGHT_COMPLETION_SETTINGS_H
#include <glib.h>
typedef struct { gboolean case_sensitive,detailed,accept_tab,inside_comments,insert_parentheses,space_after_name;gint min_chars,max_rows;gboolean semantic,insert_parameters,space_between_parameters; } InsightCompletionSettings;
void insight_completion_settings_default(InsightCompletionSettings *);
gboolean insight_completion_settings_valid(const InsightCompletionSettings *,GError **);
gboolean insight_completion_settings_read(InsightCompletionSettings *,GKeyFile *,GError **);
gboolean insight_completion_settings_write(const InsightCompletionSettings *,GKeyFile *,GError **);
/* Parentheses only; never invent call arguments from a declaration signature. */
gchar *insight_completion_settings_suffix(const InsightCompletionSettings *,gboolean,const gchar *);
#endif
