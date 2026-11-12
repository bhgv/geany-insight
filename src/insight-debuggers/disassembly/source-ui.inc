/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Navigate from an instruction to its exact local source while leaving the real execution stop marker untouched.
 * RU: Переводит от инструкции к точному локальному исходнику, сохраняя настоящий маркер остановки исполнения. */

/* EN: Resolve work's captured address from record for session; open an existing source or show a translated, nonfatal status.
 * RU: Разрешает сохранённый адрес work по record для session; открывает существующий исходник либо показывает переведённый статус без завершения отладки. */
static void native_instruction_source_reply(NativeDebug *session, NativeAssemblyWork *work, const gchar *record)
{
    guint line = 0;
    gchar *file = native_instruction_source_location(record, work->expression, &line);
    gchar *native = file ? utils_get_locale_from_utf8(file) : NULL;
    if (native && g_path_is_absolute(native) && g_file_test(native, G_FILE_TEST_IS_REGULAR))
    {
        diff_source_open(native, line);
        gtk_label_set_text(GTK_LABEL(session->assembly->status), insight_tr("Instruction source opened. The execution stop has not changed."));
    }
    else
    {
        gchar *error = g_str_has_prefix(record, "^error") ? native_debug_field(record, "msg") : NULL;
        gtk_label_set_text(GTK_LABEL(session->assembly->status), error ? error : file ?
            insight_tr("Source file is unavailable locally. Check GDB source directories or SSH source mapping.") :
            insight_tr("GDB has no source line for this instruction."));
        g_free(error);
    }
    g_free(native);
    g_free(file);
}

/* EN: Queue an exact mixed-source lookup for item's captured address in session data; only an idle paused session may query GDB.
 * RU: Ставит точный запрос исходника сохранённого адреса item в session data; GDB запрашивается только на свободной паузе. */
static void native_instruction_source(GtkWidget *item, gpointer data)
{
    NativeDebug *session = data;
    if (!native_instruction_idle(session))
        return;
    const gchar *address = g_object_get_data(G_OBJECT(item), "instruction-address");
    guint64 number = 0;
    if (!native_assembly_number(address, &number) || number == G_MAXUINT64)
        return;
    gchar *command = g_strdup_printf("-data-disassemble -s \"%s\" -e \"(%s)+1\" -- 1", address, address);
    session->assembly->source_only = TRUE;
    session->assembly->active = TRUE;
    native_assembly_queue(session->assembly, command, 16, address, NULL, NULL, NULL, 0, FALSE);
    gtk_label_set_text(GTK_LABEL(session->assembly->status), insight_tr("Looking up instruction source…"));
    native_debug_resume(session->window, session);
}
