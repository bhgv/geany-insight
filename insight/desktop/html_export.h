/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_HTML_EXPORT_H
#define INSIGHT_HTML_EXPORT_H
#include <gio/gio.h>
#define INSIGHT_HTML_SOURCE_LIMIT (1024u*1024u)
#define INSIGHT_HTML_OUTPUT_LIMIT (8u*1024u*1024u)
typedef struct { guint fore,back,size;gboolean bold,italic,underline;gchar font[257]; } InsightHtmlStyle;
typedef struct { gchar *text,*title;guchar *styles;gsize length;guint first_line,tab_width;InsightHtmlStyle palette[256]; } InsightHtmlSnapshot;
void insight_html_snapshot_free(InsightHtmlSnapshot *);
/* Immutable UTF-8 capture, styles indexed by source byte. Owned result is a
 * standalone HTML5 document with embedded CSS and no executable content. */
gchar *insight_html_export_build(const InsightHtmlSnapshot *,gboolean,GCancellable *,GError **);
#endif
