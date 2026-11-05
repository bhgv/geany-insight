/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_WAVEFORMS_H
#define INSIGHT_WAVEFORMS_H
#include <gtk/gtk.h>

typedef void (*InsightWaveSource)(const gchar *hierarchy);
void insight_waveforms_open(GtkWindow *parent, const gchar *session_dir, InsightWaveSource navigate);
void insight_waveforms_simulate(GtkWindow *parent, const gchar *bin, const gchar *include_dir,
                                const gchar *const *sources, gboolean interactive, InsightWaveSource navigate);
#endif
