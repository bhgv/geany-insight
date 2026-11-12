/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Export the visible register model as bounded UTF-8 TSV with explicit escaping inside cells.
 * RU: Экспортирует видимую модель регистров в ограниченный UTF-8 TSV с явным экранированием внутри ячеек. */

/* EN: Return owned TSV-safe text for value, preserving Unicode and escaping backslashes, tabs and line/control breaks.
 * RU: Возвращает текст value для TSV, сохраняя Unicode и экранируя обратную косую черту, табуляцию и управляющие переносы. */
static gchar *native_register_tsv_cell(const gchar *value)
{
    GString *text = g_string_new("");
    for (const unsigned char *p = (const unsigned char *)(value ? value : ""); *p; p++)
    {
        if (*p == '\\')
            g_string_append(text, "\\\\");
        else if (*p == '\t')
            g_string_append(text, "\\t");
        else if (*p == '\r')
            g_string_append(text, "\\r");
        else if (*p == '\n')
            g_string_append(text, "\\n");
        else if (*p < 32 || *p == 127)
            g_string_append_printf(text, "\\x%02x", *p);
        else
            g_string_append_c(text, (gchar)*p);
    }
    return g_string_free(text, FALSE);
}

/* EN: Export model's displayed order to owned TSV, capped at 128 rows/128 KiB; set limited if whole rows are omitted.
 * RU: Экспортирует показанный порядок model в TSV до 128 строк/128 КиБ; limited отмечает пропуск целых строк. */
static gchar *native_register_export(GtkTreeModel *model, gboolean *limited)
{
    const gchar *headers[] = {"Register", "Hex", "Unsigned value", "Resolved name / field", "Changed"};
    GString *text = g_string_new("");
    *limited = FALSE;
    for (guint i = 0; i < G_N_ELEMENTS(headers); i++)
    {
        gchar *field = native_register_tsv_cell(insight_tr(headers[i]));
        g_string_append(text, field);
        g_string_append_c(text, i == 4 ? '\n' : '\t');
        g_free(field);
    }
    GtkTreeIter row;
    guint count = 0;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        if (count++ == 128)
        {
            *limited = TRUE;
            break;
        }
        GString *line = g_string_new("");
        for (guint column = 0; column < 5; column++)
        {
            gchar *value = NULL;
            gtk_tree_model_get(model, &row, column, &value, -1);
            gchar *field = native_register_tsv_cell(value);
            g_string_append(line, field);
            g_string_append_c(line, column == 4 ? '\n' : '\t');
            g_free(field);
            g_free(value);
        }
        if (text->len + line->len > 131072)
        {
            *limited = TRUE;
            g_string_free(line, TRUE);
            break;
        }
        g_string_append(text, line->str);
        g_string_free(line, TRUE);
    }
    return g_string_free(text, FALSE);
}
