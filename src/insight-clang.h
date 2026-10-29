/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_CLANG_UI_H
#define INSIGHT_CLANG_UI_H
#include "geany.h"
#include "editor.h"
#include "../insight/desktop/clang_backend.h"
void insight_clang_ui_init(GtkWidget *menu);
void insight_clang_ui_finalize(void);
InsightClangResult *insight_clang_ui_result(ScintillaObject *);
#endif
