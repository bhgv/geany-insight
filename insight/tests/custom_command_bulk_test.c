/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify argument-vector expansion over open/modified file snapshots, bounds and literal substitution.
 * RU: Проверяет раскрытие аргументов по открытым/изменённым файлам, границы и буквальные подстановки. */
#include "../desktop/custom_command.h"
#include <string.h>

/* EN: Add an owned snapshot with suffix-specific values to files; modified controls its selection by %m.
 * RU: Добавляет собственный снимок со значениями suffix в files; modified определяет отбор через %m. */
static void add_file(GPtrArray *files, const gchar *suffix, gboolean modified)
{
    InsightCustomFile *file = g_new0(InsightCustomFile, 1);
    file->values = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    file->modified = modified;
    const gchar *keys[] = {"f", "r", "n", "b", "e"};
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++)
        g_hash_table_insert(file->values, g_strdup(keys[i]), g_strconcat(keys[i], suffix, NULL));
    g_ptr_array_add(files, file);
}

/* EN: Assert input produces the expected NULL-terminated argv without mutating context or snapshots.
 * RU: Проверяет результат input по argv expected с NULL, не меняя context или снимки files. */
static void check(const gchar *input, GHashTable *context, GPtrArray *files, const gchar **expected)
{
    GError *error = NULL;
    gchar **argv = insight_custom_argv_files(input, context, files, &error);
    g_assert_no_error(error);
    g_assert_nonnull(argv);
    guint i = 0;
    for (; expected[i]; i++)
        g_assert_cmpstr(argv[i], ==, expected[i]);
    g_assert_null(argv[i]);
    g_assert_cmpstr(g_hash_table_lookup(context, "f"), ==, "current");
    g_strfreev(argv);
}

/* EN: Require input to fail with an error and no partial argv.
 * RU: Требует отказ input с error без частично подготовленного argv. */
static void reject(const gchar *input, GHashTable *context, GPtrArray *files)
{
    GError *error = NULL;
    g_assert_null(insight_custom_argv_files(input, context, files, &error));
    g_assert_nonnull(error);
    g_clear_error(&error);
}

/* EN: Test all supported filename variants, filtering, escaping, ambiguous arguments and combined launch limits.
 * RU: Проверяет варианты имён, отбор, экранирование, неоднозначные аргументы и общие границы запуска. */
int main(void)
{
    GHashTable *context = g_hash_table_new(g_str_hash, g_str_equal);
    g_hash_table_insert(context, "f", "current");
    g_hash_table_insert(context, "o", "project");
    g_hash_table_insert(context, "PROJECT_NAME", "project-name");
    GPtrArray *files = g_ptr_array_new_with_free_func(insight_custom_file_free);
    add_file(files, " one;$(bad)'%o", FALSE);
    add_file(files, " Пример", TRUE);
    const gchar *all[] = {"tool", "f one;$(bad)'%o", "f Пример", NULL};
    check("tool %f%o", context, files, all);
    const gchar *changed[] = {"tool", "f Пример", NULL};
    check("tool %f%m", context, files, changed);
    const gchar *keys[] = {"r", "n", "b", "e"};
    for (guint i = 0; i < G_N_ELEMENTS(keys); i++)
    {
        gchar *input = g_strdup_printf("tool --file=%%%s%%o", keys[i]);
        gchar *a = g_strconcat("--file=", keys[i], " one;$(bad)'%o", NULL);
        gchar *b = g_strconcat("--file=", keys[i], " Пример", NULL);
        const gchar *expected[] = {"tool", a, b, NULL};
        check(input, context, files, expected);
        g_free(input);
        g_free(a);
        g_free(b);
    }
    const gchar *joined[] = {"tool", "n one;$(bad)'%o=f one;$(bad)'%o", "n Пример=f Пример", NULL};
    check("tool %n=%f%o", context, files, joined);
    const gchar *mixed[] = {"tool", "current", "project", "project-name", "f Пример", "%fproject", NULL};
    check("tool %f %o %PROJECT_NAME% %f%m %%f%o", context, files, mixed);
    const gchar *two[] = {"tool", "f one;$(bad)'%o", "f Пример", "n Пример", NULL};
    check("tool %f%o %n%m", context, files, two);
    reject("tool %f%o:%n%m", context, files);
    reject("%f%o --flag", context, files);
    reject("tool %f%o %unknown%", context, files);
    reject("tool %f%o%", context, files);
    reject("tool %x%o", context, files);
    reject("tool\n%f%o", context, files);
    reject("tool 'unterminated", context, files);
    GPtrArray *empty = g_ptr_array_new_with_free_func(insight_custom_file_free);
    reject("tool %f%o", context, empty);
    reject("tool %f%m", context, empty);
    add_file(empty, "clean", FALSE);
    reject("tool %f%m", context, empty);
    InsightCustomFile *incomplete = g_ptr_array_index(empty, 0);
    g_hash_table_remove(incomplete->values, "f");
    reject("tool %f%o", context, empty);
    g_ptr_array_set_size(empty, 0);
    for (guint i = 0; i < 127; i++)
        add_file(empty, "", TRUE);
    GError *error = NULL;
    gchar **argv = insight_custom_argv_files("tool %f%o", context, empty, &error);
    g_assert_no_error(error);
    g_assert_cmpuint(g_strv_length(argv), ==, 128);
    g_strfreev(argv);
    add_file(empty, "", TRUE);
    reject("tool %f%o", context, empty);
    g_ptr_array_set_size(empty, 0);
    gchar *large = g_strnfill(16383, 'x');
    for (guint i = 0; i < 20; i++)
        add_file(empty, large, TRUE);
    reject("tool %f%o", context, empty);
    g_free(large);
    g_ptr_array_unref(empty);
    g_ptr_array_unref(files);
    g_hash_table_unref(context);
    g_print("Bulk commands: all filename variants, modified filter, Unicode, single-pass literal values, argument boundaries, no partial launch and bounds passed\n");
    return 0;
}
