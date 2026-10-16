/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static const gchar *content = "first\r\n \t\r\nПривет world\r\nlast\r\n";
static gchar *directory, *profile, *source, *other;
static GeanyDocument *doc;
static GtkWidget *window, *menu, *toggle;
static gboolean started, captured;
static GtkWidget *named(GtkWidget *w, const gchar *name)
{
    if (!g_strcmp0(gtk_widget_get_name(w), name)) return w;
    if (GTK_IS_MENU_ITEM(w))
    {
        GtkWidget *sub = gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));
        GtkWidget *found = sub ? named(sub, name) : NULL;
        if (found) return found;
    }
    if (GTK_IS_CONTAINER(w))
    {
        GList *children = gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found = NULL;
        for (GList *p = children; p && !found; p = p->next) found = named(p->data, name);
        g_list_free(children);return found;
    }
    return NULL;
}
static void action(const gchar *id)
{
    gchar *name = g_strconcat("insight-nav-", id, NULL);
    GtkWidget *item = named(window, name);g_assert_nonnull(item);
    gtk_menu_item_activate(GTK_MENU_ITEM(item));g_free(name);
}
static void mode(gboolean active)
{
    g_signal_emit_by_name(menu, "show");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(toggle), active);
    g_assert_cmpint(SEND(doc->editor->sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0), ==, active);
}
static void reset(gint position)
{
    SEND(doc->editor->sci, SCI_SETREADONLY, FALSE, 0);
    SEND(doc->editor->sci, SCI_SETSELECTIONMODE, SC_SEL_STREAM, 0);
    SEND(doc->editor->sci, SCI_SETTEXT, 0, (sptr_t)content);
    SEND(doc->editor->sci, SCI_SETSEL, position, position);
    SEND(doc->editor->sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));
}
static void selection(gint anchor, gint caret)
{
    g_assert_cmpint(SEND(doc->editor->sci, SCI_GETANCHOR, 0, 0), ==, anchor);
    g_assert_cmpint(sci_get_current_position(doc->editor->sci), ==, caret);
}
static void selected(const gchar *expected)
{
    gchar *text = sci_get_selection_contents(doc->editor->sci);
    g_assert_cmpstr(text, ==, expected);g_free(text);
}
static void escape(void)
{
    GdkEvent *event = gdk_event_new(GDK_KEY_PRESS);
    event->key.window = g_object_ref(gtk_widget_get_window(GTK_WIDGET(doc->editor->sci)));
    event->key.keyval = GDK_KEY_Escape;gboolean handled = FALSE;
    g_signal_emit_by_name(doc->editor->sci, "key-press-event", &event->key, &handled);
    g_assert_true(handled);gdk_event_free(event);
}
static gboolean capture(gpointer data)
{
    (void)data;
    GdkWindow *surface = gtk_widget_get_window(menu);g_assert_nonnull(surface);
    GdkPixbuf *image = gdk_pixbuf_get_from_window(surface, 0, 0,
        gtk_widget_get_allocated_width(menu), gtk_widget_get_allocated_height(menu));
    g_assert_nonnull(image);
    gint width = gdk_pixbuf_get_width(image), height = gdk_pixbuf_get_height(image);
    gint stride = gdk_pixbuf_get_rowstride(image), channels = gdk_pixbuf_get_n_channels(image);
    guchar *pixels = gdk_pixbuf_get_pixels(image);guint bright = 0;
    for (gint y = 0; y < height; y++) for (gint x = 0; x < width; x++)
    {
        const guchar *p = pixels + y * stride + x * channels;
        if ((guint)p[0] + p[1] + p[2] > 128 * 3) bright++;
    }
    g_assert_cmpuint(bright, >, (guint)(width * height / 2));
    g_assert_true(gdk_pixbuf_save(image, g_getenv("GEANY_NAVIGATION_CAPTURE"), "jpeg", NULL, "quality", "82", NULL));
    g_object_unref(image);gtk_menu_popdown(GTK_MENU(menu));captured = TRUE;return G_SOURCE_REMOVE;
}
static void checks(void)
{
    ScintillaObject *sci = doc->editor->sci;
    gint blank = strlen("first\r\n"), word = strlen("first\r\n \t\r\n");
    gint word_end = word + strlen("Привет"), line_end = word + strlen("Привет world");
    reset(word + 2);action("select-word");selected("Привет");
    reset(word + 2);action("select-line");selected("Привет world\r\n");
    reset(word + strlen("Привет "));action("select-line-end");selected("world");
    action("select-file-start");selection(word + strlen("Привет "), 0);
    action("selection-begin");selection(0, 0);
    reset(word);action("select-file-end");selection(word, strlen(content));
    action("selection-end");selection(strlen(content), strlen(content));
    reset(word);action("blank-line-up");selection(blank, blank);
    action("blank-line-up");selection(blank, blank); /* no earlier blank */
    reset(0);action("blank-line-down");selection(blank, blank);
    action("blank-line-down");selection(strlen(content), strlen(content));
    mode(TRUE);action("selection-end"); /* no selection: no-op, retaining mode */
    g_assert_true(SEND(sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0));mode(FALSE);
    reset(word);mode(TRUE);
    SEND(sci, SCI_CHARRIGHT, 0, 0);selection(word, word + 2); /* one UTF-8 character */
    SEND(sci, SCI_WORDRIGHT, 0, 0);g_assert_cmpint(sci_get_current_position(sci), >=, word_end);
    g_assert_cmpint(SEND(sci, SCI_GETANCHOR, 0, 0), ==, word);
    action("blank-line-up");selection(word, blank);
    action("file-end");selection(word, strlen(content));
    sci_goto_line(sci, 0, TRUE);selection(word, 0);
    sci_set_current_position(sci, line_end, TRUE);selection(word, line_end);
    g_assert_true(SEND(sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0));
    escape();selection(word, line_end);g_assert_false(SEND(sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0));
    SEND(sci, SCI_CHARRIGHT, 0, 0);g_assert_false(sci_has_selection(sci));
    reset(2);mode(TRUE);GeanyDocument *first = doc;
    doc = document_open_file(other, FALSE, NULL, NULL);g_assert_nonnull(doc);
    g_signal_emit_by_name(menu, "show");g_assert_false(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(toggle)));
    SEND(doc->editor->sci, SCI_SETSAVEPOINT, 0, 0);
    doc = document_open_file(source, FALSE, NULL, NULL);g_assert_true(doc == first);
    g_signal_emit_by_name(menu, "show");g_assert_true(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(toggle)));mode(FALSE);
    reset(1);SEND(sci, SCI_SETREADONLY, TRUE, 0);mode(TRUE);
    action("file-end");selection(1, strlen(content));
    action("selection-begin");selection(1, 1);g_assert_false(SEND(sci, SCI_GETMOVEEXTENDSSELECTION, 0, 0));
    SEND(sci, SCI_SETREADONLY, FALSE, 0);
    reset(2);SEND(sci, SCI_SETSELECTIONMODE, SC_SEL_RECTANGLE, 0);
    gint old = sci_get_current_position(sci);action("file-end");g_assert_cmpint(sci_get_current_position(sci), ==, old);
    g_signal_emit_by_name(menu, "show");g_assert_false(gtk_widget_get_sensitive(toggle));
    reset(2);SEND(sci, SCI_ADDSELECTION, 4, 4);old = sci_get_current_position(sci);
    action("file-end");g_assert_cmpint(sci_get_current_position(sci), ==, old);
    reset(0);
    gchar *large = g_strnfill(4 * 1024 * 1024 + 1, 'x');
    gchar *limited = g_strconcat(large, "\n\n", NULL);g_free(large);
    SEND(sci, SCI_SETTEXT, 0, (sptr_t)limited);SEND(sci, SCI_SETSEL, 0, 0);
    SEND(sci, SCI_EMPTYUNDOBUFFER, 0, 0);
    action("blank-line-down");selection(0, 0);g_free(limited);
    reset(2);action("file-start");selection(0, 0);action("file-end");selection(strlen(content), strlen(content));
    g_assert_false(SEND(sci, SCI_CANUNDO, 0, 0));
    gchar *text = sci_get_contents(sci, sci_get_length(sci) + 2);g_assert_cmpstr(text, ==, content);g_free(text);
    gchar *disk = NULL;g_assert_true(g_file_get_contents(source, &disk, NULL, NULL));g_assert_cmpstr(disk, ==, content);g_free(disk);
    g_print("Selection/navigation: Unicode/CRLF, word/line/range selection, blank-line traversal, native Extend Mode, goto-line/position, Escape, per-document state, read-only and mode guards passed\n");
}
static gboolean step(gpointer data)
{
    (void)data;if (!document_get_current()) return G_SOURCE_CONTINUE;
    if (!started)
    {
        doc = document_open_file(source, FALSE, NULL, NULL);g_assert_nonnull(doc);
        window = gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
        GtkWidget *parent = named(window, "insight-selection-navigation");g_assert_nonnull(parent);
        menu = gtk_menu_item_get_submenu(GTK_MENU_ITEM(parent));toggle = named(menu, "insight-extend-mode");g_assert_nonnull(toggle);
        started = TRUE;
        if (g_getenv("GEANY_NAVIGATION_CAPTURE"))
        {
            GdkEvent *event = gdk_event_new(GDK_BUTTON_PRESS);
            event->button.window = g_object_ref(gtk_widget_get_window(GTK_WIDGET(doc->editor->sci)));
            event->button.button = 3;event->button.time = GDK_CURRENT_TIME;
            gdk_event_set_device(event, gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));
            gtk_menu_popup_at_widget(GTK_MENU(menu), GTK_WIDGET(doc->editor->sci), GDK_GRAVITY_SOUTH_WEST, GDK_GRAVITY_NORTH_WEST, event);
            gdk_event_free(event);g_timeout_add(150, capture, NULL);return G_SOURCE_CONTINUE;
        }
        captured = TRUE;
    }
    if (!captured) return G_SOURCE_CONTINUE;
    checks();SEND(doc->editor->sci, SCI_SETSAVEPOINT, 0, 0);
    gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window, "menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data)
{
    (void)data;g_error("Navigation GUI timeout");return G_SOURCE_REMOVE;
}
int main(void)
{
    if (!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
    directory = g_dir_make_tmp("insight-navigation-XXXXXX", NULL);profile = g_build_filename(directory, "profile", NULL);
    g_assert_cmpint(g_mkdir_with_parents(profile, 0700), ==, 0);
    source = g_build_filename(directory, "navigation.cpp", NULL);other = g_build_filename(directory, "other.cpp", NULL);
    g_assert_true(g_file_set_contents(source, content, -1, NULL));g_assert_true(g_file_set_contents(other, "other\n", -1, NULL));
    gchar *config = g_build_filename(profile, "geany.conf", NULL);
    g_assert_true(g_file_set_contents(config, "[geany]\nconfirm_exit=false\n", -1, NULL));g_free(config);
    gchar *args[] = {"navigation-native", "--new-instance", "--no-session", "--config", profile, source, NULL};
    g_timeout_add(250, step, NULL);g_timeout_add_seconds(30, timeout, NULL);
    g_log_set_always_fatal(G_LOG_FATAL_MASK | G_LOG_LEVEL_CRITICAL);gint result = main_lib(6, args);
    g_free(source);g_free(other);g_free(profile);g_free(directory);return result;
}
