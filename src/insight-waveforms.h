/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_WAVEFORMS_H
#define INSIGHT_WAVEFORMS_H
#include <gtk/gtk.h>

typedef void (*InsightWaveSource)(const gchar *hierarchy);
void insight_waveforms_open(GtkWindow *parent, InsightWaveSource navigate);
#endif
