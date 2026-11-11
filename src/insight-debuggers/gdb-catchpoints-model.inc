/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Build C++ exception catchpoints and readable descriptions from GDB metadata.
 * RU: Создаёт точки на исключениях C++ и понятные описания по данным GDB. */

/* EN: Return an owned MI command for event and optional type filter; reject unknown events or unsafe multiline input.
 * RU: Возвращает команду MI для event и необязательного фильтра типа filter; отклоняет неизвестные события и многострочность. */
static gchar *native_catch_command(guint event, const gchar *filter)
{
    const gchar *events[] = {"throw", "catch", "rethrow"};
    const gchar *text = filter ? filter : "";
    if (event >= G_N_ELEMENTS(events) || strlen(text) > 4096 ||
        !g_utf8_validate(text, -1, NULL) || g_utf8_strlen(text, -1) > 1024 || strpbrk(text, "\r\n"))
        return NULL;
    if (!*text)
        return g_strdup_printf("-catch-%s", events[event]);
    gchar *escaped = g_strescape(text, NULL);
    gchar *command = g_strdup_printf("-catch-%s -r \"%s\"", events[event], escaped);
    g_free(escaped);
    return command;
}

/* EN: Describe a known C++ catchpoint tuple, including its regex; return NULL for other breakpoint kinds.
 * RU: Описывает известную точку C++ из tuple вместе с регулярным выражением; для других видов возвращает NULL. */
static gchar *native_catch_description(const gchar *tuple)
{
    gchar *kind = native_debug_field(tuple, "type");
    gchar *event = native_debug_field(tuple, "catch-type");
    const gchar *caption = !g_strcmp0(event, "throw") ? "C++ throw" :
        !g_strcmp0(event, "catch") ? "C++ catch" : !g_strcmp0(event, "rethrow") ? "C++ rethrow" : NULL;
    gchar *result = NULL;
    if (!g_strcmp0(kind, "catchpoint") && caption)
    {
        gchar *filter = native_debug_field(tuple, "regexp");
        result = filter && *filter ? g_strdup_printf("%s [%s]", insight_tr(caption), filter) :
                                    g_strdup(insight_tr(caption));
        g_free(filter);
    }
    g_free(event);
    g_free(kind);
    return result;
}
