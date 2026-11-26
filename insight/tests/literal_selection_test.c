/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify complete literal boundaries using the production shared lexical scanner.
 * RU: Проверяет границы завершённых литералов настоящим общим лексическим сканером. */
#include "../desktop/source_mask.h"
#include <string.h>

/* EN: Assert delimiter-adjacent selection of literal in source/dialect with opening/closing widths.
 * RU: Проверяет выделение literal в source/dialect у разделителей ширины opening/closing. */
static void selected(const gchar *source, InsightCodeDialect dialect, const gchar *literal,
                     gsize opening, gsize closing)
{
    const gchar *start = strstr(source, literal);
    g_assert_nonnull(start);
    gsize begin = start - source, end = begin + strlen(literal);
    for (gsize at = begin; at <= end; at++)
    {
        gsize first = G_MAXSIZE, last = G_MAXSIZE;
        gboolean boundary = at <= begin + opening || at >= end - closing;
        gint found = insight_source_select_literal(source, strlen(source), dialect, at, &first, &last, NULL);
        g_assert_cmpint(found, ==, boundary ? 1 : 0);
        if (boundary)
        {
            g_assert_cmpuint(first, ==, begin);
            g_assert_cmpuint(last, ==, end);
        }
        else
        {
            g_assert_cmpuint(first, ==, G_MAXSIZE);
            g_assert_cmpuint(last, ==, G_MAXSIZE);
        }
    }
}

/* EN: Check escaping, raw/triple/long literals, Unicode byte coordinates and adjacent-token precedence.
 * RU: Проверяет экранирование, raw/triple/long литералы, байты Unicode и приоритет соседних литералов. */
static void complete(void)
{
    selected("const char *s = \"привет \\\"мир\\\"\";", INSIGHT_CODE_C, "\"привет \\\"мир\\\"\"", 1, 1);
    selected("char c = '\\'';", INSIGHT_CODE_C, "'\\''", 1, 1);
    selected("auto s = R\"tag(я \"()\"\r\n text)tag\";", INSIGHT_CODE_C, "R\"tag(я \"()\"\r\n text)tag\"", 6, 5);
    selected("\"a\\\r\nb\"", INSIGHT_CODE_C, "\"a\\\r\nb\"", 1, 1);
    selected("x = '''first\nsecond'''", INSIGHT_CODE_PYTHON, "'''first\nsecond'''", 3, 3);
    selected("x = [==[a ]] b\n c]==]", INSIGHT_CODE_LUA, "[==[a ]] b\n c]==]", 4, 4);
    selected("let s = `a ${value}\n b`;", INSIGHT_CODE_JS, "`a ${value}\n b`", 1, 1);
    selected("x='one\ntwo'", INSIGHT_CODE_SHELL, "'one\ntwo'", 1, 1);
    selected("$display(\"данные \\\"ok\\\"\");", INSIGHT_CODE_VERILOG, "\"данные \\\"ok\\\"\"", 1, 1);
    gsize first = 99, last = 99;
    g_assert_cmpint(insight_source_select_literal("\"a\"\"b\"", 6, INSIGHT_CODE_C, 3, &first, &last, NULL), ==, 1);
    g_assert_cmpuint(first, ==, 3);
    g_assert_cmpuint(last, ==, 6);
}

/* EN: Reject comments, numeric apostrophes, escaped HDL names and unfinished/invalid literals without changing outputs.
 * RU: Отклоняет комментарии, числовые апострофы, экранированные имена HDL и незавершённые/некорректные литералы без изменения результатов. */
static void excluded(void)
{
    const struct { const gchar *text; InsightCodeDialect dialect; } cases[] = {
        {"/* \"hidden\" 'x' */ // \"other\"", INSIGHT_CODE_C},
        {"1'000 + 0xA'B", INSIGHT_CODE_C},
        {"8'hff; '0; \\name\"quoted ", INSIGHT_CODE_VERILOG},
        {"--[=[ \"comment\" ]=]", INSIGHT_CODE_LUA},
        {"# 'comment'", INSIGHT_CODE_PYTHON},
        {"\"unfinished", INSIGHT_CODE_C},
        {"\"line\nbreak\"", INSIGHT_CODE_C},
        {"R\"tag(unfinished)", INSIGHT_CODE_C},
        {"'''unfinished", INSIGHT_CODE_PYTHON},
        {"[==[unfinished", INSIGHT_CODE_LUA}
    };
    for (guint i = 0; i < G_N_ELEMENTS(cases); i++)
        for (gsize at = 0; at <= strlen(cases[i].text); at++)
        {
            gsize first = 99, last = 99;
            g_assert_cmpint(insight_source_select_literal(cases[i].text, strlen(cases[i].text),
                cases[i].dialect, at, &first, &last, NULL), ==, 0);
            g_assert_cmpuint(first, ==, 99);
            g_assert_cmpuint(last, ==, 99);
        }
    gsize first = 99, last = 99;
    GError *error = NULL;
    g_assert_cmpint(insight_source_select_literal("x", 1, INSIGHT_CODE_C, 2, &first, &last, &error), ==, -1);
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
    g_clear_error(&error);
    g_assert_cmpint(insight_source_select_literal("\xff", 1, INSIGHT_CODE_C, 0, &first, &last, &error), ==, -1);
    g_assert_error(error, G_FILE_ERROR, G_FILE_ERROR_INVAL);
    g_clear_error(&error);
}

/* EN: Run GLib tests using argc/argv; no UI, parser service or external utility is involved.
 * RU: Запускает проверки GLib с argc/argv без интерфейса, сервиса парсера или внешних утилит. */
int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/literal/complete", complete);
    g_test_add_func("/literal/excluded", excluded);
    return g_test_run();
}
