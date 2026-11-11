/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Apply the same global address names to value displays, memory and watch graphs without changing raw GDB values.
 * RU: Применяет единые глобальные имена к значениям, памяти и графам без изменения исходных значений GDB. */

/* EN: Refresh session's display-only annotations from canonical locals, watches and the last valid memory snapshot.
 * RU: Обновляет только отображаемые подписи session по исходным переменным, наблюдениям и последнему снимку памяти. */
static void native_name_refresh_views(NativeDebug *session)
{
    GtkTreeIter row;
    GtkTreeModel *model = GTK_TREE_MODEL(session->local_rows);
    guint index = 0;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row);
        valid && index < session->locals->len; valid = gtk_tree_model_iter_next(model, &row))
    {
        NativeWatch *local = session->locals->pdata[index++];
        gchar *text = native_name_addresses(session, local->value);
        gtk_list_store_set(session->local_rows, &row, 1, text, -1);
        g_free(text);
    }
    native_watch_refresh(session, FALSE);
    if (session->value_tree)
        gtk_widget_queue_draw(session->value_tree);
    if (session->memory_text)
    {
        const gchar *raw = g_object_get_data(G_OBJECT(session->memory_text), "native-memory-raw");
        if (raw)
        {
            gchar *text = native_name_line_addresses(session, raw);
            debug_set_text(session->memory_text, text);
            g_free(text);
        }
    }
}

/* EN: Drop function-local resolutions when session changes thread/frame; global names remain usable in this process.
 * RU: Сбрасывает локальные разрешения при смене потока/кадра session; глобальные имена остаются действующими в процессе. */
static void native_assembly_context(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly || assembly->active ||
        (!assembly->function && !assembly->ranges->len) ||
        (!g_strcmp0(assembly->context_thread, session->selected_thread) &&
         !g_strcmp0(assembly->context_frame, session->selected_frame)))
        return;
    for (guint i = 0; i < assembly->ranges->len;)
    {
        NativeNamedRange *range = assembly->ranges->pdata[i];
        if (range->frame || (range->scope && *range->scope))
            g_ptr_array_remove_index(assembly->ranges, i);
        else
            i++;
    }
    assembly->instruction_pc_valid = FALSE;
    native_instruction_refresh(session);
    assembly->follow_needed = TRUE;
    g_hash_table_remove_all(assembly->registers);
    g_clear_pointer(&assembly->function, g_free);
    g_free(assembly->context_thread);
    g_free(assembly->context_frame);
    assembly->context_thread = g_strdup(session->selected_thread);
    assembly->context_frame = g_strdup(session->selected_frame);
    gtk_label_set_text(GTK_LABEL(assembly->status),
        insight_tr("Previous instruction snapshot. Pause and read again to resolve names in the new context."));
    native_name_refresh_views(session);
}

/* EN: Display the completed disassembly of session and refresh other address views, keeping final errors visible.
 * RU: Показывает готовый дизассемблер session и обновляет адреса в других представлениях, сохраняя видимость ошибок. */
static void native_assembly_finish(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    gchar *text = assembly->record ? native_assembly_render(session, assembly->record) : NULL;
    gchar *error = text || !assembly->record ? NULL : native_debug_field(assembly->record, "msg");
    debug_set_text(assembly->text, text ? text : "");
    native_instruction_refresh(session);
    gtk_label_set_text(GTK_LABEL(assembly->status), text ?
        insight_tr("Intel on x86/x64; native syntax on ARM/RISC-V. Names annotate original addresses. Up to 128 data locations, 64 symbols, 3 field levels. Refresh after stepping or changing frames.") :
        error ? error : insight_tr("No readable machine instructions at this address."));
    if (text && assembly->unresolved)
    {
        gchar *message = g_strdup_printf(insight_tr("Instruction snapshot ready. Unresolved name/type queries: %u. %s"),
            assembly->unresolved, assembly->last_error ? assembly->last_error : "");
        gtk_label_set_text(GTK_LABEL(assembly->status), message);
        g_free(message);
    }
    native_name_refresh_views(session);
    native_assembly_register_rows(session);
    g_free(error);
    g_free(text);
}
