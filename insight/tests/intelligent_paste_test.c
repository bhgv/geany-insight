/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/intelligent_paste.h"
#include <string.h>
static void check(const gchar *text, guint indent, guint tab, gboolean tabs,
                  const gchar *eol, const gchar *expected)
{
    gchar *actual = insight_paste_reindent(text, indent, tab, tabs, eol);
    g_assert_cmpstr(actual, ==, expected);
    g_free(actual);
}
int main(void)
{
    check("    a();\n        b();\n", 2, 4, FALSE, "\n", "  a();\n      b();\n");
    check("\ta();\r\n\t  b();\r\n", 8, 4, TRUE, "\r\n", "\t\ta();\r\n\t\t  b();\r\n");
    check("   \n  a\r    b\r\n\n", 0, 4, FALSE, "\n", "\na\n  b\n\n");
    check("    Привет();\n", 4, 8, FALSE, "\r", "    Привет();\r");
    check("\n\n", 8, 4, TRUE, "\n", "\n\n");
    gchar *edge = insight_paste_reindent("a\n", 4096, 32, TRUE, "\n");
    g_assert_nonnull(edge);g_assert_cmpuint(strlen(edge), ==, 130);g_free(edge);
    check("", 0, 4, FALSE, "\n", NULL);
    check("word", 0, 4, FALSE, "\n", NULL);
    check("a\n  ", 0, 4, FALSE, "\n", NULL);
    check("\xff\n", 0, 4, FALSE, "\n", NULL);
    check(NULL, 0, 4, FALSE, "\n", NULL);
    check("a\n", 4097, 4, FALSE, "\n", NULL);
    check("a\n", 0, 0, FALSE, "\n", NULL);
    check("a\n", 0, 33, FALSE, "\n", NULL);
    check("a\n", 0, 4, FALSE, "bad", NULL);
    check("a\n b\n", 4096, 4, FALSE, "\n", NULL);
    gchar *long_line = g_strnfill(4097, ' '), *input = g_strconcat(long_line, "a\n", NULL);
    check(input, 0, 4, FALSE, "\n", NULL);g_free(input);g_free(long_line);
    GString *many = g_string_new(NULL);
    for (guint i = 0; i < 10001; i++) g_string_append(many, "a\n");
    check(many->str, 0, 4, FALSE, "\n", NULL);g_string_free(many, TRUE);
    gchar *large = g_strnfill(1024*1024+1, 'a');large[1024*1024]='\n';
    check(large, 0, 4, FALSE, "\n", NULL);g_free(large);
    many = g_string_new(NULL);
    for (guint i = 0; i < 1100; i++) g_string_append(many, "a\n");
    check(many->str, 4096, 4, FALSE, "\n", NULL);g_string_free(many, TRUE);
    g_print("Intelligent paste: relative indentation, tabs, blank lines, Unicode, CR/LF/CRLF and bounded rejection passed\n");
    return 0;
}
