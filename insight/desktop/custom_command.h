/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Bounded custom-command substitution, profile storage and diagnostic links.
 * RU: Ограниченные подстановки команд, хранение профилей и ссылки диагностики. */
#ifndef INSIGHT_CUSTOM_COMMAND_H
#define INSIGHT_CUSTOM_COMMAND_H
#include <gio/gio.h>
typedef struct {
    gchar *name, *command, *directory, *diagnostic_pattern, *shortcut;
    gboolean parse_on_finish;
} InsightCustomCommand;
void insight_custom_command_free(gpointer);
InsightCustomCommand *insight_custom_command_copy(const InsightCustomCommand *);
/* Parse first, substitute each argument second. Substituted text can never
 * introduce another argument or an implicit shell operator. Keys are case
 * sensitive for one-letter SI tokens, upper-case for long path variables. */
gchar *insight_custom_expand(const gchar *,GHashTable *,GError **);
gchar **insight_custom_argv(const gchar *,GHashTable *,GError **);
typedef struct { GHashTable *values; gboolean modified; } InsightCustomFile;
void insight_custom_file_free(gpointer);
gchar **insight_custom_argv_files(const gchar *, GHashTable *, GPtrArray *, GError **);
typedef struct {
    gchar *text, *directory, *path;
    gsize length;
    gboolean created;
} InsightCustomSelection;
gboolean insight_custom_selection_requested(const gchar *, GError **);
InsightCustomSelection *insight_custom_selection_plan(const gchar *, gsize, GError **);
gboolean insight_custom_selection_create(InsightCustomSelection *, GError **);
void insight_custom_selection_free(InsightCustomSelection *);
GPtrArray *insight_custom_commands_load(const gchar *,GError **);
gboolean insight_custom_commands_save(const gchar *,GPtrArray *,GError **);
typedef struct { gchar *path,*message;guint line,column; } InsightCustomLink;
/* Recognize one GCC/Clang, MSVC or Python diagnostic. No file access. Relative
 * paths resolve against the captured launch directory; numbers are 1-based. */
InsightCustomLink *insight_custom_source_link(const gchar *,const gchar *);
void insight_custom_link_free(gpointer);
GRegex *insight_custom_diagnostic_regex(const gchar *, GError **);
InsightCustomLink *insight_custom_source_link_pattern(const gchar *, const gchar *, GRegex *, GError **);
#endif
