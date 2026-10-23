/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_RECORDING_H
#define INSIGHT_RECORDING_H
#include "editor.h"
void insight_recording_init(GtkWidget *menu);
void insight_recording_notify(GeanyEditor *editor, SCNotification *nt);
void insight_recording_command(guint action);
#endif
