/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify HDL region masks around literals, escaped names and comments.
 * RU: Проверяет HDL-маски вокруг литералов, экранированных имён и комментариев. */
#include "source_mask.h"
#include <string.h>

/* EN: Assert mask preserves code and masks comments/strings in text; masks use
 * original byte offsets. Exercise display and binary membership representations.
 * RU: Проверяет сохранение кода и маскирование строк/комментариев text в исходных
 * байтовых координатах. Проверяет текстовую и бинарную формы принадлежности. */
static void check(const gchar *text)
{
    gsize size = strlen(text);
    gchar *code = insight_source_code_mask(text, size, INSIGHT_CODE_VERILOG, NULL);
    gchar *comments = insight_source_region_mask(text, size, INSIGHT_CODE_VERILOG,
        INSIGHT_REGION_COMMENTS, NULL);
    guchar *membership = insight_source_region_membership(text, size, INSIGHT_CODE_VERILOG,
        INSIGHT_REGION_CODE, NULL);
    g_assert_nonnull(code);
    g_assert_nonnull(comments);
    g_assert_nonnull(membership);
    g_assert_cmpuint(strlen(code), ==, size);
    const gchar *live = strstr(text, "target");
    g_assert_nonnull(live);
    g_assert_cmpmem(code + (live - text), 6, "target", 6);
    g_assert_cmpint(membership[live - text], ==, 1);
    const gchar *comment = strstr(text, "hidden");
    if (comment)
    {
        g_assert_cmpmem(code + (comment - text), 6, "      ", 6);
        g_assert_cmpmem(comments + (comment - text), 6, "hidden", 6);
        g_assert_cmpint(membership[comment - text], ==, 0);
    }
    const gchar *quoted = strstr(text, "quoted");
    if (quoted)
    {
        g_assert_cmpmem(code + (quoted - text), 6, "      ", 6);
        g_assert_cmpmem(comments + (quoted - text), 6, "      ", 6);
    }
    for (gsize n = 0; n < size; n++)
        if (text[n] == '\r' || text[n] == '\n')
            g_assert_cmpint(code[n], ==, text[n]);
    g_free(code);
    g_free(comments);
    g_free(membership);
}

/* EN: Run lexical HDL regressions; return zero after all assertions pass.
 * RU: Запускает лексические HDL-регрессии; возвращает ноль после всех проверок. */
int main(void)
{
    check("1'b0; target = '0; // hidden\r\n\"quoted // not comment\";");
    check("8'shff; target = int'(4); /* hidden */ \"quoted\";");
    check("'{default: 0}; target = 'x; /* hidden */");
    check("\\name//with\"quotes target; // hidden\r\n");
    check("// hidden\\\r\n target; \"quoted\\\"tail\";");
    check("target; /* unterminated hidden");
    g_print("Verilog source regions: numbers, casts, assignment patterns, escaped names, strings, comments and CRLF passed\n");
    return 0;
}
