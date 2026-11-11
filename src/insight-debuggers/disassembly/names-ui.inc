/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Edit global addresses, function-scoped frame names and real typed-data bindings.
 * RU: Редактирует глобальные адреса, имена кадра внутри функции и привязки настоящих типов данных. */

/* EN: Rebuild session's rules list from the canonical user rules, never persisting automatic runtime names.
 * RU: Перестраивает список session по ручным правилам, не сохраняя автоматические имена текущего процесса. */
static void native_name_rows(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    gtk_list_store_clear(assembly->rule_rows);
    const gchar *kinds[] = {"Address", "Frame offset", "Typed data"};
    for (guint i = 0; i < assembly->rules->len; i++)
    {
        NativeNameRule *rule = assembly->rules->pdata[i];
        gtk_list_store_insert_with_values(assembly->rule_rows, NULL, -1,
            0, insight_tr(kinds[rule->kind]), 1, rule->anchor, 2, rule->name,
            3, rule->type, 4, rule->scope, 5, i, -1);
    }
}

/* EN: Save current user name rules from session into profile; runtime addresses/derived fields are deliberately omitted.
 * RU: Записывает ручные имена session в profile; адреса процесса и вычисленные поля намеренно не сохраняются. */
static void native_name_profile_store(NativeDebug *session, GKeyFile *profile)
{
    NativeAssembly *assembly = session->assembly;
    g_key_file_set_integer(profile, "assembly-names", "count", assembly->rules->len);
    g_key_file_set_boolean(profile, "assembly-names", "follow", assembly->follow_enabled);
    for (guint i = 0; i < assembly->rules->len; i++)
    {
        NativeNameRule *rule = assembly->rules->pdata[i];
        gchar *group = g_strdup_printf("assembly-name-%u", i);
        g_key_file_set_integer(profile, group, "kind", rule->kind);
        g_key_file_set_string(profile, group, "anchor", rule->anchor);
        g_key_file_set_string(profile, group, "name", rule->name);
        g_key_file_set_string(profile, group, "type", rule->type);
        g_key_file_set_string(profile, group, "scope", rule->scope);
        g_free(group);
    }
}

/* EN: Replace session's user rules from an already validated profile; old profiles without names preserve current rules.
 * RU: Заменяет ручные правила session из проверенного profile; старые профили без имён сохраняют текущие правила. */
static void native_name_profile_load(NativeDebug *session, GKeyFile *profile)
{
    if (!g_key_file_has_group(profile, "assembly-names"))
        return;
    NativeAssembly *assembly = session->assembly;
    g_ptr_array_set_size(assembly->rules, 0);
    g_ptr_array_set_size(assembly->ranges, 0);
    gint count = g_key_file_get_integer(profile, "assembly-names", "count", NULL);
    for (gint i = 0; i < count; i++)
        g_ptr_array_add(assembly->rules, native_name_rule_read(profile, (guint)i));
    native_name_rows(session);
    if (g_key_file_has_key(profile, "assembly-names", "follow", NULL))
    {
        assembly->follow_enabled = g_key_file_get_boolean(profile, "assembly-names", "follow", NULL);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(assembly->follow), assembly->follow_enabled);
    }
}

/* EN: Return the selected rule index in session, or -1; list indices remain stable until the next user edit.
 * RU: Возвращает выбранный индекс правила session либо -1; индексы стабильны до следующего редактирования. */
static gint native_name_selected(NativeDebug *session)
{
    GtkTreeModel *model = NULL;
    GtkTreeIter row;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->assembly->rules_view));
    guint index = 0;
    if (!gtk_tree_selection_get_selected(selection, &model, &row))
        return -1;
    gtk_tree_model_get(model, &row, 5, &index, -1);
    return index < session->assembly->rules->len ? (gint)index : -1;
}

/* EN: Re-resolve every view after a manual rule change in session, only when the same target is paused and idle.
 * RU: Повторно разрешает все представления после изменения правила session только на паузе и при свободном GDB. */
static void native_name_reread(NativeDebug *session)
{
    native_name_refresh_views(session);
    if (!session->closed && session->ready && session->stopped && !session->pending && !session->writing)
    {
        gchar *request = native_assembly_command(gtk_entry_get_text(GTK_ENTRY(session->assembly->expression)));
        if (request)
        {
            native_assembly_prepare(session, request, gtk_entry_get_text(GTK_ENTRY(session->assembly->expression)));
            native_debug_resume(session->window, session);
        }
    }
}

/* EN: Edit button's rule kind in session data; no target writes occur, and only validated names replace the old rule.
 * RU: Редактирует вид правила button в session data; программа не изменяется, старое правило заменяется только проверенным. */
static void native_name_edit(GtkButton *button, gpointer data)
{
    NativeDebug *session = data;
    NativeAssembly *assembly = session->assembly;
    if (session->pending || session->writing || assembly->active)
        return;
    gint action = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button), "name-action"));
    gint selected = action < 0 ? native_name_selected(session) : -1;
    NativeNameRule *old = selected >= 0 ? assembly->rules->pdata[selected] : NULL;
    if (action == -2)
    {
        if (old)
        {
            g_ptr_array_remove_index(assembly->rules, selected);
            g_ptr_array_set_size(assembly->ranges, 0);
            native_name_rows(session);
            native_name_reread(session);
        }
        return;
    }
    if (action < 0 && !old)
        return;
    guint kind = old ? old->kind : (guint)action;
    if (!old && assembly->rules->len >= 256)
        return;
    GtkWidget *owner = g_object_ref(session->window);
    GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("Name or type binding"), GTK_WINDOW(owner),
        GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel", GTK_RESPONSE_CANCEL,
        "_Apply", GTK_RESPONSE_ACCEPT, NULL);
    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), grid, TRUE, TRUE, 0);
    const gchar *labels[] = {kind == 1 ? "Signed frame offset (e.g. -0x20)" : "Address expression (e.g. &object)",
        "Display name", kind == 1 ? "Base register (fp, sp, rbp, x29, r11, r7, s0)" : "C/C++ type (for typed data)",
        "Function scope (required for frame names)"};
    const gchar *defaults[] = {old ? old->anchor : kind == 1 ? "-0x10" : gtk_entry_get_text(GTK_ENTRY(assembly->expression)),
        old ? old->name : "", old ? old->type : kind == 1 ? "fp" : "",
        old ? old->scope : kind == 1 && assembly->function ? assembly->function : ""};
    GtkWidget *entries[4];
    const gchar *entry_names[] = {"insight-gdb-name-anchor", "insight-gdb-name-label",
        "insight-gdb-name-type", "insight-gdb-name-scope"};
    const gint limits[] = {512, 128, 256, 512};
    for (guint i = 0; i < 4; i++)
    {
        GtkWidget *label = gtk_label_new(insight_tr(labels[i]));
        gtk_widget_set_halign(label, GTK_ALIGN_START);
        entries[i] = gtk_entry_new();
        gtk_widget_set_name(entries[i], entry_names[i]);
        gtk_entry_set_max_length(GTK_ENTRY(entries[i]), limits[i]);
        gtk_entry_set_text(GTK_ENTRY(entries[i]), defaults[i]);
        gtk_widget_set_size_request(entries[i], 340, -1);
        gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), entries[i], 1, i, 1, 1);
    }
    gtk_widget_set_sensitive(entries[2], kind != 0);
    gtk_widget_set_sensitive(entries[3], kind != 0);
    GtkTextIter start, end;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(assembly->text));
    if (!old && kind != 1 && gtk_text_buffer_get_selection_bounds(buffer, &start, &end))
    {
        gchar *chosen = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
        guint64 address = 0;
        if (native_assembly_number(chosen, &address))
        {
            gchar *numeric = g_strdup_printf("0x%" G_GINT64_MODIFIER "x", address);
            gtk_entry_set_text(GTK_ENTRY(entries[0]), numeric);
            g_free(numeric);
        }
        g_free(chosen);
    }
    GtkWidget *hint = gtk_label_new(insight_tr("Names refresh matching addresses automatically. Frame names belong to one function. Typed fields come from GDB. Save debugger profile to keep rules."));
    gtk_label_set_line_wrap(GTK_LABEL(hint), TRUE);
    gtk_widget_set_size_request(hint, 540, -1);
    gtk_grid_attach(GTK_GRID(grid), hint, 0, 4, 2, 1);
    gboolean changed = FALSE;
    gtk_widget_show_all(dialog);
    while (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT && !session->closed)
    {
        NativeNameRule *rule = g_new0(NativeNameRule, 1);
        rule->kind = kind;
        rule->anchor = g_strdup(gtk_entry_get_text(GTK_ENTRY(entries[0])));
        rule->name = g_strdup(gtk_entry_get_text(GTK_ENTRY(entries[1])));
        rule->type = g_strdup(gtk_entry_get_text(GTK_ENTRY(entries[2])));
        rule->scope = g_strdup(kind != 0 ? gtk_entry_get_text(GTK_ENTRY(entries[3])) : "");
        if (native_name_rule_valid(rule))
        {
            if (old)
                g_ptr_array_remove_index(assembly->rules, selected);
            native_name_rule_replace(assembly, rule);
            g_ptr_array_set_size(assembly->ranges, 0);
            native_name_rows(session);
            changed = TRUE;
            break;
        }
        native_name_rule_free(rule);
        gtk_label_set_text(GTK_LABEL(hint), insight_tr("Enter a name and valid address/offset; frame names need a function and typed data needs a type."));
    }
    gtk_widget_destroy(dialog);
    if (changed && !session->closed)
        native_name_reread(session);
    g_object_unref(owner);
}

/* EN: Save rules through the existing validated debugger profile for session data; button is unused.
 * RU: Сохраняет правила через проверяемый профиль отладчика session data; button не используется. */
static void native_name_save(GtkButton *button, gpointer data)
{
    (void)button;
    NativeDebug *session = data;
    if (!session->pending && !session->writing && !session->assembly->active)
    {
        native_profile_save(session);
        gtk_label_set_text(GTK_LABEL(session->assembly->status),
            gtk_label_get_text(GTK_LABEL(session->remote->status)));
    }
}

/* EN: Create the editable rule list for session; automatic names stay separate from user-authored rules.
 * RU: Создаёт редактируемый список правил session; автоматические имена отделены от ручных правил. */
static GtkWidget *native_name_widget(NativeDebug *session)
{
    NativeAssembly *assembly = session->assembly;
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    GtkWidget *bar = gtk_flow_box_new();
    gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(bar), GTK_SELECTION_NONE);
    const gchar *labels[] = {"Address name…", "Frame name…", "Typed data…", "Edit name…", "Delete name"};
    const gint actions[] = {0, 1, 2, -1, -2};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *button = gtk_button_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(button), "name-action", GINT_TO_POINTER(actions[i]));
        g_signal_connect(button, "clicked", G_CALLBACK(native_name_edit), session);
        g_ptr_array_add(session->paused_controls, button);
        gtk_flow_box_insert(GTK_FLOW_BOX(bar), button, -1);
    }
    GtkWidget *save = gtk_button_new_with_label(insight_tr("Save names in debugger profile"));
    g_signal_connect(save, "clicked", G_CALLBACK(native_name_save), session);
    g_ptr_array_add(session->paused_controls, save);
    gtk_flow_box_insert(GTK_FLOW_BOX(bar), save, -1);
    gtk_box_pack_start(GTK_BOX(box), bar, FALSE, FALSE, 0);
    assembly->rule_rows = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING,
        G_TYPE_STRING, G_TYPE_STRING, G_TYPE_UINT);
    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(assembly->rule_rows));
    assembly->rules_view = view;
    gtk_widget_set_name(view, "insight-gdb-address-names");
    g_object_unref(assembly->rule_rows);
    const gchar *columns[] = {"Kind", "Address / offset", "Name", "Type / register", "Function scope"};
    for (guint i = 0; i < G_N_ELEMENTS(columns); i++)
        gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1,
            insight_tr(columns[i]), gtk_cell_renderer_text_new(), "text", i, NULL);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, -1, 100);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(box), scroll, TRUE, TRUE, 0);
    return box;
}
