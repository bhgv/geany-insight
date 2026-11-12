/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Match register snapshots by changed state and Unicode-insensitive name, address or resolved label.
 * RU: Отбирает снимки регистров по изменениям и имени, адресу или подписи без учёта регистра Unicode. */

/* EN: Return whether name/hex/label pass query (complete hex addresses compare numerically) and only_changed; changed is the snapshot comparison flag, no target is read.
 * RU: Проверяет name/hex/label по query (полный hex-адрес сравнивается численно) и only_changed; changed — признак сравнения снимков, чтения цели нет. */
static gboolean native_register_matches(const gchar *name, const gchar *hex, const gchar *label,
                                        gboolean changed, const gchar *query, gboolean only_changed)
{
    if (only_changed && !changed)
        return FALSE;
    if (!query || !*query)
        return TRUE;
    gchar *needle = g_utf8_casefold(query, -1);
    const gchar *values[] = {name, hex, label};
    gboolean found = FALSE;
    guint64 wanted = 0, actual = 0;
    if (g_str_has_prefix(needle, "0x") && hex && g_str_has_prefix(hex, "0x") &&
        g_ascii_string_to_unsigned(needle + 2, 16, 0, G_MAXUINT64, &wanted, NULL) &&
        g_ascii_string_to_unsigned(hex + 2, 16, 0, G_MAXUINT64, &actual, NULL))
    {
        g_free(needle);
        return wanted == actual;
    }
    for (guint i = 0; i < G_N_ELEMENTS(values) && !found; i++)
    {
        gchar *value = g_utf8_casefold(values[i] ? values[i] : "", -1);
        found = strstr(value, needle) != NULL;
        g_free(value);
    }
    g_free(needle);
    return found;
}
