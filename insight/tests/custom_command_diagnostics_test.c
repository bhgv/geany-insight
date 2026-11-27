/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify named diagnostic captures, path/coordinate validation, bounded matching and atomic profile compatibility.
 * RU: Проверяет именованные группы диагностики, пути/координаты, ограничение поиска и совместимость атомарного сохранения. */
#include "../desktop/custom_command.h"
#include <glib/gstdio.h>
#include <string.h>

/* EN: Compile pattern and require a configuration error, freeing any returned resources.
 * RU: Компилирует pattern и требует ошибку настройки, освобождая ресурсы. */
static void invalid_pattern(const gchar *pattern)
{
    GError *error = NULL;
    g_assert_null(insight_custom_diagnostic_regex(pattern, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
}

/* EN: Verify custom parsing and persisted backward-compatible diagnostic settings without accessing source files.
 * RU: Проверяет свой разбор и сохраняемые совместимые настройки диагностики без чтения исходников. */
int main(void)
{
    GError *error = NULL;
    g_assert_null(insight_custom_diagnostic_regex("", &error));
    g_assert_no_error(error);
    invalid_pattern("(");
    invalid_pattern("(?<file>.+)");
    invalid_pattern("(?<line>[0-9]+)");
    invalid_pattern("(?<file>.+)\n(?<line>[0-9]+)");
    gchar *large = g_strnfill(1025, 'x');
    invalid_pattern(large);
    g_free(large);
    const gchar *pattern = "^S\\|(?<file>[^|]+)\\|(?<line>[^|]+)\\|(?<column>[^|]*)\\|(?<message>.*)$";
    GRegex *regex = insight_custom_diagnostic_regex(pattern, &error);
    g_assert_no_error(error);
    g_assert_nonnull(regex);
    gchar *directory = g_dir_make_tmp("diagnostic-pattern-XXXXXX", NULL);
    InsightCustomLink *link = insight_custom_source_link_pattern("\033[31mS|\"src/Пример name.v\"|12|3|warning\033[0m", directory, regex, &error);
    g_assert_no_error(error);
    g_assert_nonnull(link);
    gchar *path = g_build_filename(directory, "src", "Пример name.v", NULL);
    g_assert_cmpstr(link->path, ==, path);
    g_assert_cmpuint(link->line, ==, 12);
    g_assert_cmpuint(link->column, ==, 3);
    g_assert_cmpstr(link->message, ==, "warning");
    insight_custom_link_free(link);
    g_free(path);
    const gchar *bad[] = {"S|file.v|0|1|bad", "S|file.v|12junk|1|bad", "S|file.v|1|0|bad",
        "S|file.v|99999999999999999999|1|bad", "S|https://host/x|1|1|bad", "S|<stdin>|1|1|bad", "unmatched"};
    for (guint i = 0; i < G_N_ELEMENTS(bad); i++)
    {
        g_assert_null(insight_custom_source_link_pattern(bad[i], directory, regex, &error));
        g_assert_no_error(error);
    }
    g_regex_unref(regex);
    regex = insight_custom_diagnostic_regex("^L(?<line>[0-9]+)@(?<file>.+)$", &error);
    g_assert_no_error(error);
    link = insight_custom_source_link_pattern("L3@file.inc", directory, regex, &error);
    g_assert_nonnull(link);
    g_assert_cmpuint(link->line, ==, 3);
    g_assert_cmpuint(link->column, ==, 0);
    g_assert_cmpstr(link->message, ==, "L3@file.inc");
    insight_custom_link_free(link);
    g_regex_unref(regex);
    regex = insight_custom_diagnostic_regex("(*NO_START_OPT)(*NO_AUTO_POSSESS)^(?<file>(a+)+)(?<line>1)$", &error);
    g_assert_no_error(error);
    gchar *many = g_strnfill(4096, 'a');
    gchar *subject = g_strconcat(many, "1X", NULL);
    g_assert_null(insight_custom_source_link_pattern(subject, directory, regex, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_free(subject);
    g_free(many);
    g_regex_unref(regex);
    path = g_build_filename(directory, "commands.conf", NULL);
    const gchar *legacy = "[commands]\ncount=1\n[command-0]\nname=old\ncommand=tool %f\ndirectory=%d\n";
    g_assert_true(g_file_set_contents(path, legacy, -1, NULL));
    GPtrArray *list = insight_custom_commands_load(path, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(list->len, ==, 1);
    InsightCustomCommand *command = g_ptr_array_index(list, 0);
    g_assert_false(command->parse_on_finish);
    g_assert_null(command->diagnostic_pattern);
    g_assert_null(command->shortcut);
    command->shortcut = g_strdup("<Control><Shift>F9");
    command->diagnostic_pattern = g_strdup(pattern);
    command->parse_on_finish = TRUE;
    InsightCustomCommand *copy = insight_custom_command_copy(command);
    g_assert_cmpstr(copy->diagnostic_pattern, ==, pattern);
    g_assert_true(copy->parse_on_finish);
    g_assert_cmpstr(copy->shortcut, ==, "<Control><Shift>F9");
    insight_custom_command_free(copy);
    g_assert_true(insight_custom_commands_save(path, list, &error));
    g_assert_no_error(error);
    GPtrArray *loaded = insight_custom_commands_load(path, &error);
    g_assert_no_error(error);
    copy = g_ptr_array_index(loaded, 0);
    g_assert_cmpstr(copy->diagnostic_pattern, ==, pattern);
    g_assert_true(copy->parse_on_finish);
    g_assert_cmpstr(copy->shortcut, ==, "<Control><Shift>F9");
    g_ptr_array_unref(loaded);
    gchar *before = NULL;
    g_assert_true(g_file_get_contents(path, &before, NULL, NULL));
    g_free(command->diagnostic_pattern);
    command->diagnostic_pattern = g_strdup("(");
    g_assert_false(insight_custom_commands_save(path, list, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    gchar *after = NULL;
    g_assert_true(g_file_get_contents(path, &after, NULL, NULL));
    g_assert_cmpstr(before, ==, after);
    g_free(before);
    g_free(after);
    g_ptr_array_unref(list);
    const gchar *malformed = "[commands]\ncount=1\n[command-0]\nname=bad\ncommand=tool\ndirectory=%d\nparse-on-finish=wrong\n";
    g_assert_true(g_file_set_contents(path, malformed, -1, NULL));
    g_assert_null(insight_custom_commands_load(path, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    g_remove(path);
    g_rmdir(directory);
    g_free(path);
    g_free(directory);
    g_print("Diagnostic patterns: required/optional/reordered groups, ANSI/Unicode paths, strict coordinates, unsafe paths, match limits, legacy defaults, deep copies and atomic persistence passed\n");
    return 0;
}
