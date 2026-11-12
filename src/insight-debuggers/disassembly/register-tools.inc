/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Filter/sort the register snapshot and copy selected rows or inspect a register as code, preserving model ownership.
 * RU: Фильтрует/сортирует снимок регистров, копирует строки и открывает регистр как код с корректным владением моделями. */

/* EN: Decide visibility of row in model for session data using current search and changed-only controls.
 * RU: Определяет видимость row модели model для session data по поиску и показу только изменений. */
static gboolean native_register_visible(GtkTreeModel *model, GtkTreeIter *row, gpointer data)
{
    NativeDebug *session = data;
    if (session->closed)
        return TRUE;
    NativeAssembly *assembly = session->assembly;
    gchar *name = NULL, *hex = NULL, *label = NULL, *changed = NULL;
    gtk_tree_model_get(model, row, 0, &name, 1, &hex, 3, &label, 4, &changed, -1);
    gboolean result = native_register_matches(name, hex, label, changed && *changed,
        gtk_entry_get_text(GTK_ENTRY(assembly->register_search)),
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(assembly->register_changed)));
    g_free(changed);
    g_free(label);
    g_free(hex);
    g_free(name);
    return result;
}

/* EN: Refilter session data after control changes; control is unused and no target query occurs.
 * RU: Обновляет фильтр session data после изменения параметров; control не используется, запросов к цели нет. */
static void native_register_filter_changed(GtkWidget *control, gpointer data)
{
    (void)control;
    NativeDebug *session = data;
    if (!session->closed && session->assembly->register_filter)
        gtk_tree_model_filter_refilter(GTK_TREE_MODEL_FILTER(session->assembly->register_filter));
}

/* EN: Copy button's selected register field/row in session data, or open its current register expression as code for action 4.
 * RU: Копирует выбранное поле/строку регистра по button в session data либо открывает текущее выражение регистра как код для действия 4. */
static void native_register_tool(GtkWidget *button, gpointer data)
{
    NativeDebug *session = data;
    if (session->closed)
        return;
    GtkTreeModel *model = NULL;
    GtkTreeIter row;
    GtkTreeSelection *selection = gtk_tree_view_get_selection(GTK_TREE_VIEW(session->assembly->register_view));
    if (!gtk_tree_selection_get_selected(selection, &model, &row))
        return;
    guint action = GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button), "register-tool"));
    gchar *name = NULL, *hex = NULL, *decimal = NULL, *label = NULL;
    gtk_tree_model_get(model, &row, 0, &name, 1, &hex, 2, &decimal, 3, &label, -1);
    gchar *text = NULL;
    if (native_name_register(name))
    {
        if (action == 4)
        {
            if (native_instruction_idle(session))
            {
                gchar *expression = g_strconcat("$", name, NULL);
                gtk_entry_set_text(GTK_ENTRY(session->assembly->expression), expression);
                native_assembly_prepare(session, native_assembly_command(expression), expression);
                native_debug_resume(session->window, session);
                g_free(expression);
            }
        }
        else
        {
            text = action == 0 ? g_strconcat("$", name, NULL) : action == 1 ? g_strdup(hex) :
                action == 2 ? g_strdup(decimal) : g_strdup_printf("%s = %s / %s%s%s", name,
                    hex ? hex : "", decimal ? decimal : "", label && *label ? " — " : "", label ? label : "");
            if (text)
                gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, -1);
        }
    }
    g_free(text);
    g_free(label);
    g_free(decimal);
    g_free(hex);
    g_free(name);
}

/* EN: Add search controls to box for session before model construction; returned fields belong to GTK.
 * RU: Добавляет поиск в box для session до создания модели; поля принадлежат GTK. */
static void native_register_search_widget(NativeDebug *session, GtkWidget *box)
{
    NativeAssembly *assembly = session->assembly;
    assembly->register_search = gtk_search_entry_new();
    gtk_entry_set_max_length(GTK_ENTRY(assembly->register_search), 256);
    gtk_widget_set_name(assembly->register_search, "insight-gdb-register-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(assembly->register_search), insight_tr("Find register, address or resolved name"));
    g_signal_connect(assembly->register_search, "changed", G_CALLBACK(native_register_filter_changed), session);
    gtk_box_pack_start(GTK_BOX(box), assembly->register_search, FALSE, FALSE, 0);
    assembly->register_changed = gtk_check_button_new_with_label(insight_tr("Only changed registers"));
    gtk_widget_set_name(assembly->register_changed, "insight-gdb-register-changed");
    g_signal_connect(assembly->register_changed, "toggled", G_CALLBACK(native_register_filter_changed), session);
    gtk_box_pack_start(GTK_BOX(box), assembly->register_changed, FALSE, FALSE, 0);
}

/* EN: Append session's register copy/code actions to bar; only code inspection requires an idle paused target.
 * RU: Добавляет копирование и просмотр кода регистров session в bar; только просмотр кода требует свободной паузы цели. */
static void native_register_tools_widget(NativeDebug *session, GtkWidget *bar)
{
    const gchar *labels[] = {"Copy register expression", "Copy register hex value", "Copy register decimal value", "Copy register row"};
    GtkWidget *menu = gtk_menu_new();
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *item = gtk_menu_item_new_with_label(insight_tr(labels[i]));
        g_object_set_data(G_OBJECT(item), "register-tool", GUINT_TO_POINTER(i));
        g_signal_connect(item, "activate", G_CALLBACK(native_register_tool), session);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
    }
    gtk_widget_show_all(menu);
    GtkWidget *copy = gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(copy), insight_tr("Copy register…"));
    gtk_widget_set_name(copy, "insight-gdb-register-copy");
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(copy), menu);
    gtk_flow_box_insert(GTK_FLOW_BOX(bar), copy, -1);
    GtkWidget *code = gtk_button_new_with_label(insight_tr("View register address as code"));
    g_object_set_data(G_OBJECT(code), "register-tool", GUINT_TO_POINTER(4));
    g_signal_connect(code, "clicked", G_CALLBACK(native_register_tool), session);
    g_ptr_array_add(session->paused_controls, code);
    gtk_flow_box_insert(GTK_FLOW_BOX(bar), code, -1);
}
