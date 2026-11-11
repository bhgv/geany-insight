/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Resolve saved disabled C++ locations against fresh GDB numbers without guessing ambiguous matches.
 * RU: Сопоставляет выключенные подпозиции C++ с новыми номерами GDB без угадывания неоднозначных совпадений. */

/* EN: Free pending override data, including its selector and current-session parent number.
 * RU: Освобождает ожидающее правило data, селектор и номер родителя текущего сеанса. */
static void native_location_override_free(gpointer data)
{
    NativeLocationOverride *override = data;
    g_free(override->parent_number);
    native_saved_location_free(override->location);
    g_free(override);
}

/* EN: Free candidate data and its owned concrete GDB location number.
 * RU: Освобождает кандидата data и принадлежащий ему номер подпозиции GDB. */
static void native_location_candidate_free(gpointer data)
{
    NativeLocationCandidate *candidate = data;
    g_free(candidate->number);
    g_free(candidate);
}

/* EN: Add tuple's concrete location to candidates, marking repeated selectors with distinct IDs ambiguous.
 * RU: Добавляет подпозицию tuple в candidates; одинаковые селекторы с разными ID отмечает неоднозначными. */
static void native_location_candidate_add(GHashTable *candidates, const gchar *tuple)
{
    gchar *number = native_debug_field(tuple, "number");
    const gchar *dot = number ? strchr(number, '.') : NULL;
    NativeSavedLocation *location = dot && native_point_number(number) ? native_profile_location_parse(tuple) : NULL;
    if (location)
    {
        gchar *parent = g_strndup(number, dot - number);
        gchar *key = native_profile_location_key(parent, location);
        NativeLocationCandidate *candidate = g_hash_table_lookup(candidates, key);
        if (!candidate)
        {
            candidate = g_new0(NativeLocationCandidate, 1);
            candidate->number = g_strdup(number);
            candidate->matches = 1;
            gchar *enabled = native_debug_field(tuple, "enabled");
            candidate->enabled = g_strcmp0(enabled, "n") != 0;
            g_free(enabled);
            g_hash_table_insert(candidates, g_strdup(key), candidate);
        }
        else if (g_strcmp0(candidate->number, number))
            candidate->matches++;
        g_free(key);
        g_free(parent);
        native_saved_location_free(location);
    }
    g_free(number);
}

/* EN: Queue uniquely matched disabled selectors in session from record; return the number still awaiting symbols.
 * RU: Ставит однозначные выключения session из record в очередь; возвращает число правил, ожидающих символов. */
static guint native_profile_locations_apply(NativeDebug *session, const gchar *record)
{
    const gchar *body = strstr(record, "body=[");
    if (!body || !session->profile_disabled_pending->len)
        return 0;
    GHashTable *candidates = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, native_location_candidate_free);
    const gchar *cursor = body + strlen("body=["), *start, *end;
    guint scanned = 0;
    while (scanned++ < 4096 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        native_location_candidate_add(candidates, tuple);
        const gchar *nested = strstr(tuple, "locations=[");
        if (nested)
        {
            nested += strlen("locations=[");
            const gchar *child_start, *child_end;
            while (scanned++ < 4096 && (child_end = native_next_tuple(&nested, &child_start)))
            {
                gchar *child = g_strndup(child_start, child_end - child_start);
                native_location_candidate_add(candidates, child);
                g_free(child);
            }
        }
        g_free(tuple);
    }
    guint pending = 0;
    for (guint i = 0; i < session->profile_disabled_pending->len; i++)
    {
        NativeLocationOverride *override = session->profile_disabled_pending->pdata[i];
        if (override->applied)
            continue;
        gchar *key = override->parent_number ? native_profile_location_key(override->parent_number, override->location) : NULL;
        NativeLocationCandidate *candidate = key ? g_hash_table_lookup(candidates, key) : NULL;
        if (candidate && candidate->matches == 1)
        {
            override->applied = TRUE;
            if (candidate->enabled)
            {
                g_queue_push_tail(&session->launch_commands, g_strdup_printf("-break-disable %s", candidate->number));
                candidate->enabled = FALSE;
                session->profile_restoring = session->points_needed = TRUE;
            }
        }
        else
            pending++;
        g_free(key);
    }
    g_hash_table_unref(candidates);
    return pending;
}
