/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Assemble guarded paste, clipboard and editing tools in dependency order.
 * RU: Собирает защищённую вставку, буфер обмена и инструменты правки в порядке зависимостей. */
#include "geany.h"
#include "insight-paste.h"
#include "insight-renumber-ranges.h"
#include "insight-buffer-transfer.h"
#include "keybindings.h"
#include "insight-languages.h"
#include "insight-completion.h"
#include "insight-help.h"
#include "document.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "main.h"
#include "insight-i18n.h"
#include "../insight/desktop/intelligent_paste.h"
#include "../insight/desktop/text_transfer.h"
#include "../insight/desktop/renumber.h"
#include "../insight/desktop/paragraph.h"
#include "../insight/desktop/line_insert.h"
#include "../insight/desktop/calculate.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define STATE "insight-paste-generation"
static gboolean loaded, enabled, always_select;
static guint64 preference_generation;
/* Zero preserves the historical caret-after-paste behavior. */
typedef enum { PASTE_END, PASTE_START, PASTE_SELECT, PASTE_POSITION_COUNT } PastePosition;
static PastePosition paste_position;
typedef struct { guint64 generation; } State;
typedef struct {
    ScintillaObject *sci;
    guint id;
    guint64 generation;
    gint anchor, caret;
    guint64 preferences;
    gpointer filetype;
    gboolean smart;
} Request;


#include "insight-paste/state.inc"
#include "insight-paste/insert.inc"
#include "insight-paste/preferences.inc"
#include "insight-paste/lines.inc"
#include "insight-paste/tools-word.inc"
#include "insight-paste/tools-characters.inc"
#include "insight-paste/tools-clipboard.inc"
#include "insight-paste/tools-files.inc"
#include "insight-paste/tools-calculate.inc"
#include "insight-paste/tools-paragraph.inc"
#include "insight-paste/tools-line-insert.inc"
#include "insight-paste/tools-renumber.inc"
#include "insight-paste/tools-menu.inc"
