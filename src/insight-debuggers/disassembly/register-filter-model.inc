/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Match register snapshots by changed state and Unicode-insensitive name, address or resolved label.
 * RU: Отбирает снимки регистров по изменениям и имени, адресу или подписи без учёта регистра Unicode. */

/* EN: Return whether name/hex/label pass query and only_changed; changed is the snapshot comparison flag, no target is read.
 * RU: Проверяет name/hex/label по query и only_changed; changed — признак сравнения снимков, чтения цели нет. */
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
    for (guint i = 0; i < G_N_ELEMENTS(values) && !found; i++)
    {
        gchar *value = g_utf8_casefold(values[i] ? values[i] : "", -1);
        found = strstr(value, needle) != NULL;
        g_free(value);
    }
    g_free(needle);
    return found;
}
