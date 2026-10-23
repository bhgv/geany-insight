/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_WEB_SEARCH_H
#define INSIGHT_WEB_SEARCH_H
#include <gio/gio.h>
/* One %S after the HTTP(S) authority; term becomes one escaped URL component. */
gchar *insight_web_search_url(const gchar *pattern,const gchar *term,GError **error);
#endif
