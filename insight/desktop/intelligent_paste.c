/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "intelligent_paste.h"
#include <string.h>
#define INPUT_LIMIT (1024 * 1024)
#define OUTPUT_LIMIT (4 * 1024 * 1024)
#define COLUMN_LIMIT 4096
#define LINE_LIMIT 10000

typedef struct { const gchar *body; gsize length; guint indent; } Line;

gchar *insight_paste_reindent(const gchar *text, guint indent, guint tab_width,
                            gboolean use_tabs, const gchar *eol)
{
    gsize length = 0;
    if (!text || !eol || indent > COLUMN_LIMIT || tab_width < 1 || tab_width > 32 ||
        (strcmp(eol, "\n") && strcmp(eol, "\r\n") && strcmp(eol, "\r")))
        return NULL;
    while (length <= INPUT_LIMIT && text[length])
        length++;
    if (!length || length > INPUT_LIMIT ||
        (text[length - 1] != '\n' && text[length - 1] != '\r') ||
        !g_utf8_validate(text, length, NULL))
        return NULL;

    GArray *lines = g_array_new(FALSE, FALSE, sizeof(Line));
    guint common = G_MAXUINT;
    const gchar *p = text;
    while (*p)
    {
        guint columns = 0;
        while (*p == ' ' || *p == '\t')
        {
            columns += *p == '\t' ? tab_width - columns % tab_width : 1;
            p++;
            if (columns > COLUMN_LIMIT)
                goto rejected;
        }
        const gchar *body = p;
        while (*p && *p != '\r' && *p != '\n')
            p++;
        Line line = {body, p - body, columns};
        if (line.length)
            common = MIN(common, columns);
        g_array_append_val(lines, line);
        if (lines->len > LINE_LIMIT)
            goto rejected;
        if (*p == '\r' && p[1] == '\n')
            p += 2;
        else if (*p)
            p++;
    }
    if (common == G_MAXUINT)
        common = 0;
    GString *output = g_string_new(NULL);
    for (guint i = 0; i < lines->len; i++)
    {
        const Line *line = &g_array_index(lines, Line, i);
        if (line->length)
        {
            guint columns = indent + line->indent - common;
            if (columns > COLUMN_LIMIT)
            {
                g_string_free(output, TRUE);
                goto rejected;
            }
            if (use_tabs)
                for (guint n = 0; n < columns / tab_width; n++)
                    g_string_append_c(output, '\t');
            guint spaces = use_tabs ? columns % tab_width : columns;
            for (guint n = 0; n < spaces; n++)
                g_string_append_c(output, ' ');
            g_string_append_len(output, line->body, line->length);
        }
        g_string_append(output, eol);
        if (output->len > OUTPUT_LIMIT)
        {
            g_string_free(output, TRUE);
            goto rejected;
        }
    }
    g_array_free(lines, TRUE);
    return g_string_free(output, FALSE);
rejected:
    g_array_free(lines, TRUE);
    return NULL;
}
