/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Show and copy GDB-provided instruction bytes from an existing snapshot without querying or running the target.
 * RU: Показывает и копирует байты инструкций GDB из готового снимка без запросов к цели и её исполнения. */

/* EN: Return owned validated opcode bytes at exact address in assembly's cached record, or NULL when unavailable.
 * RU: Возвращает проверенные байты точного address из сохранённого record assembly либо NULL при отсутствии. */
static gchar *native_opcode_at(NativeAssembly *assembly, const gchar *address)
{
    guint64 wanted = 0;
    if (!assembly->record || !native_assembly_number(address, &wanted))
        return NULL;
    const gchar *cursor = strstr(assembly->record, "asm_insns=["), *start, *end;
    if (!cursor)
        return NULL;
    cursor += strlen("asm_insns=[");
    guint count = 0;
    while (count++ < 256 && (end = native_next_tuple(&cursor, &start)))
    {
        gchar *tuple = g_strndup(start, end - start);
        gchar *raw = native_debug_field(tuple, "address");
        guint64 actual = 0;
        gboolean match = native_assembly_number(raw, &actual) && actual == wanted;
        gchar *opcodes = match ? native_debug_field(tuple, "opcodes") : NULL;
        gchar *result = native_assembly_bytes(opcodes);
        g_free(opcodes);
        g_free(raw);
        g_free(tuple);
        if (match)
            return result;
    }
    return NULL;
}

/* EN: Apply button's bytes visibility to session data and repaint the cached snapshot, preserving its cursor row.
 * RU: Применяет видимость байтов button к session data и перерисовывает сохранённый снимок с сохранением строки курсора. */
static void native_opcode_toggled(GtkToggleButton *button, gpointer data)
{
    NativeDebug *session = data;
    session->assembly->show_bytes = gtk_toggle_button_get_active(button);
    native_instruction_refresh(session);
}

/* EN: Copy item's captured snapshot content to the clipboard; data is unused, so copying also works while running.
 * RU: Копирует сохранённое содержимое снимка item в буфер обмена; data не используется, копирование доступно и во время выполнения. */
static void native_opcode_copy(GtkWidget *item, gpointer data)
{
    (void)data;
    const gchar *text = g_object_get_data(G_OBJECT(item), "instruction-copy-text");
    if (text)
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
}
