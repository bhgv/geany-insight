/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Modeless setup dialogs, help and AI actions.
 * RU: Немодальные окна настройки, справка и действия ИИ.
 */

typedef struct
{
    GMainLoop *loop;
    gint response;
    gboolean finished;
} SetupDialogWait;

/* EN: Store response from dialog in the pending wait at data and stop its loop.
 * RU: Сохраняет response окна dialog в ожидании data и останавливает цикл. */
static void setup_dialog_response(GtkDialog *dialog, gint response, gpointer data)
{
    (void)dialog;
    SetupDialogWait *wait = data;
    wait->response = response;
    wait->finished = TRUE;
    g_main_loop_quit(wait->loop);
}

/* EN: End the wait at data when widget is destroyed.
 * RU: Завершает ожидание data при уничтожении widget. */
static void setup_dialog_destroy(GtkWidget *widget, gpointer data)
{
    setup_dialog_response(GTK_DIALOG(widget), GTK_RESPONSE_NONE, data);
}

/* EN: Wait for dialog without grabbing editor input. Suspend only a parent dialog.
 * RU: Ожидает ответ dialog без захвата ввода редактора. Блокирует только родительский диалог. */
gint insight_waveforms_run_setup_dialog(GtkDialog *dialog)
{
    GtkWidget *widget = GTK_WIDGET(dialog);
    GtkWindow *parent = gtk_window_get_transient_for(GTK_WINDOW(dialog));
    GtkWidget *parent_dialog = GTK_IS_DIALOG(parent) ? GTK_WIDGET(parent) : NULL;
    gboolean restore = parent_dialog && gtk_widget_get_sensitive(parent_dialog);
    if (restore)
    {
        g_object_ref(parent_dialog);
        gtk_widget_set_sensitive(parent_dialog, FALSE);
    }

    SetupDialogWait wait = {g_main_loop_new(NULL, FALSE), GTK_RESPONSE_NONE, FALSE};
    g_object_ref(widget);
    gulong response = g_signal_connect(widget, "response", G_CALLBACK(setup_dialog_response), &wait);
    gulong destroy = g_signal_connect(widget, "destroy", G_CALLBACK(setup_dialog_destroy), &wait);
    gtk_widget_show(widget);
    if (!wait.finished)
        g_main_loop_run(wait.loop);
    if (g_signal_handler_is_connected(widget, response))
        g_signal_handler_disconnect(widget, response);
    if (g_signal_handler_is_connected(widget, destroy))
        g_signal_handler_disconnect(widget, destroy);
    g_main_loop_unref(wait.loop);
    g_object_unref(widget);
    if (restore)
    {
        if (gtk_widget_get_visible(parent_dialog))
            gtk_widget_set_sensitive(parent_dialog, TRUE);
        g_object_unref(parent_dialog);
    }
    return wait.response;
}

/* EN: Show the current dialog's local help in a modeless child. button owns the
 * trigger and data is the parent dialog containing the guidance text.
 * RU: Показывает локальную справку в немодальном дочернем окне. button запускает
 * действие, data — родительский диалог с текстом справки. */
static void simulation_dialog_help(GtkButton *button, gpointer data)
{
    (void)button;
    GtkWidget *parent = data;
    const gchar *text = g_object_get_data(G_OBJECT(parent), "verilog-guidance");
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Verilog Setup Help", GTK_WINDOW(parent),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close", GTK_RESPONSE_CLOSE, NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 300);
    GtkWidget *view = gtk_text_view_new();
    gtk_widget_set_name(view, "insight-verilog-help-text");
    gtk_text_view_set_editable(GTK_TEXT_VIEW(view), FALSE);
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), text, -1);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), scroll, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
}

typedef struct
{
    GtkWidget *controller;
    GtkLabel *status;
    GtkTextBuffer *recipe;
    const gchar *help;
} SimulationAiContext;

/* EN: Find the nearest port recipe buffer in dialog's transient parent chain.
 * Return a borrowed buffer, or NULL if the dialog is unrelated to port setup.
 * RU: Ищет ближайший буфер правил в цепочке родительских окон dialog.
 * Возвращает заимствованный буфер или NULL вне настроек портов. */
static GtkTextBuffer *simulation_dialog_recipe(GtkWindow *dialog)
{
    for (GtkWindow *window = dialog; window; window = gtk_window_get_transient_for(window))
    {
        GtkTextBuffer *recipe = g_object_get_data(G_OBJECT(window), "verilog-port-recipe");
        if (recipe)
            return recipe;
    }
    return NULL;
}

/* EN: Replace assistant context with help and the current recipe from data.
 * button triggered the explicit action. Report whether the context was updated.
 * RU: Заменяет контекст помощника справкой и текущими правилами из data.
 * button запускает явное действие. Сообщает результат обновления. */
static void simulation_ai_include_rules(GtkButton *button, gpointer data)
{
    (void)button;
    SimulationAiContext *context = data;
    GtkTextIter first, last;
    gtk_text_buffer_get_bounds(context->recipe, &first, &last);
    gchar *recipe = gtk_text_buffer_get_text(context->recipe, &first, &last, FALSE);
    gchar *text = g_strconcat(context->help, "\n\nPort Signals rules:\n", recipe, NULL);
    gboolean updated = insight_ai_context_update(context->controller, text);
    gtk_label_set_text(
        context->status,
        updated
            ? insight_tr("Port rules added. Review the context before sending.")
            : insight_tr("Could not update context. Wait for the current request or shorten the rules."));
    g_free(text);
    g_free(recipe);
}

/* EN: Open the AI assistant for parent (data) from button. Seed it with local
 * help and offer explicit inclusion of port rules when a recipe is available.
 * RU: Открывает ИИ-помощника для parent из data по button. Добавляет справку
 * и предлагает явно включить правила портов, если буфер доступен. */
static void simulation_dialog_ai(GtkButton *button, gpointer data)
{
    (void)button;
    GtkWidget *parent = data;
    const gchar *text = g_object_get_data(G_OBJECT(parent), "verilog-guidance");
    GtkWidget *dialog = gtk_dialog_new_with_buttons("Verilog AI Assistant", GTK_WINDOW(parent),
                                                    GTK_DIALOG_DESTROY_WITH_PARENT,
                                                    "_Close", GTK_RESPONSE_CLOSE, NULL);
    g_object_ref_sink(dialog);
    gtk_window_set_default_size(GTK_WINDOW(dialog), 850, 650);
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), box, TRUE, TRUE, 0);
    SimulationAiContext context = {
        .controller =
            insight_ai_embed(box, gtk_window_get_title(GTK_WINDOW(parent)), text, parent, NULL, NULL),
        .recipe = simulation_dialog_recipe(GTK_WINDOW(parent)),
        .help = text};
    if (context.recipe)
    {
        GtkWidget *actions = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        GtkWidget *include = gtk_button_new_with_label(insight_tr("Use Help + Port Rules"));
        gtk_widget_set_name(include, "insight-verilog-ai-include-rules");
        gtk_widget_set_tooltip_text(
            include,
            insight_tr("Replace the current AI context with help and the unsaved Port Signals recipe."));
        context.status = GTK_LABEL(gtk_label_new(NULL));
        gtk_label_set_line_wrap(context.status, TRUE);
        gtk_box_pack_start(GTK_BOX(actions), include, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), GTK_WIDGET(context.status), TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))), actions, FALSE,
                           FALSE, 4);
        g_signal_connect(include, "clicked", G_CALLBACK(simulation_ai_include_rules), &context);
    }
    gtk_widget_show_all(dialog);
    insight_waveforms_run_setup_dialog(GTK_DIALOG(dialog));
    gtk_widget_destroy(dialog);
    g_object_unref(dialog);
}

/* EN: Set dialog's contextual guidance to english or russian using the current
 * interface language. The dialog owns the copied text.
 * RU: Записывает в dialog текст english или russian по текущему языку интерфейса.
 * Диалог хранит копию текста. */
static void simulation_dialog_guidance(GtkWidget *dialog, const gchar *english, const gchar *russian)
{
    const gchar *language = insight_i18n_language();
    gboolean use_russian = language && g_str_has_prefix(language, "ru");
    g_object_set_data_full(G_OBJECT(dialog), "verilog-guidance",
                           g_strdup(use_russian ? russian : english), g_free);
}

/* EN: Attach Help and AI buttons to dialog and initialize their shared context
 * from english and russian. Neither button changes the parent dialog response.
 * RU: Добавляет в dialog кнопки справки и ИИ с контекстом из english и russian.
 * Нажатие не подтверждает и не закрывает родительский диалог. */
static void simulation_dialog_tools(GtkWidget *dialog, const gchar *english, const gchar *russian)
{
    simulation_dialog_guidance(dialog, english, russian);
    GtkWidget *tools = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget *help = gtk_button_new_with_label(insight_tr("Help"));
    gtk_widget_set_name(help, "insight-verilog-dialog-help");
    gtk_button_set_image(GTK_BUTTON(help),
                         gtk_image_new_from_icon_name("help-browser-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(help), TRUE);
    g_signal_connect(help, "clicked", G_CALLBACK(simulation_dialog_help), dialog);
    gtk_box_pack_start(GTK_BOX(tools), help, FALSE, FALSE, 0);

    GtkWidget *ai = gtk_button_new_with_label(insight_tr("AI Assistant"));
    gtk_widget_set_name(ai, "insight-verilog-dialog-ai");
    gtk_button_set_image(GTK_BUTTON(ai), insight_command_icon("brain"));
    gtk_button_set_always_show_image(GTK_BUTTON(ai), TRUE);
    g_signal_connect(ai, "clicked", G_CALLBACK(simulation_dialog_ai), dialog);
    gtk_box_pack_start(GTK_BOX(tools), ai, FALSE, FALSE, 0);
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    g_object_set_data(G_OBJECT(area), "insight-verilog-dialog-tools", tools);
    gtk_box_pack_start(GTK_BOX(area), tools, FALSE, FALSE, 4);
}

/* EN: Open the Verilog walkthrough next to dialog. The button is unused.
 * RU: Открывает пошаговый урок рядом с dialog. Кнопка button не используется. */
static void simulation_tutorial_open(GtkButton *button, GtkWindow *dialog)
{
    (void)button;
    const gchar *language = insight_i18n_language();
    gchar *directory = g_build_filename(app->datadir, "insight", "docs", language, NULL);
    gchar *path = g_build_filename(directory, "verilog-tutorial.ini", NULL);
    GKeyFile *lesson = g_key_file_new();
    GError *error = NULL;
    if (!g_key_file_load_from_file(lesson, path, G_KEY_FILE_NONE, &error))
    {
        GtkWidget *notice = gtk_message_dialog_new(dialog, GTK_DIALOG_DESTROY_WITH_PARENT,
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", error->message);
        g_signal_connect_swapped(notice, "response", G_CALLBACK(gtk_widget_destroy), notice);
        gtk_widget_show(notice);
        g_clear_error(&error);
        g_key_file_unref(lesson);
        g_free(path);
        g_free(directory);
        return;
    }
    g_free(path);
    gchar *title = g_key_file_get_string(lesson, "Tutorial", "Title", NULL);
    gsize count;
    gchar **groups = g_key_file_get_groups(lesson, &count);
    GtkWidget *assistant = gtk_assistant_new();
    gtk_window_set_title(GTK_WINDOW(assistant), title ? title : insight_tr("Tutorial"));
    g_free(title);
    gtk_window_set_transient_for(GTK_WINDOW(assistant), dialog);
    gtk_window_set_destroy_with_parent(GTK_WINDOW(assistant), TRUE);
    gtk_window_set_default_size(GTK_WINDOW(assistant), 530, 260);
    for (gsize i = 0; i < count; i++)
    {
        if (!g_str_has_prefix(groups[i], "Step "))
            continue;
        gchar *heading = g_key_file_get_string(lesson, groups[i], "Title", NULL);
        gchar *text = g_key_file_get_string(lesson, groups[i], "Text", NULL);
        gchar *picture = g_key_file_get_string(lesson, groups[i], "Image", NULL);
        if (heading && text)
        {
            GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
            GtkWidget *label = gtk_label_new(text);
            gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
            gtk_label_set_xalign(GTK_LABEL(label), 0);
            gtk_box_pack_start(GTK_BOX(page), label, FALSE, FALSE, 0);
            if (picture && *picture && !strpbrk(picture, "/\\:") &&
                strcmp(picture, ".") && strcmp(picture, ".."))
            {
                gchar *image_path = g_build_filename(directory, picture, NULL);
                if (g_file_test(image_path, G_FILE_TEST_IS_REGULAR))
                    gtk_box_pack_start(GTK_BOX(page), gtk_image_new_from_file(image_path),
                                       FALSE, FALSE, 0);
                g_free(image_path);
            }
            gtk_widget_set_margin_start(page, 16);
            gtk_widget_set_margin_end(page, 16);
            gtk_widget_set_margin_top(page, 12);
            gtk_assistant_append_page(GTK_ASSISTANT(assistant), page);
            gtk_assistant_set_page_title(GTK_ASSISTANT(assistant), page, heading);
            gtk_assistant_set_page_complete(GTK_ASSISTANT(assistant), page, TRUE);
        }
        g_free(heading);
        g_free(text);
        g_free(picture);
    }
    g_strfreev(groups);
    g_key_file_unref(lesson);
    g_free(directory);
    gint pages = gtk_assistant_get_n_pages(GTK_ASSISTANT(assistant));
    if (!pages)
    {
        gtk_widget_destroy(assistant);
        return;
    }
    GtkWidget *last = gtk_assistant_get_nth_page(GTK_ASSISTANT(assistant), pages - 1);
    gtk_assistant_set_page_type(GTK_ASSISTANT(assistant), last, GTK_ASSISTANT_PAGE_SUMMARY);
    g_signal_connect_swapped(assistant, "cancel", G_CALLBACK(gtk_widget_destroy), assistant);
    g_signal_connect_swapped(assistant, "close", G_CALLBACK(gtk_widget_destroy), assistant);
    gtk_widget_show_all(assistant);
}

/* EN: Add a tutorial button to the help row already installed in dialog.
 * RU: Добавляет кнопку урока в существующую строку справки диалога dialog. */
static void simulation_dialog_tutorial(GtkWidget *dialog)
{
    GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *tools = g_object_get_data(G_OBJECT(area), "insight-verilog-dialog-tools");
    GtkWidget *button = gtk_button_new_with_label(insight_tr("Tutorial"));
    gtk_button_set_image(GTK_BUTTON(button),
                         gtk_image_new_from_icon_name("help-contents-symbolic", GTK_ICON_SIZE_BUTTON));
    gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
    g_signal_connect(button, "clicked", G_CALLBACK(simulation_tutorial_open), dialog);
    gtk_box_pack_start(GTK_BOX(tools), button, FALSE, FALSE, 0);
}
