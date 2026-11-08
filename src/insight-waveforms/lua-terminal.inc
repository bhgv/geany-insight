/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Display and send interactive commands to each Lua device.
 * RU: Показывает вывод каждого Lua-устройства и отправляет ему команды. */

typedef struct
{
    WaveSimulation *run;
    gchar *stem;
    GtkWidget *view;
    GtkEntry *entry;
    long offset;
} LuaTerminal;

/* EN: Release the terminal descriptor data when its page is finalized.
 * RU: Освобождает описание терминала data при уничтожении страницы. */
static void lua_terminal_free(gpointer data)
{
    LuaTerminal *terminal = data;
    g_free(terminal->stem);
    g_free(terminal);
}

/* EN: Append text to terminal and retain a bounded scrollback.
 * RU: Добавляет text в terminal и ограничивает размер истории. */
static void lua_terminal_append(LuaTerminal *terminal, const gchar *text)
{
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(terminal->view));
    GtkTextIter first, end;
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_buffer_insert(buffer, &end, text, -1);
    gint excess = gtk_text_buffer_get_char_count(buffer) - 65536;
    if (excess > 0)
    {
        gtk_text_buffer_get_start_iter(buffer, &first);
        gtk_text_buffer_get_iter_at_offset(buffer, &end, excess);
        gtk_text_buffer_delete(buffer, &first, &end);
    }
    gtk_text_buffer_get_end_iter(buffer, &end);
    gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(terminal->view), &end, 0, FALSE, 0, 0);
}

/* EN: Build the inbox path of terminal in the simulator directory.
 * RU: Формирует путь входящих команд terminal в каталоге симулятора. */
static gchar *lua_terminal_input_path(LuaTerminal *terminal, const gchar *directory)
{
    gchar *name = g_strconcat(terminal->stem, ".in", NULL);
    gchar *path = g_build_filename(directory, name, NULL);
    g_free(name);
    return path;
}

/* EN: Queue the line in entry for terminal and echo it locally.
 * RU: Ставит строку из entry в очередь terminal и отображает её в окне. */
static void lua_terminal_enter(GtkEntry *entry, LuaTerminal *terminal)
{
    const gchar *line = gtk_entry_get_text(entry);
    gsize length = strlen(line);
    if (!length || length > 4096)
        return;
    g_mutex_lock(&terminal->run->lock);
    gchar *directory = g_strdup(terminal->run->live_directory);
    g_mutex_unlock(&terminal->run->lock);
    if (!directory)
        return;

    gchar *path = lua_terminal_input_path(terminal, directory);
    if (!g_file_test(path, G_FILE_TEST_EXISTS) && g_file_set_contents(path, line, length, NULL))
    {
        gchar *echo = g_strdup_printf("> %s\n", line);
        lua_terminal_append(terminal, echo);
        g_free(echo);
        gtk_entry_set_text(entry, "");
        gtk_widget_set_sensitive(GTK_WIDGET(entry), FALSE);
    }
    g_free(path);
    g_free(directory);
}

/* EN: Create a terminal page for the log stem and device title in run.
 * RU: Создаёт страницу терминала журнала stem с заголовком title в run. */
static LuaTerminal *lua_terminal_add(WaveSimulation *run, const gchar *stem, const gchar *title)
{
    LuaTerminal *terminal = g_new0(LuaTerminal, 1);
    terminal->run = run;
    terminal->stem = g_strdup(stem);
    GtkWidget *page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    g_object_set_data_full(G_OBJECT(page), "lua-terminal", terminal, lua_terminal_free);
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    terminal->view = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(terminal->view), FALSE);
    gtk_text_view_set_monospace(GTK_TEXT_VIEW(terminal->view), TRUE);
    gtk_container_add(GTK_CONTAINER(scroll), terminal->view);
    gtk_box_pack_start(GTK_BOX(page), scroll, TRUE, TRUE, 0);
    terminal->entry = GTK_ENTRY(gtk_entry_new());
    gtk_entry_set_placeholder_text(terminal->entry, insight_tr("Enter a Lua device command"));
    gtk_entry_set_max_length(terminal->entry, 1024);
    gtk_box_pack_start(GTK_BOX(page), GTK_WIDGET(terminal->entry), FALSE, FALSE, 0);
    g_signal_connect(terminal->entry, "activate", G_CALLBACK(lua_terminal_enter), terminal);
    gtk_notebook_append_page(GTK_NOTEBOOK(run->lua_terminals), page, gtk_label_new(title));
    gtk_widget_show_all(page);
    return terminal;
}

/* EN: Read new output from path and refresh the input state for terminal.
 * RU: Читает новый вывод из path и обновляет состояние ввода terminal. */
static void lua_terminal_refresh(LuaTerminal *terminal, const gchar *path, const gchar *directory)
{
    FILE *file = g_fopen(path, "rb");
    if (file)
    {
        if (fseek(file, terminal->offset, SEEK_SET) == 0)
        {
            gchar bytes[8192];
            gsize count = fread(bytes, 1, sizeof bytes, file);
            if (count)
            {
                gsize complete = count;
                const gchar *tail = g_utf8_find_prev_char(bytes, bytes + count);
                if (tail && g_utf8_get_char_validated(tail, bytes + count - tail) == (gunichar)-2)
                    complete = tail - bytes;
                gchar *valid = g_utf8_make_valid(bytes, complete);
                terminal->offset += (long)complete;
                lua_terminal_append(terminal, valid);
                g_free(valid);
            }
        }
        fclose(file);
    }
    gchar *input = lua_terminal_input_path(terminal, directory);
    gtk_widget_set_sensitive(GTK_WIDGET(terminal->entry), !g_file_test(input, G_FILE_TEST_EXISTS));
    g_free(input);
}

/* EN: Discover Lua device logs and update their terminal pages in run.
 * RU: Находит журналы Lua-устройств и обновляет страницы терминалов run. */
static void simulation_lua_terminals_refresh(WaveSimulation *run)
{
    if (!run->lua_terminals)
        return;

    g_mutex_lock(&run->lock);
    gchar *directory = g_strdup(run->live_directory);
    g_mutex_unlock(&run->lock);
    if (!directory)
        return;
    GDir *dir = g_dir_open(directory, 0, NULL);
    if (dir)
    {
        const gchar *name;
        while ((name = g_dir_read_name(dir)))
        {
            gboolean spi = g_str_has_prefix(name, "geany-lua-spi-");
            gboolean i2c = g_str_has_prefix(name, "geany-lua-i2c-");
            if ((!spi && !i2c) || !g_str_has_suffix(name, ".log"))
                continue;
            const gchar *id = name + strlen("geany-lua-spi-");
            gsize digits = strspn(id, "0123456789");
            if (!digits || strcmp(id + digits, ".log"))
                continue;

            gchar *stem = g_strndup(name, strlen(name) - 4);
            LuaTerminal *terminal = g_object_get_data(G_OBJECT(run->lua_terminals), stem);
            if (!terminal)
            {
                gchar *title = g_strdup_printf("%s %.*s", spi ? "SPI" : "I²C", (gint)digits, id);
                terminal = lua_terminal_add(run, stem, title);
                g_object_set_data(G_OBJECT(run->lua_terminals), stem, terminal);
                g_free(title);
            }
            gchar *path = g_build_filename(directory, name, NULL);
            lua_terminal_refresh(terminal, path, directory);
            g_free(path);
            g_free(stem);
        }
        g_dir_close(dir);
    }
    g_free(directory);
}
