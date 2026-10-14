/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_OVERVIEW_H
#define GEANY_INSIGHT_OVERVIEW_H
#include "document.h"
GtkWidget *insight_overview_wrap(GeanyDocument *);
void insight_overview_tags_changed(GeanyDocument *);
void insight_overview_install_commands(void);
void insight_overview_set_visible(gboolean);
gboolean insight_overview_get_visible(void);
void insight_overview_read(GKeyFile *);
void insight_overview_write(GKeyFile *);
#endif
