/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include <gdk/gdkkeysyms.h>
#include "insight-browser-mode.h"
#include "insight-workbench.h"
#include "document.h"
#include "documentprivate.h"
#include "geanyobject.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "insight-help.h"
#include "insight-i18n.h"
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct { gboolean readonly,sci_readonly; } Original;
static GHashTable *originals;
static GtkWidget *menu_item,*popup_item,*badge;
static gboolean enabled,syncing;
static Original *original(GeanyDocument *doc) { return originals && DOC_VALID(doc) ? g_hash_table_lookup(originals,GUINT_TO_POINTER(doc->id)):NULL; }
gboolean insight_browser_mode_active(void) { return enabled; }
gboolean insight_browser_mode_file_readonly(GeanyDocument *doc) { Original *old=original(doc);return old ? old->readonly:doc->readonly; }
static void protect(GeanyDocument *doc) {
 if(!enabled || !DOC_VALID(doc)) return;
 if(!original(doc)) { Original *old=g_new(Original,1);old->readonly=doc->readonly;old->sci_readonly=SEND(doc->editor->sci,SCI_GETREADONLY,0,0);g_hash_table_insert(originals,GUINT_TO_POINTER(doc->id),old); }
 doc->readonly=TRUE;sci_set_readonly(doc->editor->sci,TRUE);ui_update_tab_status(doc);
}
void insight_browser_mode_reloaded(GeanyDocument *doc) {
 if(!enabled || !DOC_VALID(doc)) return;
 Original *old=original(doc);if(old) { old->readonly=doc->readonly;old->sci_readonly=SEND(doc->editor->sci,SCI_GETREADONLY,0,0); }protect(doc);
}
static void reloaded(GObject *object,GeanyDocument *doc,gpointer data) { (void)object;(void)data;insight_browser_mode_reloaded(doc); }
static void saved(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(enabled && DOC_VALID(doc) && !doc->readonly) insight_browser_mode_reloaded(doc);else protect(doc);
}
static void closed(GObject *object,GeanyDocument *doc,gpointer data) { (void)object;(void)data;if(originals) g_hash_table_remove(originals,GUINT_TO_POINTER(doc->id)); }
static void sync(void) {
 syncing=TRUE;gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_item),enabled);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(popup_item),enabled);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(badge),enabled);syncing=FALSE;
 GeanyDocument *doc=document_get_current();if(doc) { ui_update_tab_status(doc);ui_update_statusbar(doc); }
}
void insight_browser_mode_toggle(void) {
 if(!originals || !menu_item) return;
 enabled=!enabled;
 for(guint n=0;documents_array && n<documents_array->len;n++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(!DOC_VALID(doc)) continue;
  if(enabled) protect(doc);
  else { Original *old=original(doc);if(old) { doc->readonly=old->readonly;sci_set_readonly(doc->editor->sci,old->sci_readonly || doc->readonly || (doc->priv && doc->priv->protected));ui_update_tab_status(doc); } }
 }
 if(!enabled) g_hash_table_remove_all(originals);
 sync();ui_set_statusbar(FALSE,"%s",insight_tr(enabled ? "Browser Mode: ON — click/Space to browse, Backspace to go back":"Browser Mode: OFF"));
}
static void toggled(GtkWidget *widget,gpointer data) { (void)widget;(void)data;if(!syncing) insight_browser_mode_toggle(); }
static void help(GtkWidget *widget,gpointer data) { (void)widget;(void)data;insight_help_show(NULL,"browser-mode.md"); }
static gboolean status_popup(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->button!=3) return FALSE;
 GtkWidget *menu=gtk_menu_new(),*toggle=gtk_check_menu_item_new_with_label("Browser Mode"),*guide=gtk_menu_item_new_with_label("Browser Mode help…");
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(toggle),enabled);g_signal_connect(toggle,"toggled",G_CALLBACK(toggled),NULL);g_signal_connect(guide,"activate",G_CALLBACK(help),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),toggle);gtk_menu_shell_append(GTK_MENU_SHELL(menu),guide);g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);insight_i18n_apply(menu);gtk_widget_show_all(menu);gtk_menu_popup_at_widget(GTK_MENU(menu),widget,GDK_GRAVITY_NORTH_EAST,GDK_GRAVITY_SOUTH_EAST,(GdkEvent *)event);return TRUE;
}
gboolean insight_browser_mode_key(GtkWidget *widget,GdkEventKey *event) {
 GeanyDocument *doc=document_get_current();if(!enabled || !DOC_VALID(doc) || widget!=GTK_WIDGET(doc->editor->sci) || !gtk_widget_has_focus(widget) || (event->state&gtk_accelerator_get_default_mod_mask())) return FALSE;
 if(event->keyval==GDK_KEY_BackSpace) { keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_BACK);return TRUE; }
 if(event->keyval==GDK_KEY_space) { insight_workbench_browser_definition(sci_get_current_position(doc->editor->sci));return TRUE; }
 return FALSE;
}
gboolean insight_browser_mode_mouse(GeanyEditor *editor,GdkEventButton *event,gint position) {
 if(!enabled || editor->document!=document_get_current() || event->button!=1 || event->type!=GDK_BUTTON_PRESS || (event->state&gtk_accelerator_get_default_mod_mask())) return FALSE;
 gint margin=(gint)SEND(editor->sci,SCI_GETMARGINLEFT,0,0);for(guint n=0;n<5;n++) margin+=(gint)SEND(editor->sci,SCI_GETMARGINWIDTHN,n,0);
 if(event->x<margin || position<0) return FALSE;
 /* Normal clicks on whitespace retain native selection/caret behavior. */
 return insight_workbench_browser_definition(position);
}
static void destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;enabled=FALSE;g_clear_pointer(&originals,g_hash_table_unref);menu_item=popup_item=badge=NULL; }
void insight_browser_mode_init(GtkWidget *edit_menu) {
 originals=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);
 menu_item=gtk_check_menu_item_new_with_label("Browser Mode");gtk_widget_set_name(menu_item,"insight-browser-mode");keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_BROWSERMODE,menu_item);g_signal_connect(menu_item,"toggled",G_CALLBACK(toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),menu_item);gtk_widget_show(menu_item);
 popup_item=gtk_check_menu_item_new_with_label("Browser Mode");gtk_widget_set_name(popup_item,"insight-browser-mode-popup");g_signal_connect(popup_item,"toggled",G_CALLBACK(toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),popup_item);gtk_widget_show(popup_item);
 badge=gtk_toggle_button_new_with_label("Browse");gtk_widget_set_name(badge,"insight-browser-mode-status");gtk_button_set_relief(GTK_BUTTON(badge),GTK_RELIEF_NONE);gtk_widget_set_tooltip_text(badge,"Browser Mode: read-only browsing; right-click for options");gtk_box_pack_end(GTK_BOX(ui_lookup_widget(main_widgets.window,"statusbar")),badge,FALSE,FALSE,0);g_signal_connect(badge,"toggled",G_CALLBACK(toggled),NULL);g_signal_connect(badge,"button-press-event",G_CALLBACK(status_popup),NULL);gtk_widget_show(badge);
 const gchar *events[]={"document-new","document-open"};for(guint n=0;n<G_N_ELEMENTS(events);n++) g_signal_connect_object(geany_object,events[n],G_CALLBACK(reloaded),menu_item,G_CONNECT_AFTER);
 g_signal_connect_object(geany_object,"document-reload",G_CALLBACK(reloaded),menu_item,G_CONNECT_AFTER);g_signal_connect_object(geany_object,"document-save",G_CALLBACK(saved),menu_item,G_CONNECT_AFTER);g_signal_connect_object(geany_object,"document-close",G_CALLBACK(closed),menu_item,0);g_signal_connect(menu_item,"destroy",G_CALLBACK(destroyed),NULL);
}
