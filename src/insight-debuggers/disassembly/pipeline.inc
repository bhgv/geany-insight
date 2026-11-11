/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Serialize symbol/field discovery with disassembly and dispose temporary GDB variable objects.
 * RU: Последовательно собирает символы/поля и дизассемблер, освобождает временные объекты переменных GDB. */

/* EN: Free assembly and all pending work after the debugger window closes; widgets belong to GTK.
 * RU: Освобождает assembly и ожидающие задания при закрытии отладчика; элементы интерфейса принадлежат GTK. */
static void native_assembly_free(NativeAssembly *assembly)
{
    if (!assembly)
        return;
    insight_queue_clear_full(&assembly->work, native_assembly_work_free);
    if (assembly->pending)
        native_assembly_work_free(assembly->pending);
    g_ptr_array_unref(assembly->rules);
    g_ptr_array_unref(assembly->ranges);
    g_ptr_array_unref(assembly->objects);
    if (assembly->register_names)
        g_ptr_array_unref(assembly->register_names);
    g_hash_table_unref(assembly->registers);
    if (assembly->instruction_points)
        g_hash_table_unref(assembly->instruction_points);
    if (assembly->instruction_marks)
        g_hash_table_unref(assembly->instruction_marks);
    if (assembly->previous_registers)
        g_hash_table_unref(assembly->previous_registers);
    g_free(assembly->register_thread);
    g_free(assembly->register_frame);
    g_free(assembly->record);
    g_free(assembly->function);
    g_free(assembly->request);
    g_free(assembly->last_error);
    g_free(assembly->request_expression);
    g_free(assembly->context_thread);
    g_free(assembly->context_frame);
    g_free(assembly);
}

/* EN: Queue discovery for session before owned request for expression; manual expressions are reevaluated after every restart/read.
 * RU: Ставит сбор метаданных session перед принадлежащим request для expression; ручные выражения вычисляются заново после запуска/чтения. */
static void native_assembly_prepare(NativeDebug *session, gchar *request, const gchar *expression)
{
    NativeAssembly *assembly = session->assembly;
    assembly->active = TRUE;
    assembly->follow_needed = FALSE;
    g_free(assembly->context_thread);
    g_free(assembly->context_frame);
    assembly->context_thread = g_strdup(session->selected_thread);
    assembly->context_frame = g_strdup(session->selected_frame);
    assembly->request = request;
    assembly->span = 128;
    g_free(assembly->request_expression);
    assembly->request_expression = g_strdup(expression);
    assembly->temporary_valid = FALSE;
    assembly->collected = assembly->unresolved = 0;
    g_clear_pointer(&assembly->last_error, g_free);
    g_ptr_array_set_size(assembly->ranges, 0);
    g_hash_table_remove_all(assembly->registers);
    native_assembly_queue(assembly, g_strdup("-gdb-set disassembly-flavor intel"), 0, NULL, NULL, NULL, NULL, 0, FALSE);
    native_assembly_queue(assembly, g_strdup("-stack-info-frame"), 1, NULL, NULL, NULL, NULL, 0, FALSE);
    if (!assembly->register_names)
        assembly->register_names = g_ptr_array_new_with_free_func(g_free);
    g_ptr_array_set_size(assembly->register_names, 0);
    native_assembly_queue(assembly, g_strdup("-data-list-register-names"), 13, NULL, NULL, NULL, NULL, 0, FALSE);
    const gchar *registers[] = {"fp", "sp"};
    for (guint i = 0; i < G_N_ELEMENTS(registers); i++)
    {
        gchar *expression = g_strdup_printf("(unsigned long long)$%s", registers[i]);
        native_assembly_queue(assembly, native_assembly_expression("-data-evaluate-expression", expression),
            2, registers[i], NULL, NULL, NULL, 0, FALSE);
        g_free(expression);
    }
    native_assembly_queue(assembly, g_strdup("-stack-list-variables --no-values"), 3, NULL, NULL, NULL, NULL, 0, FALSE);
}

/* EN: Send the next owned work item for session; TRUE keeps the normal request scheduler behind this bounded pipeline.
 * RU: Отправляет очередное задание session; TRUE удерживает обычную очередь до завершения ограниченного конвейера. */
static gboolean native_assembly_next(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly)
        return FALSE;
    if (!assembly->active)
        native_assembly_follow(session);
    if (!assembly->active)
        return FALSE;
    if (!session->stopped)
    {
        insight_queue_clear_full(&assembly->work, native_assembly_work_free);
        g_clear_pointer(&assembly->request, g_free);
        assembly->active = FALSE;
        return FALSE;
    }
    if (g_queue_is_empty(&assembly->work) && assembly->objects->len)
    {
        gchar *object = g_strdup(assembly->objects->pdata[assembly->objects->len - 1]);
        g_ptr_array_remove_index(assembly->objects, assembly->objects->len - 1);
        native_assembly_queue(assembly, native_assembly_expression("-var-delete", object),
            9, NULL, NULL, NULL, NULL, 0, FALSE);
        g_free(object);
    }
    if (g_queue_is_empty(&assembly->work) && assembly->request)
        native_assembly_queue(assembly, g_steal_pointer(&assembly->request), 10, NULL, NULL, NULL, NULL, 0, FALSE);
    assembly->pending = g_queue_pop_head(&assembly->work);
    if (!assembly->pending)
    {
        assembly->active = FALSE;
        native_assembly_finish(session);
        native_debug_update(session);
        return FALSE;
    }
    native_debug_send(session->window, assembly->pending->command);
    return TRUE;
}

/* EN: Consume session's pending assembly record, hiding optional metadata errors but displaying the final disassembly error.
 * RU: Обрабатывает ожидающий record session, скрывая ошибки необязательных метаданных и показывая ошибку дизассемблера. */
static gboolean native_assembly_reply(NativeDebug *session, const gchar *record)
{
    NativeAssembly *assembly = session->assembly;
    if (!assembly || !assembly->pending)
        return FALSE;
    NativeAssemblyWork *work = g_steal_pointer(&assembly->pending);
    if (work->action == 10)
    {
        g_free(assembly->record);
        assembly->record = g_strdup(record);
        if (g_str_has_prefix(record, "^error") && assembly->span > 1)
        {
            assembly->span /= 2;
            gchar *retry = native_assembly_window(assembly->request_expression, assembly->span);
            native_assembly_queue(assembly, retry, 10, NULL, NULL, NULL, NULL, 0, FALSE);
        }
        else
            native_assembly_symbols(assembly, record);
    }
    else if (work->action == 12)
        native_assembly_symbol_reply(assembly, work, record);
    else
        native_assembly_metadata(assembly, work, record);
    native_assembly_work_free(work);
    return TRUE;
}
