/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Compile every production GDB fragment with real GTK, Geany and runtime types.
 * RU: Компилирует все производственные части GDB с настоящими типами GTK, Geany и runtime. */
#include <gtk/gtk.h>
#include "Scintilla.h"
#include "ScintillaWidget.h"
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "sciwrappers.h"
#include "utils.h"
#include "ui_utils.h"
#include "project.h"
#include "geanyobject.h"
#include "insight-workbench.h"
#include "insight/core/runtime.h"
#include "insight/desktop/glib_compat.h"
#include "insight/desktop/remote_io.h"
#include <string.h>
#define insight_tr(s) (s)
typedef struct {
    GPtrArray *watch_paths;
    GHashTable *watch_previous_values;
    gint watch_selected;
    InsightLuaWatchGraph *watch_graph;
} ScriptDebugWindow;
typedef struct {GtkWidget *window;} InsightDebugSession;
extern InsightDebugSession *active_debug_session(void);
extern void debug_set_text(GtkWidget *, const gchar *);
extern void debug_watch_compare(ScriptDebugWindow *);
extern void debug_watch_layout(ScriptDebugWindow *);
extern GtkWidget *debug_create_watch_tab(ScriptDebugWindow *);
extern GtkWidget *debug_create_view(GtkWidget *, gboolean);
extern void diff_source_open(const gchar *, guint);
void insight_panel_help_attach(GtkWidget *, const gchar *);
#include "insight-debuggers/gdb-remote-state.inc"
#include "insight-debuggers/gdb-profile-location-state.inc"
#include "insight-debuggers/disassembly/state.inc"
#include "insight-debuggers/gdb-session.inc"
#include "insight-debuggers/gdb-watches.inc"
#include "insight-debuggers/gdb-context.inc"
#include "insight-debuggers/gdb-backtraces.inc"
#include "insight-debuggers/gdb-source-markers.inc"
#include "insight-debuggers/gdb-lifecycle.inc"
#include "insight-debuggers/gdb-catchpoints-model.inc"
#include "insight-debuggers/gdb-catchpoints-ui.inc"
#include "insight-debuggers/gdb-breakpoints.inc"
#include "insight-debuggers/gdb-actions.inc"
#include "insight-debuggers/gdb-values-model.inc"
#include "insight-debuggers/gdb-values-ui.inc"
#include "insight-debuggers/gdb-data-actions.inc"
#include "insight-debuggers/gdb-memory-model.inc"
#include "insight-debuggers/gdb-memory-ui.inc"
#include "insight-debuggers/gdb-disassembly-model.inc"
#include "insight-debuggers/disassembly/rules.inc"
#include "insight-debuggers/disassembly/resolve.inc"
#include "insight-debuggers/disassembly/annotations.inc"
#include "insight-debuggers/disassembly/symbols.inc"
#include "insight-debuggers/disassembly/registers-ui.inc"
#include "insight-debuggers/disassembly/views.inc"
#include "insight-debuggers/disassembly/names-ui.inc"
#include "insight-debuggers/disassembly/pipeline.inc"
#include "insight-debuggers/disassembly/follow-ui.inc"
#include "insight-debuggers/disassembly/instruction-model.inc"
#include "insight-debuggers/disassembly/instruction-ui.inc"
#include "insight-debuggers/gdb-disassembly-ui.inc"
#include "insight-debuggers/gdb-remote-model.inc"
#include "insight-debuggers/gdb-remote-process.inc"
#include "insight-debuggers/gdb-attach-model.inc"
#include "insight-debuggers/gdb-attach-ui.inc"
#include "insight-debuggers/gdb-attach-interrupt.inc"
#include "insight-debuggers/gdb-remote-connection.inc"
#include "insight-debuggers/gdb-profile-catch-model.inc"
#include "insight-debuggers/gdb-profile-debug-model.inc"
#include "insight-debuggers/gdb-profile-locations-model.inc"
#include "insight-debuggers/gdb-profile-locations-snapshot.inc"
#include "insight-debuggers/gdb-profile-locations-storage.inc"
#include "insight-debuggers/gdb-profile-model.inc"
#include "insight-debuggers/gdb-profile-debug-ui.inc"
#include "insight-debuggers/gdb-profile-ui.inc"
#include "insight-debuggers/gdb-remote-ui.inc"
#include "insight-debuggers/gdb-window.inc"
