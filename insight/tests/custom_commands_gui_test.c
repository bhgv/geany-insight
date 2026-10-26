/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "main.h"
#include "document.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "../desktop/custom_command.h"
#include <string.h>
static GtkWidget *window,*tool;
static GeanyDocument *doc;
static gchar *profile,*source,*path;
static guint phase,ticks;
static GtkWidget *find(GtkWidget *root,const gchar *name) {if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;if(GTK_IS_MENU_ITEM(root)) {GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(root));if(menu) {GtkWidget *found=find(menu,name);if(found) return found;}}if(GTK_IS_CONTAINER(root)) {GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=find(p->data,name);g_list_free(children);return found;}return NULL;}
static GtkWidget *named(const gchar *name) {GtkWidget *w=find(tool,name);g_assert_nonnull(w);return w;}
static GtkWidget *top(void) {GList *all=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=all;p && !found;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-custom-commands")) found=p->data;g_list_free(all);return found;}
static void click(const gchar *name) {g_signal_emit_by_name(named(name),"clicked");}
static void set(const gchar *name,const gchar *text) {gtk_entry_set_text(GTK_ENTRY(named(name)),text);}
static gchar *output(const gchar *name) {GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(name)));GtkTextIter a,z;gtk_text_buffer_get_bounds(b,&a,&z);return gtk_text_buffer_get_text(b,&a,&z,FALSE);}
static gboolean busy(void) {return g_object_get_data(G_OBJECT(tool),"insight-custom-command-busy")!=NULL;}
static void unchanged(void) {gchar *text=sci_get_contents(doc->editor->sci,-1);g_assert_cmpstr(text,==,"int main(void) { return 0; }\n");g_free(text);g_assert_false(doc->changed);g_assert_false(scintilla_send_message(doc->editor->sci,SCI_CANUNDO,0,0));}
static gboolean step(gpointer data) {
 (void)data;static guint last=G_MAXUINT;if(last!=phase) {g_printerr("Custom commands phase %u\n",phase);last=phase;}if(++ticks>180) g_error("Custom commands timeout phase %u",phase);
 if(!window) {GeanyDocument *current=document_get_current();if(!current) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(current->editor->sci));}
 if(phase==0) {GtkWidget *menu=find(window,"insight-custom-commands-menu");if(!menu) return G_SOURCE_CONTINUE;doc=document_open_file(source,FALSE,NULL,NULL);g_assert_nonnull(doc);gtk_menu_item_activate(GTK_MENU_ITEM(menu));tool=top();g_assert_nonnull(tool);
  set("insight-custom-command-name","Fixture compiler");set("insight-custom-command-template","/bin/sh -c 'printf \"OUT:%%s\\n\" \"$1\"; printf ERR >&2; exit 7' -- %f");set("insight-custom-command-directory","%d");click("insight-custom-command-preview");gchar *preview=output("insight-custom-command-context");g_assert_nonnull(strstr(preview,source));g_free(preview);GtkWidget *ru=find(window,"insight-interface-ru"),*en=find(window,"insight-interface-en");g_assert_nonnull(ru);g_assert_nonnull(en);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(ru),TRUE);preview=output("insight-custom-command-context");g_assert_nonnull(strstr(preview,"Доступные переменные контекста:"));g_assert_nonnull(strstr(preview,source));g_free(preview);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(en),TRUE);preview=output("insight-custom-command-context");g_assert_nonnull(strstr(preview,"Available context variables:"));g_free(preview);click("insight-custom-command-edit-1");click("insight-custom-command-run");g_assert_true(busy());g_assert_false(gtk_widget_get_sensitive(named("insight-custom-command-run")));phase=1;return G_SOURCE_CONTINUE;
 }
 if(phase==1) {if(busy()) return G_SOURCE_CONTINUE;gchar *text=output("insight-custom-command-output");gchar *expected=g_strconcat("OUT:",source,NULL);g_assert_nonnull(strstr(text,expected));g_assert_nonnull(strstr(text,"ERR"));g_free(expected);g_free(text);unchanged();
  click("insight-custom-command-edit-0");set("insight-custom-command-name","Numbered argument");set("insight-custom-command-template","/bin/echo %1");set("insight-custom-command-argument-1","argument with spaces; $(literal)");click("insight-custom-command-edit-1");click("insight-custom-command-edit-3");GError *error=NULL;GPtrArray *list=insight_custom_commands_load(path,&error);g_assert_no_error(error);g_assert_cmpuint(list->len,==,2);g_assert_cmpstr(((InsightCustomCommand *)g_ptr_array_index(list,0))->name,==,"Numbered argument");g_ptr_array_unref(list);click("insight-custom-command-run");phase=2;return G_SOURCE_CONTINUE;
 }
 if(phase==2) {if(busy()) return G_SOURCE_CONTINUE;gchar *text=output("insight-custom-command-output");g_assert_nonnull(strstr(text,"argument with spaces; $(literal)"));g_free(text);click("insight-custom-command-output-1");gchar *clip=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_nonnull(strstr(clip,"argument with spaces"));g_free(clip);unchanged();gtk_notebook_set_current_page(GTK_NOTEBOOK(named("insight-custom-command-tabs")),0);phase=7;return G_SOURCE_CONTINUE;
 }
 if(phase==7) {
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named("insight-custom-command-title"))),==,"Custom Commands");g_assert_true(gtk_widget_get_mapped(named("insight-custom-command-title")));g_assert_cmpint(gtk_widget_get_allocated_width(named("insight-custom-command-title")),>,20);
  const gchar *capture=g_getenv("INSIGHT_CUSTOM_COMMAND_CAPTURE");if(capture) {GdkWindow *native=gtk_widget_get_window(tool);GtkWidget *body=gtk_bin_get_child(GTK_BIN(tool)),*header=gtk_window_get_titlebar(GTK_WINDOW(tool));gint bx,by,hx,hy;g_assert_true(gtk_widget_translate_coordinates(body,tool,0,0,&bx,&by));g_assert_true(gtk_widget_translate_coordinates(header,tool,0,0,&hx,&hy));gint x=MIN(bx,hx),y=MIN(by,hy),right=MAX(bx+gtk_widget_get_allocated_width(body),hx+gtk_widget_get_allocated_width(header)),bottom=MAX(by+gtk_widget_get_allocated_height(body),hy+gtk_widget_get_allocated_height(header));GdkPixbuf *pix=gdk_pixbuf_get_from_window(native,x,y,right-x,bottom-y);g_assert_nonnull(pix);g_assert_true(gdk_pixbuf_save(pix,capture,"jpeg",NULL,"quality","86",NULL));g_object_unref(pix);}
  set("insight-custom-command-template","/bin/sh -c 'exec sleep 30'");click("insight-custom-command-run");phase=3;return G_SOURCE_CONTINUE;
 }
 if(phase==3) {g_assert_true(busy());click("insight-custom-command-stop");phase=4;return G_SOURCE_CONTINUE;}
 if(phase==4) {if(busy()) return G_SOURCE_CONTINUE;set("insight-custom-command-template","/bin/sh -c 'exec yes 0123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789'");click("insight-custom-command-run");phase=5;return G_SOURCE_CONTINUE;}
 if(phase==5) {if(busy()) return G_SOURCE_CONTINUE;gchar *text=output("insight-custom-command-output");g_assert_cmpuint(strlen(text),>=,4*1024*1024);g_assert_cmpuint(strlen(text),<,4*1024*1024+1000);g_free(text);unchanged();
  set("insight-custom-command-template","/bin/sh -c 'exec sleep 30'");click("insight-custom-command-run");gtk_widget_destroy(tool);tool=NULL;phase=6;return G_SOURCE_CONTINUE;
 }
 if(phase==6) {if(ticks<12) return G_SOURCE_CONTINUE;gtk_menu_item_activate(GTK_MENU_ITEM(find(window,"insight-custom-commands-menu")));tool=top();g_assert_nonnull(tool);g_assert_false(busy());g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named("insight-custom-command-name"))),==,"Numbered argument");click("insight-custom-command-edit-2");GError *error=NULL;GPtrArray *list=insight_custom_commands_load(path,&error);g_assert_no_error(error);g_assert_cmpuint(list->len,==,1);g_assert_cmpstr(((InsightCustomCommand *)g_ptr_array_index(list,0))->name,==,"Fixture compiler");g_ptr_array_unref(list);unchanged();gtk_widget_destroy(tool);tool=NULL;g_print("Native custom commands: context preview, literal argument boundaries, stderr/exit, profile persistence/order/delete, clipboard, stop, output limit and close while running passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;}
 return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;GError *error=NULL;gchar *dir=g_dir_make_tmp("insight-custom-gui-XXXXXX",&error);g_assert_no_error(error);profile=g_build_filename(dir,"profile",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);source=g_build_filename(dir,"source with spaces;$(literal).c",NULL);g_assert_true(g_file_set_contents(source,"int main(void) { return 0; }\n",-1,&error));path=g_build_filename(profile,"insight","custom-commands.conf",NULL);gchar *conf=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(conf,"[geany]\nconfirm_exit=false\n",-1,&error));g_free(conf);g_free(dir);
 gchar *args[]={"custom-commands-gui","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(150,step,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(5,args);g_free(profile);g_free(source);g_free(path);return result;
}
