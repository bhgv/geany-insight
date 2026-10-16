/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-paste.h"
#include "insight-languages.h"
#include "insight-completion.h"
#include "document.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "../insight/desktop/intelligent_paste.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define STATE "insight-paste-generation"
static gboolean loaded, enabled;
typedef struct { guint64 generation; } State;
typedef struct {
    ScintillaObject *sci;
    guint id;
    guint64 generation;
    gint anchor, caret;
} Request;

static GeanyDocument *document_for_sci(ScintillaObject *sci)
{
    for (guint i = 0; documents_array && i < documents_array->len; i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (doc->is_valid && doc->editor->sci == sci)
            return doc;
    }
    return NULL;
}
static State *state(ScintillaObject *sci)
{
    State *s = g_object_get_data(G_OBJECT(sci), STATE);
    if (!s)
    {
        s = g_new0(State, 1);
        g_object_set_data_full(G_OBJECT(sci), STATE, s, g_free);
    }
    return s;
}
void insight_paste_notify(GeanyEditor *editor, SCNotification *nt)
{
    if (nt->nmhdr.code == SCN_MODIFIED &&
        (nt->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)))
        state(editor->sci)->generation++;
}
static gchar *config_path(void)
{
    return g_build_filename(app->configdir, "insight-paste.conf", NULL);
}
static void load(void)
{
    if (loaded)
        return;
    loaded = TRUE;
    GKeyFile *key = g_key_file_new();
    gchar *path = config_path();
    if (g_key_file_load_from_file(key, path, 0, NULL))
        enabled = g_key_file_get_boolean(key, "typing", "intelligent-paste", NULL);
    g_key_file_unref(key);
    g_free(path);
}
static gboolean eligible(ScintillaObject *sci)
{
    load();
    GeanyDocument *doc = document_for_sci(sci);
    if (!enabled || !doc || !doc->is_valid ||
        SEND(sci, SCI_GETREADONLY, 0, 0) || SEND(sci, SCI_GETSELECTIONS, 0, 0) != 1 ||
        sci_get_selection_mode(sci) != SC_SEL_STREAM || (SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) || SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0)))
        return FALSE;
    const InsightFiletypeOptions *options = insight_languages_editing_options(doc->editor);
    if (options && !options->allow_intelligent_paste)
        return FALSE;
    gint start = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    /* Partial selections retain native replacement semantics. */
    return start == end ||
        (start == sci_get_position_from_line(sci, sci_get_line_from_position(sci, start)) &&
         end == sci_get_position_from_line(sci, sci_get_line_from_position(sci, end)));
}

gboolean insight_paste_text(ScintillaObject *sci, const gchar *text)
{
    if (!eligible(sci))
        return FALSE;
    gint start = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    gint line = sci_get_line_from_position(sci, start);
    gint line_start = sci_get_position_from_line(sci, line);
    gint line_end = SEND(sci, SCI_GETLINEENDPOSITION, line, 0);
    guint indent = SEND(sci, SCI_GETLINEINDENTATION, line, 0);
    guint tab_width = SEND(sci, SCI_GETTABWIDTH, 0, 0);
    const gchar *eol = sci_get_eol_mode(sci) == SC_EOL_CRLF ? "\r\n" :
                      sci_get_eol_mode(sci) == SC_EOL_CR ? "\r" : "\n";
    gchar *body = insight_paste_reindent(text, indent, tab_width,
                                       SEND(sci, SCI_GETUSETABS, 0, 0), eol);
    if (!body)
        return FALSE;
    gboolean final_line = FALSE;
    if (start == end)
    {
        if (start == line_end && line_end > line_start)
        {
            if (line + 1 < sci_get_line_count(sci))
                start = sci_get_position_from_line(sci, line + 1);
            else
            {
                start = line_end;
                final_line = TRUE;
            }
        }
        else
            start = line_start;
        end = start;
    }
    gchar *insert = final_line ? g_strconcat(eol, body, NULL) : g_strdup(body);
    GeanyDocument *doc = document_for_sci(sci);
    insight_completion_cancel(doc->editor);
    SEND(sci, SCI_AUTOCCANCEL, 0, 0);
    sci_start_undo_action(sci);
    SEND(sci, SCI_SETSEL, start, end);
    sci_replace_sel(sci, insert);
    sci_end_undo_action(sci);
    g_free(body);
    g_free(insert);
    return TRUE;
}
static void clipboard_ready(GtkClipboard *clipboard, const gchar *text, gpointer data)
{
    (void)clipboard;
    Request *r = data;
    GeanyDocument *doc = document_for_sci(r->sci);
    gboolean unchanged = doc && doc->is_valid && doc->id == r->id && document_get_current() == doc &&
        state(r->sci)->generation == r->generation &&
        SEND(r->sci, SCI_GETANCHOR, 0, 0) == r->anchor &&
        sci_get_current_position(r->sci) == r->caret && eligible(r->sci);
    if (unchanged && !insight_paste_text(r->sci, text))
        SEND(r->sci, SCI_PASTE, 0, 0);
    else if (!unchanged)
        ui_set_statusbar(FALSE, "Paste cancelled: destination changed while reading the clipboard.");
    g_object_unref(r->sci);
    g_free(r);
}
static gboolean request_clipboard(gpointer data)
{
    Request *r = data;
    gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), clipboard_ready, r);
    return G_SOURCE_REMOVE;
}
gboolean insight_paste_clipboard(ScintillaObject *sci)
{
    if (!eligible(sci))
        return FALSE;
    GeanyDocument *doc = document_for_sci(sci);
    Request *r = g_new0(Request, 1);
    r->sci = g_object_ref(sci);
    r->id = doc->id;
    r->generation = state(sci)->generation;
    r->anchor = SEND(sci, SCI_GETANCHOR, 0, 0);
    r->caret = sci_get_current_position(sci);
    g_idle_add(request_clipboard, r);
    return TRUE;
}
static void toggle(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    gboolean next = gtk_check_menu_item_get_active(item);
    GKeyFile *key = g_key_file_new();
    gchar *path = config_path();
    g_key_file_load_from_file(key, path, 0, NULL);
    g_key_file_set_boolean(key, "typing", "intelligent-paste", next);
    gsize length;
    gchar *text = g_key_file_to_data(key, &length, NULL);
    GError *error = NULL;
    if (g_file_set_contents(path, text, length, &error))
        enabled = next;
    else
    {
        g_signal_handlers_block_by_func(item, toggle, NULL);
        gtk_check_menu_item_set_active(item, enabled);
        g_signal_handlers_unblock_by_func(item, toggle, NULL);
        ui_set_statusbar(TRUE, "Cannot save intelligent paste: %s", error->message);
        g_clear_error(&error);
    }
    g_free(text);
    g_free(path);
    g_key_file_unref(key);
}
void insight_paste_menu(GtkWidget *menu)
{
    load();
    GtkWidget *item = gtk_check_menu_item_new_with_label("Intelligent paste");
    gtk_widget_set_name(item, "insight-intelligent-paste-option");
    gtk_widget_set_tooltip_text(item, "Place whole lines above or below the current line and match its indentation");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), enabled);
    g_signal_connect(item, "toggled", G_CALLBACK(toggle), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
}
