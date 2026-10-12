/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_INDEX_CACHE_H
#define INSIGHT_INDEX_CACHE_H
#include <gio/gio.h>
#include "index.h"
/* Uses local-file replacement via GIO; errors preserve the previous cache.
 * Returned index/root are caller-owned; root uses malloc/free. */
gboolean insight_cache_save(const char *,const InsightIndex *,const char *,GError **);
InsightIndex *insight_cache_load(const char *,char **,GError **);
#endif
