/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Device port fields, bit widths and slice selection.
 * RU: Поля портов устройств, битность и выбор срезов.
 */

/* EN: Resolve the required width from spec, using the indexed entry in form for @.
 * RU: Определяет требуемую ширину по spec, используя поле form по индексу после @. */
static guint device_width(const DeviceForm *form, const gchar *spec)
{
    if (!spec || !*spec)
        return 0;
    const gchar *value = spec + 1;
    if (*value == '@')
    {
        guint index = (guint)g_ascii_strtoull(value + 1, NULL, 10);
        if (index >= form->entries->len)
            return 0;
        value = gtk_entry_get_text(GTK_ENTRY(form->entries->pdata[index]));
    }
    guint64 width;
    return g_ascii_string_to_unsigned(value, 10, 1, 4096, &width, NULL) ? (guint)width : 0;
}

/* EN: Update form's active direction and width, then refresh the port filters.
 * RU: Обновляет направление и ширину выбранного поля form и применяет фильтры. */
static void device_port_filter(DeviceForm *form)
{
    PortBrowser *browser = form->browser;
    const gchar *spec = form->active ? g_object_get_data(G_OBJECT(form->active), "port-spec") : NULL;
    gchar direction = spec ? *spec : 0;
    guint width = device_width(form, spec);
    if (browser->direction == direction && browser->required_width == width)
        return;
    browser->direction = direction;
    browser->required_width = width;
    gtk_widget_set_sensitive(browser->compatible, direction != 0);
    port_browser_refilter(NULL, browser);
}

/* EN: Refresh width captions after editable changes; data points to the device form.
 * RU: Обновляет подписи ширины при изменении editable. data указывает на форму. */
static void device_width_changed(GtkEditable *editable, gpointer data)
{
    (void)editable;
    DeviceForm *form = data;
    for (guint i = 0; i < form->entries->len; i++)
    {
        GtkWidget *entry = form->entries->pdata[i];
        const gchar *spec = g_object_get_data(G_OBJECT(entry), "port-spec");
        GtkLabel *label = g_object_get_data(G_OBJECT(entry), "port-label");
        const gchar *title = g_object_get_data(G_OBJECT(entry), "port-title");
        if (!spec || !label)
            continue;
        guint width = device_width(form, spec);
        gchar *caption = *spec == 'a' ? g_strdup_printf("%s (1–64 bits)", title)
                         : width
                             ? g_strdup_printf("%s (%u %s)", title, width, width == 1 ? "bit" : "bits")
                             : g_strdup_printf("%s (width unspecified)", title);
        gtk_label_set_text(label, caption);
        g_free(caption);
    }
    device_port_filter(form);
}

/* EN: Remember the focused port entry and highlight matching directions. event is unused.
 * data is the form. Return FALSE to preserve normal GTK focus behavior.
 * RU: Запоминает выбранное поле entry и подсвечивает подходящие направления.
 * event не используется, data — форма. Возвращает FALSE для обычного поведения GTK. */
static gboolean device_port_focus(GtkWidget *entry, GdkEventFocus *event, gpointer data)
{
    (void)event;
    DeviceForm *form = data;
    const gchar *spec = g_object_get_data(G_OBJECT(entry), "port-spec");
    form->active = spec ? entry : NULL;
    device_port_filter(form);
    return FALSE;
}

typedef struct
{
    gchar *name;
    guint available, width;
    gint left, right;
} DevicePortChoice;

/* EN: Read row from model into choice for the active field of form. Return FALSE
 * and show a diagnostic for an incompatible port. The caller frees choice->name.
 * RU: Читает row из model в choice для активного поля form. При несовместимости
 * показывает причину и возвращает FALSE. Вызывающий освобождает choice->name. */
static gboolean device_port_choice(DeviceForm *form, GtkTreeModel *model, GtkTreeIter *row,
                                   DevicePortChoice *choice)
{
    if (!form->active)
    {
        gtk_label_set_text(GTK_LABEL(form->status), "Select a port field first.");
        return FALSE;
    }
    gchar *direction;
    gtk_tree_model_get(model, row, PORT_NAME, &choice->name, PORT_DIRECTION, &direction, PORT_WIDTH,
                       &choice->available, PORT_LEFT, &choice->left, PORT_RIGHT, &choice->right, -1);
    const gchar *spec = g_object_get_data(G_OBJECT(form->active), "port-spec");
    choice->width = device_width(form, spec);
    if (*spec == 'a' && choice->available)
        choice->width = MIN(choice->width, choice->available);
    const gchar *error = NULL;
    if (!port_browser_matches(direction, *spec))
        error = "The port direction does not match this field.";
    else if (!choice->available || !choice->width || choice->available < choice->width)
        error = "The port width is unknown or too small for this field.";
    gtk_label_set_text(GTK_LABEL(form->status), error ? error : "");
    g_free(direction);
    return error == NULL;
}

/* EN: Format an owned Verilog expression from choice and offset from its least
 * significant end. The caller validates offset against the available width.
 * RU: Формирует новое выражение Verilog из choice и смещения offset от младшего
 * края. Вызывающий проверяет offset по доступной ширине. */
static gchar *device_port_slice(const DevicePortChoice *choice, guint offset)
{
    if (choice->available == choice->width)
        return g_strdup(choice->name);
    gint step = choice->left >= choice->right ? 1 : -1;
    gint last = choice->right + step * (gint)offset;
    gint first = last + step * ((gint)choice->width - 1);
    if (choice->width == 1)
        return g_strdup_printf("%s[%d]", choice->name, last);
    return g_strdup_printf("%s[%d:%d]", choice->name, first, last);
}

/* EN: Replace the focused field of form with choice at offset, preserving HDL order.
 * RU: Заменяет активное поле form выражением choice со смещением offset в порядке HDL. */
static void device_port_apply(DeviceForm *form, const DevicePortChoice *choice, guint offset)
{
    gchar *value = device_port_slice(choice, offset);
    gtk_entry_set_text(GTK_ENTRY(form->active), value);
    g_free(value);
}

/* EN: Use the low-order slice from path in tree for the form in data. column is unused.
 * RU: Назначает младший срез порта из tree по path в форму из data. column не используется. */
static void device_port_activate(GtkTreeView *tree, GtkTreePath *path, GtkTreeViewColumn *column,
                                 gpointer data)
{
    (void)column;
    DeviceForm *form = data;
    GtkTreeModel *model = gtk_tree_view_get_model(tree);
    GtkTreeIter row;
    DevicePortChoice choice = {0};
    if (gtk_tree_model_get_iter(model, &row, path) && device_port_choice(form, model, &row, &choice))
        device_port_apply(form, &choice, 0);
    g_free(choice.name);
}

typedef struct
{
    const DevicePortChoice *choice;
    GtkLabel *label;
} DeviceSlicePreview;

/* EN: Refresh the preview in data after the offset spin button changes.
 * RU: Обновляет предварительный просмотр из data после изменения смещения spin. */
static void device_slice_preview(GtkSpinButton *spin, gpointer data)
{
    DeviceSlicePreview *preview = data;
    gchar *value = device_port_slice(preview->choice, gtk_spin_button_get_value_as_int(spin));
    gtk_label_set_text(preview->label, value);
    g_free(value);
}

/* EN: Open a modal offset picker from button for the selected port and active
 * field in form (data). Apply the expression only on confirmation.
 * RU: Открывает по button модальный выбор смещения для порта и поля формы из
 * data. Применяет выражение только после подтверждения. */
static void device_port_choose_slice(GtkButton *button, gpointer data)
{
    DeviceForm *form = data;
    GtkTreeView *tree = GTK_TREE_VIEW(form->browser->tree);
    GtkTreeModel *model;
    GtkTreeIter row;
    if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(tree), &model, &row))
    {
        gtk_label_set_text(GTK_LABEL(form->status), "Select a port in the list.");
        return;
    }
    DevicePortChoice choice = {0};
    if (device_port_choice(form, model, &row, &choice))
    {
        if (choice.available == choice.width)
            device_port_apply(form, &choice, 0);
        else
        {
            GtkWindow *parent = GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(button)));
            GtkWidget *dialog = gtk_dialog_new_with_buttons(
                "Choose Port Slice", parent, GTK_DIALOG_DESTROY_WITH_PARENT, "_Cancel",
                GTK_RESPONSE_CANCEL, "_Assign", GTK_RESPONSE_ACCEPT, NULL);
            simulation_dialog_tools(
                dialog,
                "Set the offset from the least significant end of the selected port. The preview uses "
                "the port’s declared index order. Assign replaces the current field with the shown "
                "expression.",
                "Укажите смещение от младшего края порта. Предпросмотр учитывает порядок индексов в "
                "объявлении. Assign заменяет текущее поле показанным выражением.");
            GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
            gtk_container_set_border_width(GTK_CONTAINER(box), 12);
            GtkWidget *caption = gtk_label_new("Offset from the least significant bit");
            gtk_box_pack_start(GTK_BOX(box), caption, FALSE, FALSE, 4);
            GtkWidget *spin = gtk_spin_button_new_with_range(0, choice.available - choice.width, 1);
            gtk_box_pack_start(GTK_BOX(box), spin, FALSE, FALSE, 4);
            GtkWidget *label = gtk_label_new(NULL);
            gtk_label_set_selectable(GTK_LABEL(label), TRUE);
            gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 4);
            DeviceSlicePreview preview = {&choice, GTK_LABEL(label)};
            g_signal_connect(spin, "value-changed", G_CALLBACK(device_slice_preview), &preview);
            device_slice_preview(GTK_SPIN_BUTTON(spin), &preview);
            gtk_widget_show_all(dialog);
            if (insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT)
                device_port_apply(form, &choice, gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
            gtk_widget_destroy(dialog);
        }
    }
    g_free(choice.name);
}

/* EN: Resolve value from entry using the port information in form. Append a slice alias with id to prefix when needed and return owned text or NULL.
 * RU: Разрешает value из entry по данным портов form. При необходимости добавляет срез с id в prefix и возвращает новую строку или NULL. */
static gchar *device_port_argument(const DeviceForm *form, GtkWidget *entry, const gchar *value, guint id,
                                   GString *prefix)
{
    const gchar *spec = g_object_get_data(G_OBJECT(entry), "port-spec");
    const gchar *bracket = strchr(value, '[');
    if (!spec || *spec == 'a' || !bracket || entry == form->reset)
        return g_strdup(value);
    gchar *name = g_strndup(value, bracket - value);
    PortInfo *port = g_hash_table_lookup(form->browser->ports, name);
    GRegex *range = g_regex_new("^\\[(-?[0-9]+)(?::(-?[0-9]+))?\\]$", 0, 0, NULL);
    GMatchInfo *match = NULL;
    gchar *alias = NULL;
    gboolean matched = g_regex_match(range, bracket, 0, &match);
    if (port && port->width && port_browser_matches(port->direction, *spec) && matched)
    {
        gchar *left = g_match_info_fetch(match, 1);
        gchar *right = g_match_info_fetch(match, 2);
        gint64 first, last;
        if (g_ascii_string_to_signed(left, 10, -1000000, 1000000, &first, NULL) &&
            g_ascii_string_to_signed(*right ? right : left, 10, -1000000, 1000000, &last, NULL) &&
            ABS(first - last) + 1 == device_width(form, spec))
        {
            guint offset = (guint)gtk_text_buffer_get_char_count(form->browser->recipe);
            alias = g_strdup_printf("geany_slice_%u_%u", offset, id);
            gchar direction = *spec;
            if (direction == 'c' || direction == 'd')
                direction = !strcmp(port->direction, "inout") ? 'b' : direction == 'c' ? 'o' : 'i';
            g_string_append_printf(
                prefix, "port-slice %s %s %c %d %d %" G_GINT64_FORMAT " %" G_GINT64_FORMAT "\n", alias,
                name, direction, port->left, port->right, first, last);
        }
        g_free(left);
        g_free(right);
    }
    g_match_info_free(match);
    g_regex_unref(range);
    g_free(name);
    return alias;
}
