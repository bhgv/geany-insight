/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_WAVEFORMS_H
#define INSIGHT_WAVEFORMS_H
#include <gtk/gtk.h>

typedef void (*InsightWaveSource)(const gchar *hierarchy, gboolean show_uses, GtkWindow *viewer);
gboolean insight_waveforms_select_track(GtkWindow *window, const gchar *hierarchy);
typedef void (*InsightWaveLocation)(const gchar *filename, guint line);
void insight_waveforms_open(GtkWindow *parent, const gchar *session_dir, InsightWaveSource navigate);
void insight_waveforms_simulate(GtkWindow *parent, const gchar *bin, const gchar *include_dir,
                                const gchar *const *sources, gboolean interactive, InsightWaveSource navigate,
                                InsightWaveLocation open_location,
                                const gchar *settings_directory, const gchar *project);
#endif
