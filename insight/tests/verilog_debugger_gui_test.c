/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include <glib/gstdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    OPEN_DEBUGGER, LOAD_OBSERVER, SET_POINT, RUN_TO_POINT,
    INSPECT_STOP, INSPECT_PREVIEW, WAIT_FOR_CAPTURE, INSPECT_CONNECTIONS, INSPECT_INSTRUMENTS,
    RUN_LIVE_DEVICES, SEND_LIVE_INPUT, CHECK_LIVE_DEVICES
} TestPhase;

static TestPhase phase;
static gchar *fixture_source;
static gchar *observer_source;
static guint point_line;
static gboolean passed, live_devices;

/* Find a visible top-level window by its editor title. */
/* Ищет видимое окно верхнего уровня по заголовку редактора. */
static GtkWidget *find_window(const gchar *title)
{
    GList *windows = gtk_window_list_toplevels();
    GtkWidget *found = NULL;
    for (GList *entry = windows; entry && !found; entry = entry->next)
    {
        GtkWidget *window = entry->data;
        if (gtk_widget_get_visible(window) &&
            !g_strcmp0(gtk_window_get_title(GTK_WINDOW(window)), title))
            found = window;
    }
    g_list_free(windows);
    return found;
}

/* Find a control by caption or by its unique role in the dialog. */
/* Ищет элемент по подписи или по его однозначной роли в диалоге. */
static GtkWidget *find_control(GtkWidget *widget, const gchar *key)
{
    if (!g_strcmp0(gtk_widget_get_name(widget), key))
        return widget;
    if (GTK_IS_ENTRY(widget) && !g_strcmp0(gtk_entry_get_placeholder_text(GTK_ENTRY(widget)), key))
        return widget;
    if (GTK_IS_BUTTON(widget) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), key))
        return widget;
    if (GTK_IS_LABEL(widget) && !g_strcmp0(gtk_label_get_text(GTK_LABEL(widget)), key))
        return widget;
    if (GTK_IS_TREE_VIEW(widget))
    {
        GtkTreeViewColumn *column = gtk_tree_view_get_column(GTK_TREE_VIEW(widget), 0);
        if (column && !g_strcmp0(gtk_tree_view_column_get_title(column), key))
            return widget;
    }
    if (GTK_IS_STATUSBAR(widget) && !strcmp(key, "statusbar"))
        return widget;
    if (GTK_IS_NOTEBOOK(widget) && !strcmp(key, "notebook"))
        return widget;
    if (GTK_IS_TEXT_VIEW(widget) && !strcmp(key, "console"))
        return widget;
    if (GTK_IS_COMBO_BOX_TEXT(widget) && !strcmp(key, "tracks"))
        return widget;
    if (GTK_IS_SPIN_BUTTON(widget) && !strcmp(key, "source-line") &&
        gtk_adjustment_get_upper(gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(widget))) == G_MAXINT)
        return widget;
    if (GTK_IS_MENU_ITEM(widget))
    {
        if (!g_strcmp0(gtk_widget_get_name(widget), key))
            return widget;
        GtkMenuItem *item = GTK_MENU_ITEM(widget);
        if (!g_strcmp0(gtk_menu_item_get_label(item), key))
            return widget;
        GtkWidget *submenu = gtk_menu_item_get_submenu(item);
        GtkWidget *found = submenu ? find_control(submenu, key) : NULL;
        if (found)
            return found;
    }
    if (!GTK_IS_CONTAINER(widget))
        return NULL;

    GList *children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget *found = NULL;
    for (GList *entry = children; entry && !found; entry = entry->next)
        found = find_control(entry->data, key);
    g_list_free(children);
    return found;
}

/* Activate a button only when the user could click it. */
/* Нажимает кнопку, только когда она доступна пользователю. */
static gboolean click(GtkWidget *window, const gchar *label)
{
    GtkWidget *button = find_control(window, label);
    if (!GTK_IS_BUTTON(button) || !gtk_widget_is_sensitive(button))
        return FALSE;
    gtk_button_clicked(GTK_BUTTON(button));
    return TRUE;
}

/* Read the displayed simulator console without accessing private run state. */
/* Читает видимую консоль симулятора без доступа к внутреннему состоянию сеанса. */
static gboolean console_contains(GtkWidget *window, const gchar *expected)
{
    GtkWidget *console = find_control(window, "console");
    if (!console)
        return FALSE;
    GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console));
    GtkTextIter start, end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar *text = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gboolean found = strstr(text, expected) != NULL;
    g_free(text);
    return found;
}

/* Select the expected signal in the real waveform track list. */
/* Выбирает ожидаемый сигнал в списке дорожек настоящей диаграммы. */
static gboolean select_track(GtkWidget *window, const gchar *signal)
{
    GtkWidget *tracks = find_control(window, "tracks");
    if (!GTK_IS_COMBO_BOX(tracks))
        return FALSE;
    GtkTreeModel *model = gtk_combo_box_get_model(GTK_COMBO_BOX(tracks));
    GtkTreeIter row;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &row); valid;
         valid = gtk_tree_model_iter_next(model, &row))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &row, 0, &name, -1);
        gboolean found = !g_strcmp0(name, signal);
        g_free(name);
        if (found)
        {
            gtk_combo_box_set_active_iter(GTK_COMBO_BOX(tracks), &row);
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean select_scope_signal(GtkTreeView *view, const gchar *scope, const gchar *signal)
{
    GtkTreeModel *model = gtk_tree_view_get_model(view);
    GtkTreeIter root;
    for (gboolean valid = gtk_tree_model_get_iter_first(model, &root); valid;
         valid = gtk_tree_model_iter_next(model, &root))
    {
        gchar *name = NULL;
        gtk_tree_model_get(model, &root, 0, &name, -1);
        gboolean match = !g_strcmp0(name, scope);
        g_free(name);
        if (!match)
            continue;
        GtkTreeIter child;
        for (valid = gtk_tree_model_iter_children(model, &child, &root); valid;
             valid = gtk_tree_model_iter_next(model, &child))
        {
            gtk_tree_model_get(model, &child, 0, &name, -1);
            match = !g_strcmp0(name, signal);
            g_free(name);
            if (match)
            {
                GtkTreePath *path = gtk_tree_model_get_path(model, &child);
                gtk_tree_view_expand_to_path(view, path);
                gtk_tree_selection_select_path(gtk_tree_view_get_selection(view), path);
                gtk_tree_path_free(path);
                return TRUE;
            }
        }
        break;
    }
    return FALSE;
}

static gboolean open_hdl_uses(gpointer data)
{
    (void)data;
    GtkWidget *map = find_window("Signal Map");
    if (!map)
        return G_SOURCE_CONTINUE;
    GtkWidget *scopes = find_control(map, "VCD scopes and signals");
    GtkWidget *aliases = find_control(map, "Aliases on the same recorded trace");
    if (!scopes || !aliases ||
        !select_scope_signal(GTK_TREE_VIEW(scopes), "counter_tb.dut", "count"))
        return G_SOURCE_CONTINUE;
    gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(aliases)));
    GtkWidget *button = gtk_dialog_get_widget_for_response(GTK_DIALOG(map), GTK_RESPONSE_OK);
    if (!GTK_IS_BUTTON(button) || !gtk_widget_is_sensitive(button))
        return G_SOURCE_CONTINUE;
    g_print("Opening HDL Uses from the Signal Map.\n");
    gtk_button_clicked(GTK_BUTTON(button));
    return G_SOURCE_REMOVE;
}

typedef struct
{
    gboolean bit;
    gboolean clock;
} GraphChecks;

static gboolean check_connection(GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *row,
                                 gpointer data)
{
    (void)path;
    GraphChecks *checks = data;
    gchar *role, *signal, *selection;
    gtk_tree_model_get(model, row, 0, &role, 1, &signal, 6, &selection, -1);
    if (!g_strcmp0(signal, "counter_tb.dut.odd") &&
        !g_strcmp0(role, "assign-destination") && !g_strcmp0(selection, "[0] / all"))
        checks->bit = TRUE;
    if (!g_strcmp0(signal, "counter_tb.dut.clk") && !g_strcmp0(role, "posedge-source"))
        checks->clock = TRUE;
    g_free(role);
    g_free(signal);
    g_free(selection);
    return checks->bit && checks->clock;
}

static gboolean accept_ports(gpointer data)
{
    (void)data;
    GtkWidget *dialog = find_window("Port Signals");
    if (!dialog)
        return G_SOURCE_CONTINUE;
    if (live_devices)
    {
        GtkWidget *recipe = find_control(dialog, "console");
        if (!GTK_IS_TEXT_VIEW(recipe))
            return G_SOURCE_CONTINUE;
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(recipe)),
            "module live_devices\n"
            "duration 10000000\n"
            "live-interval 250\n"
            "live-input enable 1 0\n"
            "switch switch_on\n"
            "button push\n"
            "capture-trigger fall push\n"
            "monitor pushed 1\n"
            "led ready\n"
            "display result\n"
            "slider level\n"
            "display level_out 8 dec\n"
            "scope level_out\n"
            "scope-xy level_out result\n"
            "scope-xyz level_out result ready\n"
            "monitor model_result 8\n"
            "model ext external_memory \"external memory.sv\"\n"
            "model-param ext DATA \"8'h3c\"\n"
            "model-value ext mask \"8'h05\"\n"
            "model-share ext ready enable\n"
            "model-link ext external_data data 8\n"
            "sram - - - - - 4\n"
            "uart rx tx 1000000 console\n", -1);
    }
    gtk_dialog_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* Answer the real modal chooser once GTK has selected the requested file. */
/* Подтверждает реальный диалог выбора, когда GTK выбрал нужный файл. */
static gboolean accept_fixture(gpointer data)
{
    const gchar *path = data;
    GtkWidget *window = find_window("Load Verilog Observer");
    if (!GTK_IS_FILE_CHOOSER(window))
        return G_SOURCE_CONTINUE;
    GtkFileChooser *chooser = GTK_FILE_CHOOSER(window);
    gchar *selected = gtk_file_chooser_get_filename(chooser);
    gboolean ready = !g_strcmp0(selected, path);
    g_free(selected);
    if (!ready)
    {
        gtk_file_chooser_select_filename(chooser, path);
        return G_SOURCE_CONTINUE;
    }
    gtk_dialog_response(GTK_DIALOG(window), GTK_RESPONSE_ACCEPT);
    return G_SOURCE_REMOVE;
}

/* Fail with the current phase so the CI log identifies the blocked action. */
/* Завершает проверку с номером этапа, чтобы в CI было видно, где она остановилась. */
static gboolean fail_timeout(gpointer data)
{
    (void)data;
    g_printerr("Verilog debugger GTK test timed out at phase %u\n", (guint)phase);
    GList *windows = gtk_window_list_toplevels();
    for (GList *entry = windows; entry; entry = entry->next)
    {
        GtkWidget *window = entry->data;
        if (!gtk_widget_get_visible(window))
            continue;
        const gchar *title = gtk_window_get_title(GTK_WINDOW(window));
        g_printerr("Visible window: %s\n", title ? title : "(untitled)");
        if (GTK_IS_MESSAGE_DIALOG(window))
        {
            gchar *message = NULL, *details = NULL;
            g_object_get(window, "text", &message, "secondary-text", &details, NULL);
            g_printerr("%s\n%s\n", message ? message : "", details ? details : "");
            g_free(message);
            g_free(details);
        }
        GtkWidget *status = find_control(window, "statusbar");
        if (status)
        {
            GtkWidget *area = gtk_statusbar_get_message_area(GTK_STATUSBAR(status));
            GList *children = gtk_container_get_children(GTK_CONTAINER(area));
            for (GList *child = children; child; child = child->next)
                if (GTK_IS_LABEL(child->data))
                    g_printerr("Status: %s\n", gtk_label_get_text(GTK_LABEL(child->data)));
            g_list_free(children);
        }
        GtkWidget *console = find_control(window, "console");
        if (console)
        {
            GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(console));
            GtkTextIter first, last;
            gtk_text_buffer_get_bounds(buffer, &first, &last);
            gchar *text = gtk_text_buffer_get_text(buffer, &first, &last, FALSE);
            g_printerr("Console:\n%s\n", text);
            g_free(text);
        }
    }
    g_list_free(windows);
    exit(1);
}

static void log_graph_status(GtkStatusbar *bar, guint context, const gchar *message,
                             gpointer data)
{
    (void)bar;
    (void)context;
    (void)data;
    if (phase == INSPECT_CONNECTIONS && message && *message)
        g_print("Graph status: %s\n", message);
}

/* Drive menu, Lua breakpoint, source navigation and VCD through GTK widgets. */
/* Проверяет меню, точку Lua, переход к исходнику и VCD через виджеты GTK. */
static gboolean drive_debugger(gpointer data)
{
    (void)data;
    GeanyDocument *document = document_get_current();
    if (!DOC_VALID(document))
        return G_SOURCE_CONTINUE;

    if (phase == OPEN_DEBUGGER)
    {
        GtkWidget *item = find_control(gtk_widget_get_toplevel(GTK_WIDGET(document->editor->sci)),
                                      "insight-tools-debug-current-hdl");
        if (!GTK_IS_MENU_ITEM(item))
            return G_SOURCE_CONTINUE;
        phase = live_devices ? RUN_LIVE_DEVICES : LOAD_OBSERVER;
        g_timeout_add(100, accept_ports, NULL);
        gtk_menu_item_activate(GTK_MENU_ITEM(item));
        return G_SOURCE_CONTINUE;
    }

    GtkWidget *simulation = find_window("Verilog Simulation");
    if (phase == WAIT_FOR_CAPTURE)
    {
        GtkWidget *capture = find_window("VCD Waveforms");
        if (!simulation && capture && select_track(capture, "counter_tb.dut.count"))
        {
            phase = INSPECT_CONNECTIONS;
            GtkWidget *editor = gtk_widget_get_toplevel(GTK_WIDGET(document->editor->sci));
            GtkWidget *status = find_control(editor, "statusbar");
            if (status)
                g_signal_connect(status, "text-pushed", G_CALLBACK(log_graph_status), NULL);
            g_timeout_add(100, open_hdl_uses, NULL);
            if (!click(capture, "Signal Map…"))
                return fail_timeout(NULL);
        }
        return G_SOURCE_CONTINUE;
    }
    if (phase == INSPECT_CONNECTIONS)
    {
        GtkWidget *uses = find_window("HDL Uses");
        if (!uses)
            return G_SOURCE_CONTINUE;
        GtkWidget *notebook = find_control(uses, "notebook");
        if (!notebook)
            return fail_timeout(NULL);
        gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 1);
        GtkWidget *page = gtk_notebook_get_nth_page(GTK_NOTEBOOK(notebook), 1);
        GtkWidget *view = page ? gtk_bin_get_child(GTK_BIN(page)) : NULL;
        GraphChecks checks = {FALSE, FALSE};
        if (GTK_IS_TREE_VIEW(view))
            gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),
                                   check_connection, &checks);
        if (!checks.bit || !checks.clock)
        {
            g_printerr("Verilog GTK Connections missing bit edge (%d) or clock edge (%d).\n",
                       checks.bit, checks.clock);
            return fail_timeout(NULL);
        }
        gtk_dialog_response(GTK_DIALOG(uses), GTK_RESPONSE_CLOSE);
        GtkWidget *capture = find_window("VCD Waveforms");
        if (!capture || !click(capture, "Signal Instruments…"))
            return fail_timeout(NULL);
        phase = INSPECT_INSTRUMENTS;
        return G_SOURCE_CONTINUE;
    }
    if (phase == INSPECT_INSTRUMENTS)
    {
        GtkWidget *window = find_window("Signal Instruments");
        if (!window)
            return G_SOURCE_CONTINUE;
        GtkWidget *y = find_control(window, "insight-instrument-Y");
        GtkWidget *z = find_control(window, "insight-instrument-Z");
        GtkWidget *range = find_control(window, "insight-instrument-range");
        if (!y || !z || !range)
            return fail_timeout(NULL);
        if (gtk_combo_box_get_active(GTK_COMBO_BOX(y)) == 0)
        {
            gtk_combo_box_set_active(GTK_COMBO_BOX(y), 1);
            return G_SOURCE_CONTINUE;
        }
        if (g_str_has_prefix(gtk_label_get_text(GTK_LABEL(range)), "XY plot"))
        {
            gtk_combo_box_set_active(GTK_COMBO_BOX(z), 2);
            return G_SOURCE_CONTINUE;
        }
        if (!g_str_has_prefix(gtk_label_get_text(GTK_LABEL(range)), "XYZ projection"))
            return G_SOURCE_CONTINUE;
        g_print("Verilog GTK: Lua stop, source navigation, VCD, HDL Connections and instruments passed.\n");
        passed = TRUE;
        gtk_main_quit();
        return G_SOURCE_REMOVE;
    }
    if (!simulation)
        return G_SOURCE_CONTINUE;

    if (phase == RUN_LIVE_DEVICES && console_contains(simulation, "GEANY-VVP-READY"))
    {
        if (click(simulation, "UART Console…") && click(simulation, "Continue"))
            phase = SEND_LIVE_INPUT;
    }
    else if (phase == SEND_LIVE_INPUT)
    {
        GtkWidget *uart = find_window("UART Console");
        GtkWidget *entry = uart ? find_control(uart, "Text to send; press Enter") : NULL;
        GtkWidget *input = find_control(simulation, "Binary value (0, 1, x, z)");
        if (entry && input)
        {
            gtk_entry_set_text(GTK_ENTRY(input), "1");
            GtkWidget *toggle = find_control(simulation, "switch_on");
            GtkWidget *slider = find_control(simulation, "insight-slider-level");
            if (GTK_IS_RANGE(slider) && gtk_widget_is_sensitive(slider) &&
                GTK_IS_TOGGLE_BUTTON(toggle) && gtk_widget_is_sensitive(toggle) &&
                click(simulation, "Apply Input") && click(simulation, "push"))
            {
                gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle), TRUE);
                gtk_range_set_value(GTK_RANGE(slider), 173);
                gtk_entry_set_text(GTK_ENTRY(entry), "OK");
                g_signal_emit_by_name(entry, "activate");
                phase = CHECK_LIVE_DEVICES;
            }
        }
    }
    else if (phase == CHECK_LIVE_DEVICES)
    {
        GtkWidget *uart = find_window("UART Console");
        GtkWidget *readings = find_control(simulation, "insight-live-port-readings");
        const gchar *values = readings ? gtk_label_get_text(GTK_LABEL(readings)) : "";
        GtkWidget *lamp = find_control(simulation, "insight-indicator-ready");
        GtkWidget *hex = find_control(simulation, "insight-indicator-result");
        GtkWidget *decimal = find_control(simulation, "insight-indicator-level_out");
        GtkWidget *time_plot = find_control(simulation, "insight-live-scope-1");
        GtkWidget *xy_plot = find_control(simulation, "insight-live-scope-2");
        GtkWidget *xyz_plot = find_control(simulation, "insight-live-scope-3");
        if (GTK_IS_LABEL(time_plot) && strstr(gtk_label_get_text(GTK_LABEL(time_plot)), " · Y ") &&
            GTK_IS_LABEL(xy_plot) && strstr(gtk_label_get_text(GTK_LABEL(xy_plot)), " · X ") &&
            GTK_IS_LABEL(xyz_plot) && strstr(gtk_label_get_text(GTK_LABEL(xyz_plot)), " · Z ") &&
            !console_contains(simulation, "GEANY-SCOPE") &&
            GTK_IS_LABEL(lamp) && g_str_has_suffix(gtk_label_get_text(GTK_LABEL(lamp)), "ON") &&
            GTK_IS_LABEL(hex) && !strcmp(gtk_label_get_text(GTK_LABEL(hex)), "0xA5") &&
            GTK_IS_LABEL(decimal) && !strcmp(gtk_label_get_text(GTK_LABEL(decimal)), "173") &&
            uart && console_contains(uart, "OK") &&
            console_contains(simulation, "Waveform capture started at") && strstr(values, "pushed = 1") &&
            strstr(values, "ready = 1") && strstr(values, "result = 10100101 (165)") &&
            strstr(values, "model_result = 00111001 (57)"))
        {
            g_print("Verilog GTK: live inputs, capture trigger, monitors, SRAM, external model and UART passed.\n");
            passed = TRUE;
            gtk_widget_destroy(simulation);
            gtk_main_quit();
            return G_SOURCE_REMOVE;
        }
    }
    else if (phase == LOAD_OBSERVER && console_contains(simulation, "GEANY-VVP-READY"))
    {
        if (!find_control(simulation, "Port Signals…"))
        {
            g_printerr("Missing Port Signals button in the Verilog debugger.\n");
            return fail_timeout(NULL);
        }
        GtkWidget *load = find_control(simulation, "Load Lua Observer…");
        if (load && gtk_widget_is_sensitive(load))
        {
            phase = SET_POINT;
            g_timeout_add(100, accept_fixture, observer_source);
            gtk_button_clicked(GTK_BUTTON(load));
        }
    }
    else if (phase == SET_POINT && console_contains(simulation, "Lua observer loaded."))
    {
        GtkWidget *line = find_control(simulation, "source-line");
        GtkWidget *script = find_control(simulation, "Lua decides");
        if (line && script)
        {
            gtk_spin_button_set_value(GTK_SPIN_BUTTON(line), point_line);
            gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(script), TRUE);
            if (click(simulation, "Set Point"))
                phase = RUN_TO_POINT;
        }
    }
    else if (phase == RUN_TO_POINT && console_contains(simulation, "1 source breakpoint(s)."))
    {
        if (click(simulation, "Continue"))
            phase = INSPECT_STOP;
    }
    else if (phase == INSPECT_STOP && console_contains(simulation, "HDL hit 2 at tick 15") &&
             find_control(simulation, "HDL hits = 2"))
    {
        if (click(simulation, "Open HDL Line"))
        {
            GeanyDocument *current = document_get_current();
            if (!DOC_VALID(current) || sci_get_current_line(current->editor->sci) + 1 != (gint)point_line)
            {
                g_printerr("Verilog GTK opened the wrong HDL line.\n");
                exit(1);
            }
            if (click(simulation, "Preview Waveform"))
                phase = INSPECT_PREVIEW;
        }
    }
    else if (phase == INSPECT_PREVIEW)
    {
        GtkWidget *preview = find_window("VCD Waveforms");
        GtkWidget *resume = find_control(simulation, "Continue");
        if (preview && resume && gtk_widget_is_sensitive(resume) && select_track(preview, "counter_tb.count"))
        {
            gtk_widget_destroy(preview);
            if (click(simulation, "Continue"))
                phase = WAIT_FOR_CAPTURE;
        }
    }
    return G_SOURCE_CONTINUE;
}

/* Create a single-file testbench and start the real editor with a fresh profile. */
/* Готовит однофайловый тестбенч и запускает редактор с отдельным профилем. */
int main(int argc, char **argv)
{
    live_devices = argc == 4 && !strcmp(argv[3], "--live");
    if (argc != 3 && !live_devices)
    {
        g_printerr("Usage: test_insight_verilog_debugger_gui FIXTURES WORK_DIRECTORY [--live]\n");
        return 2;
    }
    gchar *fixture_dir = g_canonicalize_filename(argv[1], NULL);
    gchar *work_dir = g_canonicalize_filename(argv[2], NULL);
    gchar *counter = g_build_filename(fixture_dir, live_devices ? "live_devices.sv" : "counter.v", NULL);
    gchar *testbench = g_build_filename(fixture_dir, "counter_tb.v", NULL);
    observer_source = g_build_filename(fixture_dir, "observe-hdl.lua", NULL);
    gchar *profile = g_build_filename(work_dir, "profile", NULL);
    fixture_source = g_build_filename(work_dir, live_devices ? "debug_live.sv" : "debug_counter.v", NULL);
    gchar *counter_text = NULL, *testbench_text = NULL;
    if (g_mkdir_with_parents(profile, 0700) || !g_file_get_contents(counter, &counter_text, NULL, NULL) ||
        !g_file_get_contents(testbench, &testbench_text, NULL, NULL))
    {
        g_printerr("Cannot prepare Verilog GTK fixtures.\n");
        return 1;
    }
    const gchar *assignment = strstr(counter_text, "count <= count + 1'b1;");
    if (!live_devices && !assignment)
        return 1;
    point_line = 2;
    if (assignment)
        for (const gchar *byte = counter_text; byte < assignment; byte++)
            if (*byte == '\n')
                point_line++;
    gchar *combined = live_devices ? g_strdup(counter_text) :
        g_strconcat("`timescale 1ns/1ns\n", counter_text, "\n", testbench_text, NULL);
    gboolean saved = g_file_set_contents(fixture_source, combined, -1, NULL);
    g_free(combined);
    g_free(counter_text);
    g_free(testbench_text);
    g_free(counter);
    g_free(testbench);
    if (saved && live_devices)
    {
        gchar *model = g_build_filename(fixture_dir, "external_memory.sv", NULL);
        gchar *destination = g_build_filename(work_dir, "external memory.sv", NULL);
        gchar *text = NULL;
        gsize size = 0;
        saved = g_file_get_contents(model, &text, &size, NULL) &&
                g_file_set_contents(destination, text, size, NULL);
        g_free(text);
        g_free(destination);
        g_free(model);
    }
    g_free(fixture_dir);
    g_free(work_dir);
    if (!saved)
        return 1;

    gchar *editor_args[] = {argv[0], "--new-instance", "--no-session", "--no-plugins",
                           "--config", profile, fixture_source, NULL};
    g_timeout_add(100, drive_debugger, NULL);
    g_timeout_add_seconds(60, fail_timeout, NULL);
    gint result = main_lib(G_N_ELEMENTS(editor_args) - 1, editor_args);
    g_free(profile);
    g_free(fixture_source);
    g_free(observer_source);
    return passed ? result : 1;
}
