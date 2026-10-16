/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-navigation.h"
#include "document.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <gdk/gdkkeysyms.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *extend_item;
static GeanyDocument *for_sci(ScintillaObject *sci)
{
    for (guint i = 0; documents_array && i < documents_array->len; i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (doc->is_valid && doc->editor->sci == sci) return doc;
    }
    return NULL;
}
static gboolean normal(ScintillaObject *sci)
{
    return sci && for_sci(sci) && SEND(sci, SCI_GETSELECTIONS, 0, 0) == 1 &&
        sci_get_selection_mode(sci) == SC_SEL_STREAM &&
        !SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) &&
        !SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0);
}
static void select_to(ScintillaObject *sci, gint anchor, gint caret, gboolean mode)
{
    gint length = sci_get_length(sci);
    SEND(sci, SCI_SETSEL, CLAMP(anchor, 0, length), CLAMP(caret, 0, length));
    SEND(sci, SCI_SETMOVEEXTENDSSELECTION, mode, 0);
    SEND(sci, SCI_CHOOSECARETX, 0, 0);
    SEND(sci, SCI_ENSUREVISIBLE, sci_get_current_line(sci), 0);
    SEND(sci, SCI_SCROLLCARET, 0, 0);
}
gboolean insight_navigation_extend_to(ScintillaObject *sci, gint position)
{
    if (!normal(sci) || !SEND(sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0)) return FALSE;
    select_to(sci, SEND(sci, SCI_GETANCHOR, 0, 0), position, TRUE);
    return TRUE;
}
static void move_to(ScintillaObject *sci, gint position)
{
    if (!insight_navigation_extend_to(sci, position)) select_to(sci, position, position, FALSE);
}
static void toggle(GtkCheckMenuItem *item, gpointer data)
{
    (void)data;
    GeanyDocument *doc = document_get_current();
    if (!doc || !normal(doc->editor->sci)) return;
    gboolean active = gtk_check_menu_item_get_active(item);
    SEND(doc->editor->sci, SCI_SETMOVEEXTENDSSELECTION, active, 0);
    ui_set_statusbar(FALSE, active ? "Extend Mode: ON" : "Extend Mode: OFF");
    gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
}
static void menu_show(GtkWidget *menu, gpointer data)
{
    (void)menu;(void)data;
    GeanyDocument *doc = document_get_current();
    gboolean ready = doc && normal(doc->editor->sci);
    g_signal_handlers_block_by_func(extend_item, toggle, NULL);
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(extend_item),
        ready && SEND(doc->editor->sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0));
    gtk_widget_set_sensitive(extend_item, ready);
    g_signal_handlers_unblock_by_func(extend_item, toggle, NULL);
}
gboolean insight_navigation_key(GeanyEditor *editor, GdkEventKey *event)
{
    if (event->keyval != GDK_KEY_Escape ||
        (event->state & gtk_accelerator_get_default_mod_mask()) ||
        !SEND(editor->sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0)) return FALSE;
    SEND(editor->sci, SCI_SETMOVEEXTENDSSELECTION, FALSE, 0);
    ui_set_statusbar(FALSE, "Extend Mode: OFF");
    return TRUE;
}
static void blank_line(ScintillaObject *sci, gint direction)
{
    gint current = sci_get_current_line(sci), total = sci_get_line_count(sci);
    gint origin = sci_get_position_from_line(sci, current);
    guint visited = 0;
    for (gint line = current + direction; line >= 0 && line < total; line += direction)
    {
        gint start = sci_get_position_from_line(sci, line);
        gint end = SEND(sci, SCI_GETLINEENDPOSITION, line, 0);
        if (++visited > 100000 || ABS((gint64)end - origin) > 4 * 1024 * 1024 ||
            ABS((gint64)start - origin) > 4 * 1024 * 1024)
        {
            ui_set_statusbar(FALSE, "Blank line search limit reached.");
            return;
        }
        if (SEND(sci, SCI_GETLINEINDENTPOSITION, line, 0) == end)
        {
            move_to(sci, start);return;
        }
    }
}
static void command(GtkMenuItem *item, gpointer data)
{
    (void)item;
    GeanyDocument *doc = document_get_current();
    if (!doc || !normal(doc->editor->sci)) return;
    ScintillaObject *sci = doc->editor->sci;
    gint anchor = SEND(sci, SCI_GETANCHOR, 0, 0);
    gboolean mode = SEND(sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0);
    switch (GPOINTER_TO_INT(data))
    {
        case 0: if (sci_has_selection(sci)) select_to(sci, sci_get_selection_start(sci), sci_get_selection_start(sci), FALSE);break;
        case 1: if (sci_has_selection(sci)) select_to(sci, sci_get_selection_end(sci), sci_get_selection_end(sci), FALSE);break;
        case 2: blank_line(sci, -1);break;
        case 3: blank_line(sci, 1);break;
        case 4: editor_select_word(doc->editor);SEND(sci, SCI_SETMOVEEXTENDSSELECTION, mode, 0);break;
        case 5:
        {
            gint line = sci_get_current_line(sci), total = sci_get_line_count(sci);
            select_to(sci, sci_get_position_from_line(sci, line),
                line + 1 < total ? sci_get_position_from_line(sci, line + 1) : sci_get_length(sci), mode);
            break;
        }
        case 6: select_to(sci, anchor, SEND(sci, SCI_GETLINEENDPOSITION, sci_get_current_line(sci), 0), mode);break;
        case 7: select_to(sci, anchor, 0, mode);break;
        case 8: select_to(sci, anchor, sci_get_length(sci), mode);break;
        case 9: move_to(sci, 0);break;
        case 10: move_to(sci, sci_get_length(sci));break;
        default: return;
    }
    gtk_widget_grab_focus(GTK_WIDGET(sci));
}
void insight_navigation_menu(GtkWidget *menu)
{
    GtkWidget *parent = gtk_menu_item_new_with_label("Selection and navigation");
    gtk_widget_set_name(parent, "insight-selection-navigation");
    GtkWidget *sub = gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent), sub);
    extend_item = gtk_check_menu_item_new_with_label("Toggle Extend Mode");
    gtk_widget_set_name(extend_item, "insight-extend-mode");
    g_signal_connect(extend_item, "toggled", G_CALLBACK(toggle), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), extend_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
    const gchar *labels[] = {"Beginning of Selection", "End of Selection", "Blank Line Up", "Blank Line Down", "Select Word", "Select Line", "Select To End Of Line", "Select To Top Of File", "Select To End Of File", "Top of File", "Bottom of File"};
    const gchar *ids[] = {"selection-begin", "selection-end", "blank-line-up", "blank-line-down", "select-word", "select-line", "select-line-end", "select-file-start", "select-file-end", "file-start", "file-end"};
    for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
    {
        if (i == 4 || i == 9) gtk_menu_shell_append(GTK_MENU_SHELL(sub), gtk_separator_menu_item_new());
        GtkWidget *entry = gtk_menu_item_new_with_label(labels[i]);
        gchar *name = g_strconcat("insight-nav-", ids[i], NULL);gtk_widget_set_name(entry, name);g_free(name);
        g_signal_connect(entry, "activate", G_CALLBACK(command), GINT_TO_POINTER(i));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub), entry);
    }
    g_signal_connect(sub, "show", G_CALLBACK(menu_show), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), parent);gtk_widget_show_all(parent);
}
