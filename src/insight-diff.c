/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Assemble the internal comparison modules in dependency order.
 * RU: Собирает модули внутреннего сравнения в порядке зависимостей. */
#define INSIGHT_LOCALIZE_FORMATS
#include "insight-compare-ui.h"
#include "insight-i18n.h"
#include "../insight/desktop/glib_compat.h"
#include "insight-diff.h"
#include "insight-hex.h"
#include "insight-stream-ui.h"
#include "insight-history-ui.h"
#include "insight-directory.h"
#include "insight-help.h"
#include "../insight/core/diff.h"
#include <string.h>
#include <gio/gio.h>

static InsightDiffColorize colorizer;

#include "insight-diff/state.inc"
#include "insight-diff/history.inc"
#include "insight-diff/navigation.inc"
#include "insight-diff/search.inc"
#include "insight-diff/editing.inc"
#include "insight-diff/rendering.inc"
#include "insight-diff/lifecycle.inc"
#include "insight-diff/blocks.inc"
#include "insight-diff/symbol-context.inc"
#include "insight-diff/comparison.inc"
#include "insight-diff/window-tools.inc"
#include "insight-diff/symbol-navigation.inc"
#include "insight-diff/symbol-mouse.inc"
#include "insight-diff/symbol-changes.inc"
#include "insight-diff/symbol-browser.inc"
#include "insight-diff/layout.inc"
#include "insight-diff/window.inc"
#include "insight-diff/review.inc"
