/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CUSTOM_COMMAND_H
#define INSIGHT_CUSTOM_COMMAND_H
#include <gio/gio.h>
typedef struct { gchar *name,*command,*directory; } InsightCustomCommand;
void insight_custom_command_free(gpointer);
/* Parse first, substitute each argument second. Substituted text can never
 * introduce another argument or an implicit shell operator. Keys are case
 * sensitive for one-letter SI tokens, upper-case for long path variables. */
gchar *insight_custom_expand(const gchar *,GHashTable *,GError **);
gchar **insight_custom_argv(const gchar *,GHashTable *,GError **);
GPtrArray *insight_custom_commands_load(const gchar *,GError **);
gboolean insight_custom_commands_save(const gchar *,GPtrArray *,GError **);
#endif
