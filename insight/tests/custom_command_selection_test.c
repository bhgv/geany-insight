/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify selection-token detection, exact UTF-8 snapshots, private creation and cleanup ownership.
 * RU: Проверяет распознавание %s, точные снимки UTF-8, частное создание и владение при очистке. */
#include "../desktop/custom_command.h"
#include <glib/gstdio.h>
#include <string.h>
#include <sys/stat.h>

/* EN: Require text/length to reject planning with error and no resources.
 * RU: Требует отказа планирования text/length с error без создания ресурсов. */
static void reject(const gchar *text, gsize length)
{
    GError *error = NULL;
    g_assert_null(insight_custom_selection_plan(text, length, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
}

/* EN: Check quoted/escaped request detection and snapshot lifecycle without editor dependencies.
 * RU: Проверяет распознавание кавычек/экранирования и жизнь снимка без зависимостей редактора. */
int main(void)
{
    const gchar *yes[] = {"tool %s", "tool --input=%s", "tool '%s' %s", "tool %''s", "tool %f%o %s"};
    const gchar *no[] = {"tool %%s", "tool %1", "tool %SYSTEM_DIR%", "tool %S%", "tool literal"};
    GError *error = NULL;
    for (guint i = 0; i < G_N_ELEMENTS(yes); i++)
    {
        g_assert_true(insight_custom_selection_requested(yes[i], &error));
        g_assert_no_error(error);
    }
    for (guint i = 0; i < G_N_ELEMENTS(no); i++)
    {
        g_assert_false(insight_custom_selection_requested(no[i], &error));
        g_assert_no_error(error);
    }
    g_assert_false(insight_custom_selection_requested("tool 'broken", &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    reject("", 0);
    reject("a\0b", 3);
    reject("\xff", 1);
    gchar *large = g_strnfill(1024 * 1024 + 1, 'x');
    reject(large, 1024 * 1024 + 1);
    g_free(large);
    gchar source[] = "я\r\nline\t";
    gchar *expected = g_strdup(source);
    InsightCustomSelection *snapshot = insight_custom_selection_plan(source, strlen(source), &error);
    g_assert_no_error(error);
    g_assert_nonnull(snapshot);
    g_assert_true(g_path_is_absolute(snapshot->path));
    g_assert_false(g_file_test(snapshot->directory, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(snapshot->path, G_FILE_TEST_EXISTS));
    source[0] = 'X';
    g_assert_true(insight_custom_selection_create(snapshot, &error));
    g_assert_no_error(error);
    gchar *actual = NULL;
    gsize length;
    g_assert_true(g_file_get_contents(snapshot->path, &actual, &length, &error));
    g_assert_no_error(error);
    g_assert_cmpuint(length, ==, strlen(expected));
    g_assert_cmpmem(actual, length, expected, strlen(expected));
#ifndef G_OS_WIN32
    GStatBuf info;
    g_assert_cmpint(g_stat(snapshot->directory, &info), ==, 0);
    g_assert_cmpuint(info.st_mode & 0777, ==, 0700);
    g_assert_cmpint(g_stat(snapshot->path, &info), ==, 0);
    g_assert_cmpuint(info.st_mode & 0777, ==, 0600);
#endif
    g_assert_false(insight_custom_selection_create(snapshot, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    gchar *path = g_strdup(snapshot->path);
    gchar *directory = g_strdup(snapshot->directory);
    insight_custom_selection_free(snapshot);
    g_assert_false(g_file_test(path, G_FILE_TEST_EXISTS));
    g_assert_false(g_file_test(directory, G_FILE_TEST_EXISTS));
    g_free(path);
    g_free(directory);
    g_free(actual);
    g_free(expected);
    snapshot = insight_custom_selection_plan("collision", 9, &error);
    directory = g_strdup(snapshot->directory);
    path = g_build_filename(directory, "keep", NULL);
    g_assert_cmpint(g_mkdir(directory, 0700), ==, 0);
    g_assert_true(g_file_set_contents(path, "existing", -1, &error));
    g_assert_false(insight_custom_selection_create(snapshot, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    insight_custom_selection_free(snapshot);
    g_assert_true(g_file_test(path, G_FILE_TEST_IS_REGULAR));
    g_assert_cmpint(g_remove(path), ==, 0);
    g_assert_cmpint(g_rmdir(directory), ==, 0);
    g_free(path);
    g_free(directory);
    snapshot = insight_custom_selection_plan("write failure", 13, &error);
    g_free(snapshot->path);
    snapshot->path = g_build_filename(snapshot->directory, "missing", "selection.utf8", NULL);
    directory = g_strdup(snapshot->directory);
    g_assert_false(insight_custom_selection_create(snapshot, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
    insight_custom_selection_free(snapshot);
    g_assert_false(g_file_test(directory, G_FILE_TEST_EXISTS));
    g_free(directory);
    g_print("Selection snapshots: token escaping, quote concatenation, exact UTF-8/CRLF, bounds, private permissions, immutable copy, collisions, failed writes and cleanup passed\n");
    return 0;
}
