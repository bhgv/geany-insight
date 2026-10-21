/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_PROJECT_REPORT_UI_H
#define INSIGHT_PROJECT_REPORT_UI_H
#include <gtk/gtk.h>
#include "../insight/desktop/project_report.h"
typedef InsightReportSnapshot *(*InsightReportCapture)(GError **);
void insight_project_report_init(GtkWidget *menu,InsightReportCapture capture);
void insight_project_report_command(guint action);
/* Immutable indexed project membership, without reading source contents. */
InsightReportSnapshot *insight_project_report_capture(GError **);
#endif
