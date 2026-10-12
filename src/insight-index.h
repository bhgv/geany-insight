/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef GEANY_INSIGHT_INDEX_H
#define GEANY_INSIGHT_INDEX_H
#include <glib.h>
#include "tagmanager/tm_source_file.h"
#include "tagmanager/tm_tag.h"
#include "../insight/core/index.h"
/* Copy native tag data into the owning UTF-8 index. NULL/empty tags clear path.
 * Reject >250k total symbols without changing the previous file snapshot. */
gboolean insight_native_import(InsightIndex *,TMSourceFile *,const char *);
guint insight_native_definition_rank(const InsightSymbol *,const char *);
#endif
