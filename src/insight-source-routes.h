/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_SOURCE_ROUTES_H
#define INSIGHT_SOURCE_ROUTES_H
#include <gtk/gtk.h>
GtkWidget *insight_source_routes_new(void);
void insight_source_routes_capture(GtkWidget *);
GtkWidget *insight_source_routes_back_button(GtkWidget *);
typedef enum { INSIGHT_CALL_SHOW, INSIGHT_CALL_FROM_CARET, INSIGHT_CALL_TO_CARET, INSIGHT_CALL_FIND } InsightCallCommand;
/* Open the call-path options directly; caret endpoints require no manual nodes. */
void insight_source_routes_calls(GtkWidget *,InsightCallCommand);
#endif
