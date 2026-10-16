/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_FILETYPE_OPTIONS_H
#define INSIGHT_FILETYPE_OPTIONS_H
#include <glib.h>
typedef struct {
 gboolean enabled,soft_wrap,hard_wrap,line_numbers,white_space,line_endings,indent_guides,right_margin;
 gint tab_width,indent_width,indent_type,auto_indent,margin;
} InsightFiletypeOptions;
void insight_filetype_options_default(InsightFiletypeOptions *);
gboolean insight_filetype_options_valid(const InsightFiletypeOptions *,GError **);
/* Missing group inherits. Malformed settings reject the entire override. */
gboolean insight_filetype_options_read(InsightFiletypeOptions *,GKeyFile *,GError **);
gboolean insight_filetype_options_write(const InsightFiletypeOptions *,GKeyFile *,GError **);
#endif
