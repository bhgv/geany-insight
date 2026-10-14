/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_LANGUAGES_H
#define INSIGHT_LANGUAGES_H
#include "filetypes.h"
#include "ScintillaWidget.h"
typedef struct InsightLanguageFilter InsightLanguageFilter;
InsightLanguageFilter *insight_language_filter_new(const gchar *,GeanyFiletype *);
gboolean insight_language_filter_match(const InsightLanguageFilter *,const gchar *);
void insight_language_filter_free(InsightLanguageFilter *);
gchar **insight_languages_names(void);
gchar *insight_languages_profile_name(ScintillaObject *,const gchar *,GeanyFiletype *);
void insight_languages_init(void);
void insight_languages_finalize(void);
GeanyFiletype *insight_languages_detect(const gchar *path);
gboolean insight_languages_compatible(const gchar *from,GeanyFiletype *fallback,const gchar *to);
gboolean insight_languages_search_path(const gchar *,GeanyFiletype *,const gchar *);
gboolean insight_languages_keyword(ScintillaObject *,const gchar *);
void insight_languages_apply(ScintillaObject *,const gchar *,GeanyFiletype *);
void insight_languages_show(GtkWidget *,gpointer);
#endif
