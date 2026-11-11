/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Exercises Verilog debugger launch, live devices and waveform tools in GTK.
 * RU: Проверяет запуск отладчика Verilog, живые устройства и осциллограммы в GTK. */
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
    DISABLE_SIGNAL_RULE, ENABLE_SIGNAL_RULE,
    INSPECT_STOP, VERIFY_REGISTER_WRITE, VERIFY_REGISTER_RESTORE, INSPECT_PREVIEW,
    WAIT_FOR_CAPTURE, INSPECT_CONNECTIONS, INSPECT_INSTRUMENTS,
    RUN_LIVE_DEVICES, SEND_LIVE_INPUT, CHECK_LIVE_DEVICES
} TestPhase;

static TestPhase phase;
static gchar *fixture_source;
static gchar *observer_source, *spi_image, *i2c_image;
static guint point_line;
static gboolean passed, live_devices;

#include "verilog-debugger-gui/controls.inc"
#include "verilog-debugger-gui/devices.inc"
#include "verilog-debugger-gui/history.inc"
#include "verilog-debugger-gui/session.inc"

/* EN: Create a single-file testbench and start the real editor with a fresh profile.
 * RU: Готовит однофайловый тестбенч и запускает редактор с отдельным профилем. */
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
        spi_image = g_build_filename(work_dir, "SPI image.hex", NULL);
        i2c_image = g_build_filename(work_dir, "I2C image.hex", NULL);
        gchar *model = g_build_filename(fixture_dir, "external_memory.sv", NULL);
        gchar *destination = g_build_filename(work_dir, "external memory.sv", NULL);
        gchar *text = NULL;
        gsize size = 0;
        saved = g_file_get_contents(model, &text, &size, NULL) &&
                g_file_set_contents(destination, text, size, NULL) &&
                g_file_set_contents(spi_image, "@10 ba\n", -1, NULL) &&
                g_file_set_contents(i2c_image, "@1234 be\n", -1, NULL);
        g_free(text);
        g_free(destination);
        g_free(model);
    }
    g_free(fixture_dir);
    g_free(work_dir);
    if (!saved || (!live_devices && !seed_signal_rules(profile)))
        return 1;

    gchar *editor_args[] = {argv[0], "--new-instance", "--no-session", "--no-plugins",
                           "--config", profile, fixture_source, NULL};
    g_timeout_add(100, drive_debugger, NULL);
    g_timeout_add_seconds(60, fail_timeout, NULL);
    gint result = main_lib(G_N_ELEMENTS(editor_args) - 1, editor_args);
    g_free(profile);
    g_free(fixture_source);
    g_free(observer_source);
    g_free(spi_image);
    g_free(i2c_image);
    return passed ? result : 1;
}
