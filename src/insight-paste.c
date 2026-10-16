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
static gboolean allowed(ScintillaObject *sci)
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
    return TRUE;
}
static gboolean eligible(ScintillaObject *sci)
{
    if (!allowed(sci))
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
    {
        if (g_getenv("GEANY_PASTE_DEBUG"))
            g_printerr("paste cancelled: id=%u/%u generation=%"G_GUINT64_FORMAT"/%"G_GUINT64_FORMAT" anchor=%d/%ld caret=%d/%d eligible=%d\n",
                r->id, doc ? doc->id : 0, r->generation, state(r->sci)->generation,
                r->anchor, (long)SEND(r->sci, SCI_GETANCHOR, 0, 0), r->caret,
                sci_get_current_position(r->sci), eligible(r->sci));
        ui_set_statusbar(FALSE, "Paste cancelled: destination changed while reading the clipboard.");
    }
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

/* Move with Scintilla's native operation, retaining its line/marker handling.
 * Reindent only after bounded preparation, and keep the complete move in one Undo. */
gboolean insight_paste_move_lines(ScintillaObject *sci, gint direction, guint count)
{
    if (!allowed(sci) || (direction != -1 && direction != 1) || !count || count > 5)
        return FALSE;
    gint start = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    gint first = sci_get_line_from_position(sci, start);
    gint last = sci_get_line_from_position(sci, end);
    if (end > start && end == sci_get_position_from_line(sci, last))
        last--;
    guint lines = last - first + 1;
    if (lines > 10000)
        return FALSE;
    gint total = sci_get_line_count(sci);
    count = MIN(count, (guint)(direction < 0 ? first : total - last - 1));
    if (!count)
        return TRUE;
    gint begin = sci_get_position_from_line(sci, first);
    gint finish = last + 1 < total ? sci_get_position_from_line(sci, last + 1) : sci_get_length(sci);
    if (finish <= begin || finish - begin > 1024 * 1024)
        return FALSE;
    gchar *original = sci_get_contents_range(sci, begin, finish);
    const gchar *eol = sci_get_eol_mode(sci) == SC_EOL_CRLF ? "\r\n" :
                      sci_get_eol_mode(sci) == SC_EOL_CR ? "\r" : "\n";
    gsize length = strlen(original);
    gchar *whole = length && (original[length-1] == '\r' || original[length-1] == '\n') ?
                   g_strdup(original) : g_strconcat(original, eol, NULL);
    g_free(original);
    guint tab = SEND(sci, SCI_GETTABWIDTH, 0, 0);
    guint target = SEND(sci, SCI_GETLINEINDENTATION,
                        direction < 0 ? first - (gint)count : last + (gint)count, 0);
    gchar *prepared = insight_paste_reindent(whole, target, tab,
                                           SEND(sci, SCI_GETUSETABS, 0, 0), eol);
    g_free(whole);
    if (!prepared)
        return FALSE;
    GArray *indents = g_array_new(FALSE, FALSE, sizeof(guint));
    const gchar *p = prepared;
    while (*p)
    {
        guint columns = 0;
        while (*p == ' ' || *p == '\t')
        {
            columns += *p == '\t' ? tab - columns % tab : 1;
            p++;
        }
        g_array_append_val(indents, columns);
        while (*p && *p != '\r' && *p != '\n') p++;
        if (*p == '\r' && p[1] == '\n') p += 2;
        else if (*p) p++;
    }
    g_free(prepared);
    if (indents->len != lines)
    {
        g_array_free(indents, TRUE);
        return FALSE;
    }
    gboolean selection = start != end;
    gboolean forward = SEND(sci, SCI_GETANCHOR, 0, 0) <= sci_get_current_position(sci);
    gint caret = sci_get_current_position(sci);
    gint caret_line = sci_get_line_from_position(sci, caret);
    gint body_offset = MAX(0, caret - SEND(sci, SCI_GETLINEINDENTPOSITION, caret_line, 0));
    GeanyDocument *doc = document_for_sci(sci);
    insight_completion_cancel(doc->editor);
    SEND(sci, SCI_AUTOCCANCEL, 0, 0);
    sci_start_undo_action(sci);
    guint moved = 0;
    for (; moved < count; moved++)
    {
        guint64 generation = state(sci)->generation;
        SEND(sci, direction < 0 ? SCI_MOVESELECTEDLINESUP : SCI_MOVESELECTEDLINESDOWN, 0, 0);
        if (state(sci)->generation == generation)
            break;
    }
    /* Avoid guessing a destination if native movement stopped at a boundary. */
    if (moved == count)
    {
        first += direction * (gint)moved;
        for (guint i = 0; i < lines; i++)
            SEND(sci, SCI_SETLINEINDENTATION, first + i, g_array_index(indents, guint, i));
        if (selection)
        {
            gint a = sci_get_position_from_line(sci, first);
            gint b = first + (gint)lines < sci_get_line_count(sci) ?
                     sci_get_position_from_line(sci, first + lines) : sci_get_length(sci);
            SEND(sci, SCI_SETSEL, forward ? a : b, forward ? b : a);
        }
        else
        {
            caret_line += direction * (gint)moved;
            gint pos = MIN(SEND(sci, SCI_GETLINEENDPOSITION, caret_line, 0),
                           SEND(sci, SCI_GETLINEINDENTPOSITION, caret_line, 0) + body_offset);
            SEND(sci, SCI_SETSEL, pos, pos);
        }
    }
    sci_end_undo_action(sci);
    SEND(sci, SCI_SCROLLCARET, 0, 0);
    g_array_free(indents, TRUE);
    return TRUE;
}
/* GTK owns a copy of the validated text before any document edit occurs. */
gboolean insight_line_clipboard(ScintillaObject *sci, InsightLineClipboard command)
{
    GeanyDocument *doc = document_for_sci(sci);
    if (!doc || SEND(sci, SCI_GETSELECTIONS, 0, 0) != 1 ||
        sci_get_selection_mode(sci) != SC_SEL_STREAM ||
        SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) ||
        SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0))
        return FALSE;
    gboolean cut = command != INSIGHT_COPY_LINE && command != INSIGHT_COPY_LINE_RIGHT;
    if (cut && SEND(sci, SCI_GETREADONLY, 0, 0))
        return TRUE;
    gint caret = sci_get_current_position(sci);
    gint line = sci_get_line_from_position(sci, caret);
    gint line_start = sci_get_position_from_line(sci, line);
    gint line_end = SEND(sci, SCI_GETLINEENDPOSITION, line, 0);
    gint length = sci_get_length(sci), total = sci_get_line_count(sci);
    gint begin = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    switch (command)
    {
        case INSIGHT_COPY_LINE:
        {
            gint first = sci_get_line_from_position(sci, begin);
            gint last = sci_get_line_from_position(sci, end);
            gboolean selected = begin != end;
            if (selected && end == sci_get_position_from_line(sci, last))
                last--;
            gint a = sci_get_position_from_line(sci, first);
            gint b = last + 1 < total ? sci_get_position_from_line(sci, last + 1) : length;
            /* Already whole lines: the next Copy Line adds the following line. */
            if (selected && begin == a && end == b && last + 1 < total)
                b = last + 2 < total ? sci_get_position_from_line(sci, last + 2) : length;
            begin = a;end = b;
            break;
        }
        case INSIGHT_CUT_LINE:
            begin = line_start;
            end = line + 1 < total ? sci_get_position_from_line(sci, line + 1) : length;
            break;
        case INSIGHT_COPY_LINE_RIGHT:
        case INSIGHT_CUT_LINE_RIGHT:
            begin = MIN(caret, line_end);end = line_end;
            break;
        case INSIGHT_CUT_LINE_LEFT:
            begin = line_start;end = MIN(caret, line_end);
            break;
        case INSIGHT_CUT_SELECTION_OR_PASTE:
            if (begin == end)
            {
                sci_paste(sci);
                return TRUE;
            }
            break;
        default:
            return FALSE;
    }
    /* Empty boundaries leave the previous clipboard intact and create no Undo. */
    if (end <= begin)
        return TRUE;
    if (end - begin > 1024 * 1024)
    {
        ui_set_statusbar(TRUE, "Line clipboard operation exceeds 1 MiB.");
        return TRUE;
    }
    gchar *text = sci_get_contents_range(sci, begin, end);
    if (!text || strlen(text) != (gsize)(end - begin) || !g_utf8_validate(text, end - begin, NULL))
    {
        g_free(text);
        ui_set_statusbar(TRUE, "Line clipboard operation requires complete UTF-8 text without NUL bytes.");
        return TRUE;
    }
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD), text, end - begin);
    g_free(text);
    if (cut)
    {
        insight_completion_cancel(doc->editor);
        SEND(sci, SCI_AUTOCCANCEL, 0, 0);
        sci_start_undo_action(sci);
        SEND(sci, SCI_SETSEL, begin, end);
        sci_replace_sel(sci, "");
        sci_end_undo_action(sci);
    }
    else if (command == INSIGHT_COPY_LINE)
        SEND(sci, SCI_SETSEL, begin, end);
    return TRUE;
}
static void clipboard_command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (doc && doc->is_valid)
    {
        insight_line_clipboard(doc->editor->sci, GPOINTER_TO_INT(data));
        gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
    }
}
static void line_command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (!doc || !doc->is_valid)
        return;
    ScintillaObject *sci = doc->editor->sci;
    if (SEND(sci, SCI_GETREADONLY, 0, 0) || SEND(sci, SCI_GETSELECTIONS, 0, 0) != 1 ||
        sci_get_selection_mode(sci) != SC_SEL_STREAM ||
        SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) ||
        SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0))
        return;
    gint command = GPOINTER_TO_INT(data);
    if (!command)
    {
        gint line = sci_get_line_from_position(sci, sci_get_current_position(sci));
        gint pos = sci_get_position_from_line(sci, line);
        SEND(sci, SCI_SETSEL, pos, pos);
        sci_paste(sci);
    }
    else
    {
        gint direction = command < 0 ? -1 : 1;
        guint count = ABS(command);
        if (!insight_paste_move_lines(sci, direction, count))
        {
            sci_start_undo_action(sci);
            for (guint i = 0; i < count; i++)
                SEND(sci, direction < 0 ? SCI_MOVESELECTEDLINESUP : SCI_MOVESELECTEDLINESDOWN, 0, 0);
            sci_end_undo_action(sci);
        }
    }
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}
void insight_paste_line_menu(GtkWidget *menu)
{
    g_return_if_fail(GTK_IS_MENU_SHELL(menu));
    GtkWidget *parent = gtk_menu_item_new_with_label("Line operations");
    gtk_widget_set_name(parent, "insight-line-operations");
    GtkWidget *sub = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
    const gchar *labels[] = {"Paste Line", "Drag Line Up", "Drag Line Down", "Drag Line Up More", "Drag Line Down More"};
    const gchar *ids[] = {"paste-line", "drag-line-up", "drag-line-down", "drag-line-up-more", "drag-line-down-more"};
    const gint commands[] = {0, -1, 1, -5, 5};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);
        gchar *name = g_strconcat("insight-", ids[i], NULL);
        gtk_widget_set_name(entry, name);
        g_free(name);
        if (i > 2) gtk_widget_set_tooltip_text(entry, "Move up to five lines, stopping at the document boundary");
        g_signal_connect(entry, "activate", G_CALLBACK(line_command), GINT_TO_POINTER(commands[i]));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
    const gchar *clipboard_labels[] = {"Copy Line", "Cut Line", "Copy Line Right", "Cut Line Left", "Cut Line Right", "Cut Selection or Paste"};
    const gchar *clipboard_ids[] = {"copy-line", "cut-line", "copy-line-right", "cut-line-left", "cut-line-right", "cut-selection-or-paste"};
    for (guint i = 0; i < G_N_ELEMENTS(clipboard_labels); i++)
    {
        GtkWidget *entry = gtk_menu_item_new_with_label(clipboard_labels[i]);
        gchar *name = g_strconcat("insight-", clipboard_ids[i], NULL);
        gtk_widget_set_name(entry, name);g_free(name);
        g_signal_connect(entry, "activate", G_CALLBACK(clipboard_command), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);
    gtk_widget_show_all(parent);
}
