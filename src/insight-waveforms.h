/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Public entry points for the Verilog debugger and VCD waveform viewer.
 * RU: Публичные точки входа отладчика Verilog и просмотра диаграмм VCD.
 */
#ifndef INSIGHT_WAVEFORMS_H
#define INSIGHT_WAVEFORMS_H
#include <gtk/gtk.h>

typedef void (*InsightWaveSource)(const gchar *hierarchy, gboolean show_uses, GtkWindow *viewer);
gboolean insight_waveforms_select_track(GtkWindow *window, const gchar *hierarchy);
typedef void (*InsightWaveContext)(const gchar *hierarchy, GtkTextView *context);
typedef void (*InsightWaveLocation)(const gchar *filename, guint line);
void insight_waveforms_open(GtkWindow *parent, const gchar *session_dir, InsightWaveSource navigate);
gint insight_waveforms_run_setup_dialog(GtkDialog *dialog);
void insight_waveforms_simulate(GtkWindow *parent, const gchar *bin, const gchar *include_dir,
                                const gchar *const *sources, gboolean interactive, InsightWaveSource navigate,
                                InsightWaveContext show_context, InsightWaveLocation open_location,
                                const gchar *settings_directory, const gchar *project,
                                gboolean configure_ports);
#endif
