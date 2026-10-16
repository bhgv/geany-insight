/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_FILETYPE_EDITING_H
#define INSIGHT_FILETYPE_EDITING_H
#include <gtk/gtk.h>
#include "filetype_options.h"
GtkWidget *insight_filetype_editing_new(void);
void insight_filetype_editing_set(GtkWidget *,const InsightFiletypeOptions *);
void insight_filetype_editing_get(GtkWidget *,InsightFiletypeOptions *);
#endif
