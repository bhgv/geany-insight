/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-paste.h"
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
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define STATE "insight-paste-generation"
static gboolean loaded, enabled;
/* Zero preserves the historical caret-after-paste behavior. */
typedef enum { PASTE_END, PASTE_START, PASTE_SELECT, PASTE_POSITION_COUNT } PastePosition;
static PastePosition paste_position;
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
    {
        enabled = g_key_file_get_boolean(key, "typing", "intelligent-paste", NULL);
        gint position = g_key_file_get_integer(key, "typing", "paste-position", NULL);
        if (position >= 0 && position < PASTE_POSITION_COUNT)
            paste_position = position;
    }
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
    /* The separator added at an unterminated EOF belongs to the old line,
     * not to the pasted block. Byte offsets retain UTF-8 and CRLF boundaries. */
    gint pasted_start = start + (final_line ? strlen(eol) : 0);
    gint pasted_end = start + strlen(insert);
    if (paste_position == PASTE_START)
        SEND(sci, SCI_SETSEL, pasted_start, pasted_start);
    else if (paste_position == PASTE_SELECT)
        SEND(sci, SCI_SETSEL, pasted_start, pasted_end);
    SEND(sci, SCI_SCROLLCARET, 0, 0);
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
static void position_toggled(GtkCheckMenuItem *item, gpointer data)
{
    if (!gtk_check_menu_item_get_active(item)) return;
    PastePosition next = GPOINTER_TO_INT(data);
    if (next == paste_position) return;
    GKeyFile *key = g_key_file_new();
    gchar *path = config_path();
    g_key_file_load_from_file(key, path, 0, NULL);
    g_key_file_set_integer(key, "typing", "paste-position", next);
    gsize length;
    gchar *text = g_key_file_to_data(key, &length, NULL);
    GError *error = NULL;
    if (g_file_set_contents(path, text, length, &error))
        paste_position = next;
    else
    {
        GSList *group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
        /* Restore the previous radio choice without writing the setting again. */
        for (GSList *p = group; p; p = p->next)
            g_signal_handlers_block_by_func(p->data, position_toggled,
                g_object_get_data(G_OBJECT(p->data), "paste-position"));
        for (GSList *p = group; p; p = p->next)
            gtk_check_menu_item_set_active(p->data,
                GPOINTER_TO_INT(g_object_get_data(G_OBJECT(p->data), "paste-position")) == (gint)paste_position);
        for (GSList *p = group; p; p = p->next)
            g_signal_handlers_unblock_by_func(p->data, position_toggled,
                g_object_get_data(G_OBJECT(p->data), "paste-position"));
        ui_set_statusbar(TRUE, "Cannot save paste position: %s", error->message);
        g_clear_error(&error);
    }
    g_free(text);g_free(path);g_key_file_unref(key);
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
    GtkWidget *parent = gtk_menu_item_new_with_label("After intelligent paste");
    gtk_widget_set_name(parent, "insight-paste-position");
    gtk_widget_set_tooltip_text(parent, "Cursor or selection after pasting whole lines with intelligent paste");
    GtkWidget *submenu = gtk_menu_new();
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), submenu);
    const gchar *labels[] = {"Cursor at end of pasted text", "Cursor at start of pasted text", "Select pasted text"};
    GSList *group = NULL;
    for (gint i = 0; i < PASTE_POSITION_COUNT; i++)
    {
        GtkWidget *choice = gtk_radio_menu_item_new_with_label(group, labels[i]);
        group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(choice));
        gchar *name = g_strdup_printf("insight-paste-position-%d", i);
        gtk_widget_set_name(choice, name);g_free(name);
        g_object_set_data(G_OBJECT(choice), "paste-position", GINT_TO_POINTER(i));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(choice), i == (gint)paste_position);
        g_signal_connect(choice, "toggled", G_CALLBACK(position_toggled), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(submenu), choice);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);
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

/* Edit tools share the paste generation counter, but never retain a destroyed
 * Scintilla widget: resolve the document id before checking the target. */
static gboolean tools_capture(Request *request)
{
 GeanyDocument *doc=document_get_current();if(!doc || doc->readonly) return FALSE;
 ScintillaObject *sci=doc->editor->sci;
 if(SEND(sci,SCI_GETREADONLY,0,0) || SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || sci_get_selection_mode(sci)!=SC_SEL_STREAM ||
    SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)) return FALSE;
 request->sci=sci;request->id=doc->id;request->generation=state(sci)->generation;
 request->anchor=SEND(sci,SCI_GETANCHOR,0,0);request->caret=sci_get_current_position(sci);return TRUE;
}
static gboolean tools_insert(Request *request,const gchar *text)
{
 GeanyDocument *doc=document_find_by_id(request->id);
 if(!doc || doc!=document_get_current() || doc->editor->sci!=request->sci) return FALSE;
 Request current={0};if(!tools_capture(&current) || current.generation!=request->generation || current.anchor!=request->anchor || current.caret!=request->caret) return FALSE;
 insight_completion_cancel(doc->editor);SEND(request->sci,SCI_AUTOCCANCEL,0,0);
 sci_start_undo_action(request->sci);sci_replace_sel(request->sci,text);sci_end_undo_action(request->sci);
 gtk_widget_grab_focus(GTK_WIDGET(request->sci));return TRUE;
}
static void insert_guid(GtkMenuItem *item,gpointer data)
{
 (void)item;(void)data;Request request={0};if(!tools_capture(&request)) return;
 gchar *guid=g_uuid_string_random();tools_insert(&request,guid);g_free(guid);
}
/* Unicode scalar values; no NUL (the editor's insertion API is NUL-terminated).
 * Auto deliberately uses decimal for a plain leading zero, avoiding octal surprises. */
static gboolean character_code(const gchar *input,guint radix,gunichar *result)
{
 gchar *text=g_strdup(input);g_strstrip(text);const gchar *p=text;guint base=radix;
 if(!base) {
  base=10;
  if(g_ascii_strncasecmp(p,"U+",2)==0) { base=16;p+=2; }
  else if(g_ascii_strncasecmp(p,"0x",2)==0) { base=16;p+=2; }
  else if(g_ascii_strncasecmp(p,"0o",2)==0) { base=8;p+=2; }
  else if(g_ascii_strncasecmp(p,"0b",2)==0) { base=2;p+=2; }
 } else if((base==16 && (!g_ascii_strncasecmp(p,"0x",2) || !g_ascii_strncasecmp(p,"U+",2))) ||
           (base==8 && !g_ascii_strncasecmp(p,"0o",2)) || (base==2 && !g_ascii_strncasecmp(p,"0b",2))) p+=2;
 guint32 value=0;gboolean valid=*p!=0;
 for(;valid && *p;p++) {
  gint digit=g_ascii_xdigit_value(*p);
  if(digit<0 || (guint)digit>=base || value>(0x10ffffu-(guint)digit)/base) valid=FALSE;
  else value=value*base+digit;
 }
 valid=valid && value && g_unichar_validate(value);if(valid) *result=value;g_free(text);return valid;
}
typedef struct { GtkWidget *entry,*radix,*preview,*dialog; } CharacterDialog;
static gboolean character_value(CharacterDialog *dialog,gunichar *value)
{
 const guint bases[]={0,10,16,8,2};gint index=gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->radix));
 return index>=0 && index<(gint)G_N_ELEMENTS(bases) && character_code(gtk_entry_get_text(GTK_ENTRY(dialog->entry)),bases[index],value);
}
static void character_changed(GtkWidget *widget,gpointer data)
{
 (void)widget;CharacterDialog *dialog=data;gunichar value;gboolean valid=character_value(dialog,&value);
 gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog->dialog),GTK_RESPONSE_ACCEPT,valid);
 gchar *preview=NULL;
 if(valid) { gchar utf8[7]={0};g_unichar_to_utf8(value,utf8);preview=g_unichar_isprint(value) ? g_strdup_printf("%s    U+%04X",utf8,value) : g_strdup_printf("U+%04X",value); }
 gtk_label_set_text(GTK_LABEL(dialog->preview),preview ? preview : insight_tr("Enter a character code from U+0001 to U+10FFFF, excluding surrogates."));g_free(preview);
}
static void insert_character(GtkMenuItem *item,gpointer data)
{
 (void)item;(void)data;Request request={0};if(!tools_capture(&request)) return;
 CharacterDialog dialog={0};dialog.dialog=gtk_dialog_new_with_buttons(insight_tr("Insert ASCII / Unicode"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Insert"),GTK_RESPONSE_ACCEPT,NULL);
 g_object_ref_sink(dialog.dialog);gtk_widget_set_name(dialog.dialog,"insight-insert-character-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog.dialog),480,-1);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog.dialog));dialog.radix=gtk_combo_box_text_new();gtk_widget_set_name(dialog.radix,"insight-character-radix");const gchar *bases[]={"Auto","Decimal","Hexadecimal","Octal","Binary"};for(guint i=0;i<G_N_ELEMENTS(bases);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog.radix),insight_tr(bases[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(dialog.radix),0);
 dialog.entry=gtk_entry_new();gtk_widget_set_name(dialog.entry,"insight-character-code");gtk_entry_set_placeholder_text(GTK_ENTRY(dialog.entry),"65 / 0x41 / U+03B1 / 0o101 / 0b1000001");gtk_entry_set_max_length(GTK_ENTRY(dialog.entry),32);gtk_entry_set_activates_default(GTK_ENTRY(dialog.entry),TRUE);
 dialog.preview=gtk_label_new("");gtk_widget_set_name(dialog.preview,"insight-character-preview");gtk_label_set_line_wrap(GTK_LABEL(dialog.preview),TRUE);
 GtkWidget *grid=gtk_grid_new(),*radix_label=gtk_label_new(insight_tr("Radix")),*code_label=gtk_label_new(insight_tr("Character code"));gtk_grid_set_row_spacing(GTK_GRID(grid),8);gtk_grid_set_column_spacing(GTK_GRID(grid),8);gtk_container_set_border_width(GTK_CONTAINER(grid),8);gtk_label_set_xalign(GTK_LABEL(radix_label),0);gtk_label_set_xalign(GTK_LABEL(code_label),0);gtk_widget_set_hexpand(dialog.entry,TRUE);
 gtk_grid_attach(GTK_GRID(grid),radix_label,0,0,1,1);gtk_grid_attach(GTK_GRID(grid),dialog.radix,1,0,1,1);gtk_grid_attach(GTK_GRID(grid),code_label,0,1,1,1);gtk_grid_attach(GTK_GRID(grid),dialog.entry,1,1,1,1);gtk_grid_attach(GTK_GRID(grid),dialog.preview,0,2,2,1);gtk_box_pack_start(GTK_BOX(box),grid,FALSE,FALSE,0);
 g_signal_connect(dialog.entry,"changed",G_CALLBACK(character_changed),&dialog);g_signal_connect(dialog.radix,"changed",G_CALLBACK(character_changed),&dialog);
 gtk_dialog_set_default_response(GTK_DIALOG(dialog.dialog),GTK_RESPONSE_ACCEPT);character_changed(NULL,&dialog);gtk_widget_show_all(dialog.dialog);gtk_widget_grab_focus(dialog.entry);
 for(;;) {
  if(gtk_dialog_run(GTK_DIALOG(dialog.dialog))!=GTK_RESPONSE_ACCEPT) break;
  gunichar value;if(!character_value(&dialog,&value)) continue;
  gchar utf8[7]={0};g_unichar_to_utf8(value,utf8);
  if(!tools_insert(&request,utf8)) ui_set_statusbar(TRUE,"Insertion cancelled because the document or selection changed");
  break;
 }
 gtk_widget_destroy(dialog.dialog);g_object_unref(dialog.dialog);
}
typedef struct { GtkWidget *window,*text,*status;guint refs;guint64 serial;gboolean closed; } ClipboardView;
typedef struct { ClipboardView *view;guint64 serial; } ClipboardRead;
static ClipboardView *clipboard_view;
static void clipboard_unref(ClipboardView *view) { if(!--view->refs) g_free(view); }
static void clipboard_received(GtkClipboard *clipboard,const gchar *text,gpointer data)
{
 (void)clipboard;ClipboardRead *read=data;ClipboardView *view=read->view;
 if(!view->closed && read->serial==view->serial) {
  const gchar *status=NULL;
  if(!text) status=insight_tr("Clipboard has no text.");
  else if(strlen(text)>256*1024) status=insight_tr("Clipboard text exceeds the 256 KiB display limit.");
  else if(!g_utf8_validate(text,-1,NULL)) status=insight_tr("Clipboard text is not valid UTF-8.");
  else if(!*text) status=insight_tr("Clipboard text is empty.");
  gtk_label_set_text(GTK_LABEL(view->text),status ? "" : text);gtk_label_set_text(GTK_LABEL(view->status),status ? status : insight_tr("Clipboard text snapshot. Refresh to read again."));
 }
 clipboard_unref(view);g_free(read);
}
static void clipboard_refresh(GtkWidget *widget,gpointer data)
{
 (void)widget;ClipboardView *view=data;view->serial++;
 gtk_label_set_text(GTK_LABEL(view->text),"");gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Reading clipboard…"));
 ClipboardRead *read=g_new(ClipboardRead,1);read->view=view;read->serial=view->serial;view->refs++;
 gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),clipboard_received,read);
}
static void clipboard_destroy(GtkWidget *widget,gpointer data)
{
 (void)widget;ClipboardView *view=data;view->closed=TRUE;view->serial++;if(clipboard_view==view) clipboard_view=NULL;clipboard_unref(view);
}
static void clipboard_close(GtkWidget *widget,gpointer data) { (void)widget;gtk_widget_destroy(((ClipboardView*)data)->window); }
static void show_clipboard(GtkMenuItem *item,gpointer data)
{
 (void)item;(void)data;
 if(clipboard_view) { clipboard_refresh(NULL,clipboard_view);gtk_window_present(GTK_WINDOW(clipboard_view->window));return; }
 ClipboardView *view=g_new0(ClipboardView,1);view->refs=1;clipboard_view=view;
 view->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(view->window),insight_tr("Clipboard"));gtk_widget_set_name(view->window,"insight-clipboard-window");gtk_window_set_default_size(GTK_WINDOW(view->window),720,360);gtk_window_set_transient_for(GTK_WINDOW(view->window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(view->window),TRUE);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*scroll=gtk_scrolled_window_new(NULL,NULL),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);gtk_container_add(GTK_CONTAINER(view->window),box);
 view->text=gtk_label_new("");gtk_widget_set_name(view->text,"insight-clipboard-text");g_object_set_data(G_OBJECT(view->text),"insight-no-i18n",GINT_TO_POINTER(1));gtk_label_set_xalign(GTK_LABEL(view->text),0);gtk_label_set_yalign(GTK_LABEL(view->text),0);gtk_label_set_selectable(GTK_LABEL(view->text),FALSE);
 PangoAttrList *attributes=pango_attr_list_new();pango_attr_list_insert(attributes,pango_attr_family_new("monospace"));gtk_label_set_attributes(GTK_LABEL(view->text),attributes);pango_attr_list_unref(attributes);
 gtk_container_add(GTK_CONTAINER(scroll),view->text);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 view->status=gtk_label_new("");gtk_widget_set_name(view->status,"insight-clipboard-status");gtk_label_set_line_wrap(GTK_LABEL(view->status),TRUE);gtk_box_pack_start(GTK_BOX(box),view->status,FALSE,FALSE,0);
 GtkWidget *refresh=gtk_button_new_with_label(insight_tr("Refresh")),*close=gtk_button_new_with_label(insight_tr("Close"));gtk_widget_set_name(refresh,"insight-clipboard-refresh");gtk_box_pack_end(GTK_BOX(bar),close,FALSE,FALSE,0);gtk_box_pack_end(GTK_BOX(bar),refresh,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
 g_signal_connect(refresh,"clicked",G_CALLBACK(clipboard_refresh),view);g_signal_connect(close,"clicked",G_CALLBACK(clipboard_close),view);g_signal_connect(view->window,"destroy",G_CALLBACK(clipboard_destroy),view);gtk_widget_show_all(view->window);clipboard_refresh(NULL,view);
}
static gboolean file_request_valid(Request *request,gboolean selection)
{
 GeanyDocument *doc=document_find_by_id(request->id);
 return doc && doc==document_get_current() && doc->editor->sci==request->sci && state(request->sci)->generation==request->generation &&
  (!selection || (SEND(request->sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(request->sci)==SC_SEL_STREAM &&
   !SEND(request->sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(request->sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) &&
   SEND(request->sci,SCI_GETANCHOR,0,0)==request->anchor && sci_get_current_position(request->sci)==request->caret));
}
static gboolean destination_open(const gchar *path)
{
 gchar *utf8=g_filename_to_utf8(path,-1,NULL,NULL,NULL);if(!utf8) return TRUE;
 gboolean opened=document_find_by_filename(utf8)!=NULL;g_free(utf8);if(opened) return TRUE;
 GFile *destination=g_file_new_for_path(path);GFileInfo *target=g_file_query_info(destination,G_FILE_ATTRIBUTE_ID_FILE,G_FILE_QUERY_INFO_NONE,NULL,NULL);g_object_unref(destination);
 const gchar *id=target ? g_file_info_get_attribute_string(target,G_FILE_ATTRIBUTE_ID_FILE) : NULL;
 for(guint i=0;id && documents_array && i<documents_array->len && !opened;i++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!doc->is_valid || !doc->file_name) continue;
  gchar *native=g_filename_from_utf8(doc->file_name,-1,NULL,NULL,NULL);if(!native) continue;
  GFile *file=g_file_new_for_path(native);GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_ID_FILE,G_FILE_QUERY_INFO_NONE,NULL,NULL);g_free(native);g_object_unref(file);
  if(info) { opened=!g_strcmp0(id,g_file_info_get_attribute_string(info,G_FILE_ATTRIBUTE_ID_FILE));g_object_unref(info); }
 }
 g_clear_object(&target);return opened;
}
static void insert_file(GtkMenuItem *item,gpointer data)
{
 (void)item;(void)data;Request request={0};if(!tools_capture(&request)) return;
 GtkWidget *dialog=gtk_file_chooser_dialog_new(insight_tr("Insert File"),GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_OPEN,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Insert"),GTK_RESPONSE_ACCEPT,NULL);
 g_object_ref_sink(dialog);gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_widget_set_name(dialog,"insight-insert-file-dialog");gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog),TRUE);
 gchar *path=NULL;if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
 gtk_widget_destroy(dialog);g_object_unref(dialog);
 if(path) {
  GError *error=NULL;gsize length=0;gchar *text=insight_text_file_read(path,&length,&error);
  if(!text) ui_set_statusbar(TRUE,"%s",error ? insight_tr(error->message) : insight_tr("Choose a local filename."));
  else if(!tools_insert(&request,text)) ui_set_statusbar(TRUE,"Insertion cancelled because the document or selection changed");
  g_clear_error(&error);g_free(text);g_free(path);
 }
}
static void export_text(GtkMenuItem *item,gpointer data)
{
 (void)item;gboolean selection=GPOINTER_TO_INT(data);GeanyDocument *doc=document_get_current();if(!doc) return;
 ScintillaObject *sci=doc->editor->sci;gint begin=selection ? sci_get_selection_start(sci) : 0,end=selection ? sci_get_selection_end(sci) : sci_get_length(sci);
 if(selection && (begin==end || SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || sci_get_selection_mode(sci)!=SC_SEL_STREAM || SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0))) return;
 if(end-begin>INSIGHT_TEXT_TRANSFER_LIMIT) { ui_set_statusbar(TRUE,"Text transfer is limited to 8 MiB.");return; }
 gchar *text=sci_get_contents_range(sci,begin,end);gsize length=end-begin;
 if(strlen(text)!=length || !g_utf8_validate(text,length,NULL)) { ui_set_statusbar(TRUE,"Text transfer requires UTF-8 without NUL bytes.");g_free(text);return; }
 Request request={0};request.id=doc->id;request.sci=sci;request.generation=state(sci)->generation;request.anchor=SEND(sci,SCI_GETANCHOR,0,0);request.caret=sci_get_current_position(sci);
 GtkWidget *dialog=gtk_file_chooser_dialog_new(insight_tr(selection ? "Save Selection" : "Save a Copy"),GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_SAVE,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Save"),GTK_RESPONSE_ACCEPT,NULL);
 g_object_ref_sink(dialog);gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_widget_set_name(dialog,"insight-export-text-dialog");gtk_file_chooser_set_local_only(GTK_FILE_CHOOSER(dialog),TRUE);gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),selection ? "selection.txt" : "copy.txt");
 GtkWidget *note=gtk_label_new(insight_tr("Export UTF-8 text without changing the original document. Open destinations are protected."));gtk_label_set_line_wrap(GTK_LABEL(note),TRUE);gtk_file_chooser_set_extra_widget(GTK_FILE_CHOOSER(dialog),note);gtk_widget_show(note);
 gchar *path=NULL;if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));gtk_widget_destroy(dialog);g_object_unref(dialog);
 if(path) {
  GError *error=NULL;gboolean saved=FALSE;
  if(!file_request_valid(&request,selection)) ui_set_statusbar(TRUE,"Export cancelled because the source document or selection changed");
  else if(destination_open(path)) ui_set_statusbar(TRUE,"Close the destination document before exporting to this path");
  else if(!(saved=insight_text_file_write(path,text,length,&error))) ui_set_statusbar(TRUE,"%s",error ? insight_tr(error->message) : insight_tr("Choose a local filename."));
  if(saved) document_open_file(path,FALSE,NULL,"UTF-8");
  g_clear_error(&error);g_free(path);
 }
 g_free(text);
}
typedef struct { GtkWidget *mode,*margin,*preview,*status;const gchar *text,*eol;gint anchor,caret,tab,start,end; } ParagraphView;
static gchar *paragraph_preview(ParagraphView *view)
{
 GError *error=NULL;gchar *result=insight_paragraph(view->text,view->anchor,view->caret,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode))==1,gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(view->margin)),view->tab,view->eol,&view->start,&view->end,&error);
 gchar *status=result ? insight_i18n_printf("Scope: bytes %d–%d. Preview shows at most 8000 bytes.",view->start,view->end) : g_strdup(insight_tr(error->message));gtk_label_set_text(GTK_LABEL(view->status),status);g_free(status);g_clear_error(&error);
 gsize length=result ? MIN(strlen(result),8000) : 0;while(length && !g_utf8_validate(result,length,NULL)) length--;
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),result ? result : "",length);return result;
}
static void paragraph_changed(GtkWidget *widget,gpointer data)
{
 (void)widget;ParagraphView *view=data;gtk_widget_set_sensitive(view->margin,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode))==0);
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),"",0);gtk_label_set_text(GTK_LABEL(view->status),"Press Preview to inspect the result before applying");
}
static gboolean paragraph_apply(Request *request,gint start,gint end,const gchar *source,const gchar *result)
{
 if(!file_request_valid(request,TRUE) || !tools_capture(&(Request){0})) { ui_set_statusbar(TRUE,"Paragraph operation cancelled because the document or selection changed");return FALSE; }
 if(strlen(result)==(gsize)(end-start) && !strncmp(source+start,result,end-start)) return TRUE;
 insight_completion_cancel(document_get_current()->editor);SEND(request->sci,SCI_AUTOCCANCEL,0,0);
 sci_start_undo_action(request->sci);SEND(request->sci,SCI_SETTARGETRANGE,start,end);SEND(request->sci,SCI_REPLACETARGET,strlen(result),(sptr_t)result);
 gint finish=start+strlen(result);SEND(request->sci,SCI_SETSEL,request->anchor>request->caret ? finish : start,request->anchor>request->caret ? start : finish);sci_end_undo_action(request->sci);gtk_widget_grab_focus(GTK_WIDGET(request->sci));return TRUE;
}
static void paragraph_command(GtkMenuItem *item,gpointer data)
{
 (void)item;Request request={0};if(!tools_capture(&request)) return;
 if(sci_get_length(request.sci)>8*1024*1024) { ui_set_statusbar(TRUE,"Paragraph tools are limited to 8 MiB");return; }
 GeanyEditor *editor=document_get_current()->editor;const InsightFiletypeOptions *options=insight_languages_editing_options(editor);const GeanyEditorPrefs *prefs=editor_get_prefs(editor);
 gint margin=options ? options->margin : editor->line_breaking ? prefs->line_break_column : prefs->long_line_column;
 gint tab=SEND(request.sci,SCI_GETTABWIDTH,0,0);gchar *text=sci_get_contents(request.sci,-1),*eol=g_strdup(editor_get_eol_char(editor));gint command=GPOINTER_TO_INT(data);
 if(command<2) {
  GError *error=NULL;gint start=0,end=0;gchar *result=insight_paragraph(text,request.anchor,request.caret,command==1,CLAMP(margin,1,1000),CLAMP(tab,1,32),eol,&start,&end,&error);
  if(result) paragraph_apply(&request,start,end,text,result);else ui_set_statusbar(TRUE,"%s",insight_tr(error->message));g_clear_error(&error);g_free(result);g_free(text);g_free(eol);return;
 }
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Paragraph and lines",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Preview",GTK_RESPONSE_HELP,"Apply",GTK_RESPONSE_APPLY,NULL);g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-paragraph-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),760,490);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));ParagraphView view={gtk_combo_box_text_new(),gtk_spin_button_new_with_range(1,1000,1),gtk_text_view_new(),gtk_label_new(""),text,eol,request.anchor,request.caret,CLAMP(tab,1,32),0,0};
 gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),"Reform Paragraph");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),"Join Lines");gtk_combo_box_set_active(GTK_COMBO_BOX(view.mode),0);gtk_spin_button_set_value(GTK_SPIN_BUTTON(view.margin),CLAMP(margin,1,1000));
 gtk_widget_set_name(view.mode,"insight-paragraph-mode");gtk_widget_set_name(view.margin,"insight-paragraph-margin");gtk_widget_set_name(view.preview,"insight-paragraph-preview");gtk_widget_set_name(view.status,"insight-paragraph-status");gtk_text_view_set_editable(GTK_TEXT_VIEW(view.preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view.preview),TRUE);g_object_set_data(G_OBJECT(view.preview),"insight-no-i18n",GINT_TO_POINTER(1));
 GtkWidget *label=gtk_label_new("Margin in display columns (temporary; file type settings are unchanged)"),*scope=gtk_label_new("Reform: blank-delimited paragraphs. Join: intersected lines, or current + next line."),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_label_set_line_wrap(GTK_LABEL(scope),TRUE);gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);gtk_container_add(GTK_CONTAINER(scroll),view.preview);
 GtkWidget *items[]={scope,view.mode,label,view.margin,view.status};for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,5);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,5);insight_panel_help_attach(box,"paragraph-tools.md");
 g_signal_connect(view.mode,"changed",G_CALLBACK(paragraph_changed),&view);g_signal_connect(view.margin,"value-changed",G_CALLBACK(paragraph_changed),&view);paragraph_changed(NULL,&view);gtk_widget_show_all(dialog);
 gint response;while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_HELP || response==GTK_RESPONSE_APPLY) {
  gchar *result=paragraph_preview(&view);if(response==GTK_RESPONSE_APPLY && result) { paragraph_apply(&request,view.start,view.end,text,result);g_free(result);break; }g_free(result);
 }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_free(text);g_free(eol);
}
static void paragraph_menu_show(GtkWidget *menu,gpointer data)
{
 (void)data;gboolean ready=tools_capture(&(Request){0});GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_set_sensitive(p->data,ready);g_list_free(children);
}
typedef struct { GtkWidget *mode,*indent,*preview,*status;const gchar *source,*eol;gint anchor,caret; } LineInsertView;
static gboolean line_insert_plan(LineInsertView *view,InsightLineInsert *plan)
{
 GError *error=NULL;gboolean ok=insight_line_insert(view->source,view->anchor,view->caret,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode)),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(view->indent)),view->eol,plan,&error);
 if(!ok) { gtk_label_set_text(GTK_LABEL(view->status),insight_tr(error->message));g_clear_error(&error); }return ok;
}
static void line_insert_render(LineInsertView *view,const InsightLineInsert *plan)
{
 GString *result=g_string_new_len(view->source,plan->start);g_string_append(result,plan->text);g_string_append(result,view->source+plan->end);
 gint low=MAX(0,plan->caret-4000),high=MIN((gint)result->len,plan->caret+4000);
 while(low<plan->caret && (result->str[low]&0xc0)==0x80) low++;
 while(high>plan->caret && (result->str[high]&0xc0)==0x80) high--;
 if(low && result->str[low]=='\n' && result->str[low-1]=='\r') low++;
 if(high<(gint)result->len && high && result->str[high-1]=='\r' && result->str[high]=='\n') high--;
 GString *display=g_string_new(NULL);
 for(gint p=low;p<=high;) {
  if(p==plan->caret) g_string_append(display,"│");
  if(p==high) break;
  gchar c=result->str[p];
  if(c=='\r' || c=='\n') { g_string_append(display,"¶\n");if(c=='\r' && p+1<high && result->str[p+1]=='\n') p++;p++; }
  else if(c==' ' || c=='\t') { g_string_append(display,c==' ' ? "·" : "⇥");p++; }
  else { const gchar *next=g_utf8_next_char(result->str+p);g_string_append_len(display,result->str+p,next-(result->str+p));p=next-result->str; }
 }
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),display->str,display->len);
 gchar *status=insight_i18n_printf("Replace bytes %d–%d; caret %d. Shown result bytes %d–%d.",plan->start,plan->end,plan->caret,low,high);gtk_label_set_text(GTK_LABEL(view->status),status);g_free(status);g_string_free(display,TRUE);g_string_free(result,TRUE);
}
static void line_insert_changed(GtkWidget *widget,gpointer data)
{
 (void)widget;LineInsertView *view=data;gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),"",0);gtk_label_set_text(GTK_LABEL(view->status),"Press Preview to inspect the result before applying");
}
static gboolean line_insert_apply(Request *request,const InsightLineInsert *plan)
{
 if(!file_request_valid(request,TRUE) || !tools_capture(&(Request){0})) { ui_set_statusbar(TRUE,"Line insertion cancelled because the document or selection changed");return FALSE; }
 insight_completion_cancel(document_get_current()->editor);SEND(request->sci,SCI_AUTOCCANCEL,0,0);
 sci_start_undo_action(request->sci);SEND(request->sci,SCI_SETTARGETRANGE,plan->start,plan->end);SEND(request->sci,SCI_REPLACETARGET,strlen(plan->text),(sptr_t)plan->text);SEND(request->sci,SCI_SETSEL,plan->caret,plan->caret);sci_end_undo_action(request->sci);SEND(request->sci,SCI_SCROLLCARET,0,0);gtk_widget_grab_focus(GTK_WIDGET(request->sci));return TRUE;
}
static void line_insert_command(GtkMenuItem *item,gpointer data)
{
 (void)item;Request request={0};if(!tools_capture(&request)) return;
 if(sci_get_length(request.sci)>8*1024*1024) { ui_set_statusbar(TRUE,"Line insertion is limited to 8 MiB");return; }
 GeanyEditor *editor=document_get_current()->editor;gboolean indent=editor_get_indent_prefs(editor)->auto_indent_mode!=GEANY_AUTOINDENT_NONE;gchar *source=sci_get_contents(request.sci,-1),*eol=g_strdup(editor_get_eol_char(editor));gint command=GPOINTER_TO_INT(data);InsightLineInsert plan={0};GError *error=NULL;
 if(strlen(source)!=(gsize)sci_get_length(request.sci)) { ui_set_statusbar(TRUE,"Line insertion requires text without NUL bytes");g_free(source);g_free(eol);return; }
 if(command<3) {
  if(insight_line_insert(source,request.anchor,request.caret,command,indent,eol,&plan,&error)) line_insert_apply(&request,&plan);else ui_set_statusbar(TRUE,"%s",insight_tr(error->message));g_clear_error(&error);insight_line_insert_clear(&plan);g_free(source);g_free(eol);return;
 }
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Line insertion preview",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Preview",GTK_RESPONSE_HELP,"Apply",GTK_RESPONSE_APPLY,NULL);g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-line-insert-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),780,470);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));LineInsertView view={gtk_combo_box_text_new(),gtk_check_button_new_with_label("Copy the current line's indentation (temporary override)"),gtk_text_view_new(),gtk_label_new(""),source,eol,request.anchor,request.caret};
 const gchar *labels[]={"Insert Line — above","Insert Line Before Next — below","Insert New Line — split and stay"};for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),labels[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(view.mode),0);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(view.indent),indent);
 gtk_widget_set_name(view.mode,"insight-line-insert-mode");gtk_widget_set_name(view.indent,"insight-line-insert-indent");gtk_widget_set_name(view.preview,"insight-line-insert-preview");gtk_widget_set_name(view.status,"insight-line-insert-status");gtk_text_view_set_editable(GTK_TEXT_VIEW(view.preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view.preview),TRUE);g_object_set_data(G_OBJECT(view.preview),"insight-no-i18n",GINT_TO_POINTER(1));
 GtkWidget *scope=gtk_label_new("Above/below use the active caret line. Split replaces the exact selection."),*legend=gtk_label_new("Preview marks: · space, ⇥ tab, ¶ line ending, │ resulting caret"),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_label_set_line_wrap(GTK_LABEL(scope),TRUE);gtk_label_set_line_wrap(GTK_LABEL(legend),TRUE);gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);gtk_container_add(GTK_CONTAINER(scroll),view.preview);
 GtkWidget *items[]={scope,view.mode,view.indent,legend,view.status};for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,5);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,5);insight_panel_help_attach(box,"line-insertion.md");
 g_signal_connect(view.mode,"changed",G_CALLBACK(line_insert_changed),&view);g_signal_connect(view.indent,"toggled",G_CALLBACK(line_insert_changed),&view);line_insert_changed(NULL,&view);gtk_widget_show_all(dialog);
 gint response;while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_HELP || response==GTK_RESPONSE_APPLY) {
  if(line_insert_plan(&view,&plan)) { line_insert_render(&view,&plan);if(response==GTK_RESPONSE_APPLY) { line_insert_apply(&request,&plan);insight_line_insert_clear(&plan);break; }insight_line_insert_clear(&plan); }
 }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_free(source);g_free(eol);
}
static void line_insert_menu(GtkWidget *menu)
{
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 const gchar *labels[]={"Insert Line","Insert Line Before Next","Insert New Line","Line Insertion Preview…"},*ids[]={"insight-insert-line","insight-insert-line-below","insight-insert-new-line","insight-line-insert-preview-command"};
 for(guint i=0;i<4;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(entry,ids[i]);g_signal_connect(entry,"activate",G_CALLBACK(line_insert_command),GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry); }
}

static void paragraph_menu(GtkWidget *menu)
{
 GtkWidget *parent=gtk_menu_item_new_with_label("Paragraph and lines"),*sub=gtk_menu_new();gtk_widget_set_name(parent,"insight-paragraph-tools");gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),sub);
 const gchar *labels[]={"Reform Paragraph","Join Lines","Paragraph Preview…"},*ids[]={"insight-reform-paragraph","insight-join-lines","insight-paragraph-preview-command"};
 for(guint i=0;i<3;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(entry,ids[i]);g_signal_connect(entry,"activate",G_CALLBACK(paragraph_command),GINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry); }
 line_insert_menu(sub);
 g_signal_connect(sub,"show",G_CALLBACK(paragraph_menu_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),parent);gtk_widget_show_all(parent);
}

typedef struct { GtkWidget *mode,*radix,*value,*preview,*status;const gchar *text; } RenumberView;
static gchar *renumber_preview(RenumberView *view)
{
 GError *error=NULL;guint count=0;
 gchar *result=insight_renumber(view->text,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode)),gtk_combo_box_get_active(GTK_COMBO_BOX(view->radix)),gtk_entry_get_text(GTK_ENTRY(view->value)),&count,&error);
 gchar *status=result ? insight_i18n_printf("%u integer tokens. Preview shows at most 8000 bytes.",count) : g_strdup(insight_tr(error->message));
 gtk_label_set_text(GTK_LABEL(view->status),status);g_free(status);g_clear_error(&error);
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview));
 gsize length=result ? MIN(strlen(result),8000) : 0;
 while(length && !g_utf8_validate(result,length,NULL)) length--;
 gtk_text_buffer_set_text(buffer,result ? result : "",length);
 return result;
}
static void renumber_changed(GtkWidget *widget,gpointer data)
{
 (void)widget;RenumberView *view=data;
 gtk_widget_set_sensitive(view->value,gtk_combo_box_get_active(GTK_COMBO_BOX(view->mode))!=0);
 /* Options invalidate the displayed preview; large documents are processed only
  * on explicit Preview or Apply, never on every keystroke. */
 gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view->preview)),"",0);
 gtk_label_set_text(GTK_LABEL(view->status),"Press Preview to inspect the result before applying");
}
static void renumber_text(GtkMenuItem *item,gpointer data)
{
 (void)item;(void)data;Request request={0};if(!tools_capture(&request)) return;
 gint start=MIN(request.anchor,request.caret),end=MAX(request.anchor,request.caret);
 if(start==end) { start=0;end=sci_get_length(request.sci); }
 if(end-start>8*1024*1024) { ui_set_statusbar(TRUE,"Renumber is limited to 8 MiB");return; }
 gchar *text=sci_get_contents_range(request.sci,start,end);
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Renumber",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Preview",GTK_RESPONSE_HELP,"Apply",GTK_RESPONSE_APPLY,NULL);
 g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-renumber-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),720,480);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*scope=gtk_label_new(request.anchor==request.caret ? "Scope: whole document" : "Scope: exact selection");
 RenumberView view={gtk_combo_box_text_new(),gtk_combo_box_text_new(),gtk_entry_new(),gtk_text_view_new(),gtk_label_new(""),text};
 const gchar *modes[]={"Auto: ascend from first number","Start at value","Offset existing numbers"},*radices[]={"Auto: preserve each number's radix","Decimal","Hexadecimal","Octal"};
 for(guint i=0;i<G_N_ELEMENTS(modes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.mode),modes[i]);
 for(guint i=0;i<G_N_ELEMENTS(radices);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(view.radix),radices[i]);
 gtk_combo_box_set_active(GTK_COMBO_BOX(view.mode),0);gtk_combo_box_set_active(GTK_COMBO_BOX(view.radix),0);gtk_entry_set_text(GTK_ENTRY(view.value),"1");
 gtk_widget_set_name(view.mode,"insight-renumber-mode");gtk_widget_set_name(view.radix,"insight-renumber-radix");gtk_widget_set_name(view.value,"insight-renumber-value");gtk_widget_set_name(view.preview,"insight-renumber-preview");gtk_widget_set_name(view.status,"insight-renumber-status");
 gtk_text_view_set_editable(GTK_TEXT_VIEW(view.preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view.preview),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view.preview),GTK_WRAP_NONE);g_object_set_data(G_OBJECT(view.preview),"insight-no-i18n",GINT_TO_POINTER(1));
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view.preview);gtk_label_set_line_wrap(GTK_LABEL(view.status),TRUE);
 GtkWidget *items[]={scope,view.mode,view.radix,view.value,view.status};for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,5);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,5);
 g_signal_connect(view.mode,"changed",G_CALLBACK(renumber_changed),&view);g_signal_connect(view.radix,"changed",G_CALLBACK(renumber_changed),&view);g_signal_connect(view.value,"changed",G_CALLBACK(renumber_changed),&view);
 insight_panel_help_attach(box,"renumber.md");renumber_changed(NULL,&view);gtk_widget_show_all(dialog);
 gint response;
 while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_HELP || response==GTK_RESPONSE_APPLY) {
  gchar *result=renumber_preview(&view);
  if(response==GTK_RESPONSE_APPLY && result) {
   if(!file_request_valid(&request,TRUE) || !tools_capture(&(Request){0})) ui_set_statusbar(TRUE,"Renumber cancelled because the document or selection changed");
   else if(strcmp(text,result)) {
    insight_completion_cancel(document_get_current()->editor);SEND(request.sci,SCI_AUTOCCANCEL,0,0);
    sci_start_undo_action(request.sci);SEND(request.sci,SCI_SETTARGETRANGE,start,end);SEND(request.sci,SCI_REPLACETARGET,strlen(result),(sptr_t)result);
    /* Keep the transformed range selected, including reverse selections. */
    gint finish=start+strlen(result);SEND(request.sci,SCI_SETSEL,request.anchor>request.caret ? finish : start,request.anchor>request.caret ? start : finish);sci_end_undo_action(request.sci);
   }
   g_free(result);break;
  }
  g_free(result);
 }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_free(text);
}

static void tools_menu_show(GtkWidget *menu,gpointer data)
{
 (void)data;Request request={0};gboolean ready=tools_capture(&request);
 gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"renumber"),ready);
 gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"guid"),ready);
 gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"character"),ready);
 gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"insert-file"),ready);
 GeanyDocument *doc=document_get_current();gboolean single=doc && SEND(doc->editor->sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(doc->editor->sci)==SC_SEL_STREAM && sci_has_selection(doc->editor->sci) && !SEND(doc->editor->sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(doc->editor->sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0);
 gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"save-selection"),single);
 gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"save-copy"),doc!=NULL);
}
void insight_paste_tools_menu(GtkWidget *menu)
{
 paragraph_menu(menu);
 GtkWidget *parent=gtk_menu_item_new_with_label("Insert and clipboard"),*sub=gtk_menu_new();gtk_widget_set_name(parent,"insight-insert-tools");gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),sub);
 const gchar *labels[]={"Insert GUID","Insert ASCII / Unicode…","Show Clipboard"},*ids[]={"insight-insert-guid","insight-insert-character","insight-show-clipboard"};GCallback actions[]={G_CALLBACK(insert_guid),G_CALLBACK(insert_character),G_CALLBACK(show_clipboard)};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) { GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(entry,ids[i]);if(i<2) g_object_set_data(G_OBJECT(sub),i ? "character" : "guid",entry);g_signal_connect(entry,"activate",actions[i],NULL);gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry); }
 gtk_menu_shell_append(GTK_MENU_SHELL(sub),gtk_separator_menu_item_new());
 const gchar *file_labels[]={"Insert File…","Save Selection…","Save a Copy…"},*file_ids[]={"insight-insert-file","insight-save-selection","insight-save-copy"},*file_keys[]={"insert-file","save-selection","save-copy"};
 for(guint i=0;i<3;i++) { GtkWidget *entry=gtk_menu_item_new_with_label(file_labels[i]);gtk_widget_set_name(entry,file_ids[i]);g_object_set_data(G_OBJECT(sub),file_keys[i],entry);g_signal_connect(entry,"activate",i ? G_CALLBACK(export_text) : G_CALLBACK(insert_file),GINT_TO_POINTER(i==1));gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry); }
 GtkWidget *renumber=gtk_menu_item_new_with_label("Renumber…");gtk_widget_set_name(renumber,"insight-renumber");g_object_set_data(G_OBJECT(sub),"renumber",renumber);g_signal_connect(renumber,"activate",G_CALLBACK(renumber_text),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(sub),renumber);
 g_signal_connect(sub,"show",G_CALLBACK(tools_menu_show),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),parent);gtk_widget_show_all(parent);
}
