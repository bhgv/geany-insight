/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#undef SSM
#define SSM(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *directory,*profile,*source,*other;static GtkWidget *window;static GeanyDocument *doc,*second;static guint phase,edit_mode;static gboolean restarted;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{ if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *found=named(menu,name);if(found) return found; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
static GtkWidget *dialog(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-language-dialog")) found=p->data;g_list_free(windows);return found; }
static void spin(GtkWidget *d,const gchar *id,gint value) { gchar *name=g_strconcat("insight-filetype-editing-",id,NULL);GtkWidget *w=named(d,name);g_assert_nonnull(w);gtk_spin_button_set_value(GTK_SPIN_BUTTON(w),value);g_free(name); }
static void toggle(GtkWidget *d,const gchar *id,gboolean value) { gchar *name=g_strconcat("insight-filetype-editing-",id,NULL);GtkWidget *w=named(d,name);g_assert_nonnull(w);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),value);g_free(name); }
static gboolean close_dialog(gpointer data) { (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);return G_SOURCE_REMOVE; }
static gboolean edit_dialog(gpointer data)
{
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;g_assert_cmpstr(gtk_combo_box_get_active_id(GTK_COMBO_BOX(named(d,"insight-language-profile"))),==,"Editing Test");toggle(d,"enabled",edit_mode!=2);spin(d,"tab",8);spin(d,"indent",2);spin(d,"margin",41);gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-filetype-editing-type")),2);gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-filetype-editing-auto")),0);toggle(d,"soft-wrap",TRUE);toggle(d,"hard-wrap",TRUE);toggle(d,"line-numbers",FALSE);toggle(d,"white-space",TRUE);toggle(d,"line-endings",TRUE);toggle(d,"guides",FALSE);toggle(d,"right-margin",TRUE);
 if(!edit_mode) gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CLOSE);else { g_timeout_add(100,close_dialog,NULL);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY); }return G_SOURCE_REMOVE;
}
static void edit(guint mode) { edit_mode=mode;g_timeout_add(30,edit_dialog,NULL);GtkWidget *item=named(window,"insight-filetype-options");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void assert_options(void)
{
 ScintillaObject *sci=doc->editor->sci;const GeanyIndentPrefs *p=editor_get_indent_prefs(doc->editor);g_assert_cmpint(p->type,==,GEANY_INDENT_TYPE_BOTH);g_assert_cmpint(p->width,==,2);g_assert_cmpint(p->hard_tab_width,==,8);g_assert_cmpint(p->auto_indent_mode,==,GEANY_AUTOINDENT_NONE);g_assert_cmpint(SSM(sci,SCI_GETTABWIDTH,0,0),==,8);g_assert_cmpint(SSM(sci,SCI_GETINDENT,0,0),==,2);g_assert_cmpint(SSM(sci,SCI_GETUSETABS,0,0),==,1);g_assert_true(doc->editor->line_wrapping);g_assert_true(doc->editor->line_breaking);g_assert_cmpint(SSM(sci,SCI_GETWRAPMODE,0,0),==,SC_WRAP_WORD);g_assert_cmpint(SSM(sci,SCI_GETMARGINWIDTHN,1,0),==,0);g_assert_cmpint(SSM(sci,SCI_GETVIEWWS,0,0),!=,0);g_assert_cmpint(SSM(sci,SCI_GETVIEWEOL,0,0),==,1);g_assert_cmpint(SSM(sci,SCI_GETEDGEMODE,0,0),==,EDGE_LINE);g_assert_cmpint(SSM(sci,SCI_GETEDGECOLUMN,0,0),==,41);
}
static void text_unchanged(void) { gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+2);g_assert_cmpstr(text,==,"int value;\n");g_free(text);g_assert_false(doc->changed); }
static gboolean step(gpointer data)
{
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 if(!phase) {
  doc=document_open_file(source,FALSE,NULL,NULL);g_assert_nonnull(doc);window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));phase=1;
  if(restarted) { assert_options();text_unchanged();g_print("File type editing overrides restored in a fresh editor process\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
  second=document_open_file(other,FALSE,NULL,NULL);g_assert_nonnull(second);doc=document_open_file(source,FALSE,NULL,NULL);doc->editor->indent_type=GEANY_INDENT_TYPE_SPACES;doc->editor->indent_width=3;SSM(doc->editor->sci,SCI_SETUSETABS,0,0);SSM(doc->editor->sci,SCI_SETTABWIDTH,3,0);SSM(doc->editor->sci,SCI_SETINDENT,3,0);doc->editor->auto_indent=TRUE;doc->editor->line_breaking=FALSE;doc->editor->line_wrapping=FALSE;SSM(doc->editor->sci,SCI_SETWRAPMODE,SC_WRAP_NONE,0);SSM(doc->editor->sci,SCI_SETVIEWWS,SCWS_VISIBLEAFTERINDENT,0);SSM(doc->editor->sci,SCI_SETEDGEMODE,EDGE_BACKGROUND,0);SSM(doc->editor->sci,SCI_SETEDGECOLUMN,77,0);edit(0);g_assert_cmpint(editor_get_indent_prefs(doc->editor)->width,==,3);g_assert_false(doc->editor->line_breaking);text_unchanged();
 } else if(phase==1) {
  phase=2;gint other_tab=SSM(second->editor->sci,SCI_GETTABWIDTH,0,0);edit(1);assert_options();g_assert_cmpint(SSM(second->editor->sci,SCI_GETTABWIDTH,0,0),==,other_tab);text_unchanged();g_assert_false(SSM(doc->editor->sci,SCI_CANUNDO,0,0));
  sci_set_current_position(doc->editor->sci,0,FALSE);keybindings_send_command(GEANY_KEY_GROUP_FORMAT,GEANY_KEYS_FORMAT_INCREASEINDENT);gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+2);g_assert_true(g_str_has_prefix(text,"  int"));g_free(text);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_UNDO);text_unchanged();g_assert_true(SSM(doc->editor->sci,SCI_CANREDO,0,0));edit(2);g_assert_cmpint(editor_get_indent_prefs(doc->editor)->width,==,3);g_assert_cmpint(editor_get_indent_prefs(doc->editor)->type,==,GEANY_INDENT_TYPE_SPACES);g_assert_true(doc->editor->auto_indent);g_assert_false(doc->editor->line_wrapping);g_assert_false(doc->editor->line_breaking);g_assert_cmpint(SSM(doc->editor->sci,SCI_GETVIEWWS,0,0),==,SCWS_VISIBLEAFTERINDENT);g_assert_cmpint(SSM(doc->editor->sci,SCI_GETEDGEMODE,0,0),==,EDGE_BACKGROUND);g_assert_cmpint(SSM(doc->editor->sci,SCI_GETEDGECOLUMN,0,0),==,77);text_unchanged();g_assert_true(SSM(doc->editor->sci,SCI_CANREDO,0,0));edit(1);assert_options();text_unchanged();g_print("Native file type editing: current profile, Cancel/Apply, real indent and Undo, per-file isolation, restoration and source preservation passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("File type editing GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 restarted=argc==3 && !strcmp(argv[1],"--restart");directory=restarted ? g_strdup(argv[2]) : g_dir_make_tmp("insight-filetype-editing-XXXXXX",NULL);profile=g_build_filename(directory,"profile",NULL);source=g_build_filename(directory,"test.editing",NULL);other=g_build_filename(directory,"other.py",NULL);
 if(!restarted) { gchar *folder=g_build_filename(profile,"languages","Editing Test",NULL),*config=g_build_filename(folder,"language.conf",NULL),*css=g_build_filename(folder,"style.css",NULL),*preferences=g_build_filename(profile,"geany.conf",NULL);g_assert_cmpint(g_mkdir_with_parents(folder,0700),==,0);g_assert_true(g_file_set_contents(config,"[language]\nname=Editing Test\nparser=C++\nfile-filters=*.editing\nextensions=editing\ncompatible-extensions=\nkeywords=\ntheme=current\n",-1,NULL));g_assert_true(g_file_set_contents(css,"",0,NULL));g_assert_true(g_file_set_contents(preferences,"[geany]\nconfirm_exit=false\n",-1,NULL));g_assert_true(g_file_set_contents(source,"int value;\n",-1,NULL));g_assert_true(g_file_set_contents(other,"x = 1\n",-1,NULL));g_free(folder);g_free(config);g_free(css);g_free(preferences); }
 gchar *args[]={"filetype-editing-native","--new-instance","--no-session","--config",profile,source,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(25,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);
 if(!restarted) { gchar *executable=g_canonicalize_filename(argv[0],NULL),*child[]={executable,"--restart",directory,NULL};GError *error=NULL;gint status;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);g_free(executable); }
 g_free(directory);g_free(profile);g_free(source);g_free(other);return result;
}
