/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static const gchar *content="int apricot_count;\ndouble apricot_ratio;\nint apricot_compute(int value) { return value; }\nvoid demo() {\n    \n}\n";
static gchar *source,*profile,*original;static GeanyDocument *doc;static gint start;static guint phase;
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *v=named(menu,name);if(v) return v; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *result=NULL;for(GList *p=children;p && !result;p=p->next) result=named(p->data,name);g_list_free(children);return result; }return NULL; }
static GtkWidget *popup(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-completion-popup") && gtk_widget_get_visible(p->data)) result=p->data;g_list_free(windows);return result; }
static GtkWidget *list(void) { GtkWidget *p=popup();g_assert_nonnull(p);GtkWidget *view=named(p,"insight-completion-list");g_assert_nonnull(view);return view; }
static guint count(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(list())),NULL); }
static void typed(const gchar *text) { for(const gchar *p=text;*p;p++) { gchar one[]={*p,0};SEND(doc->editor->sci,SCI_REPLACESEL,0,(sptr_t)one);SEND(doc->editor->sci,SCI_COLOURISE,0,-1);SCNotification nt={0};nt.nmhdr.code=SCN_CHARADDED;nt.ch=*p;g_signal_emit_by_name(doc->editor->sci,"sci-notify",0,&nt); } }
static void key(guint value) { ScintillaObject *sci=doc->editor->sci;GdkEvent *e=gdk_event_new(GDK_KEY_PRESS);e->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));e->key.send_event=TRUE;e->key.keyval=value;gboolean handled=FALSE;g_signal_emit_by_name(sci,"key-press-event",&e->key,&handled);g_assert_true(handled);gdk_event_free(e); }
static gchar *selected(void) { GtkTreeIter iter;GtkTreeModel *model;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list())),&model,&iter));gchar *name;gtk_tree_model_get(model,&iter,0,&name,-1);return name; }
static void prefix_is(const gchar *expected) { gchar *value=sci_get_contents_range(doc->editor->sci,start,sci_get_current_position(doc->editor->sci));g_assert_cmpstr(value,==,expected);g_free(value); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;g_printerr("completion test phase %u popup=%d\n",phase,popup()!=NULL);
 if(phase==0) { doc=document_open_file(source,FALSE,NULL,NULL);g_assert_nonnull(doc);if(!doc->tm_file || doc->tm_file->tags_array->len<4) return G_SOURCE_CONTINUE;start=strstr(content,"    \n")-content+4;sci_set_current_position(doc->editor->sci,start,FALSE);SEND(doc->editor->sci,SCI_EMPTYUNDOBUFFER,0,0);typed("ap");g_assert_null(popup());typed("r");phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) {
  g_assert_cmpuint(count(),==,3);GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(list()));GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));gboolean signature=FALSE;
  do { gchar *name,*markup,*icon,*kind,*details;gtk_tree_model_get(model,&iter,0,&name,1,&markup,2,&icon,3,&kind,4,&details,-1);g_assert_nonnull(strstr(markup,"foreground"));g_assert_nonnull(icon);g_assert_true(*kind!=0);if(!strcmp(name,"apricot_compute")) { g_assert_nonnull(strstr(markup,"int value"));g_assert_nonnull(strstr(details,"int value"));signature=TRUE; }g_free(name);g_free(markup);g_free(icon);g_free(kind);g_free(details); }while(gtk_tree_model_iter_next(model,&iter));g_assert_true(signature);
  typed("icot_c");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==2) { g_assert_cmpuint(count(),==,2);SEND(doc->editor->sci,SCI_DELETEBACK,0,0);phase++;return G_SOURCE_CONTINUE; }
 if(phase==3) { g_assert_cmpuint(count(),==,3);gchar *before=selected();key(GDK_KEY_Down);gchar *after=selected();g_assert_cmpstr(before,!=,after);g_free(before);key(GDK_KEY_Up);g_assert_cmpuint(count(),==,3);key(GDK_KEY_Down);key(GDK_KEY_Return);g_assert_null(popup());prefix_is(after);g_free(after);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_UNDO);prefix_is("apricot_");keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_AUTOCOMPLETE);phase++;return G_SOURCE_CONTINUE; }
 if(phase==4) { g_assert_cmpuint(count(),==,3);key(GDK_KEY_Escape);g_assert_null(popup());prefix_is("apricot_");keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_AUTOCOMPLETE);phase++;return G_SOURCE_CONTINUE; }
 if(phase==5) { GtkWidget *view=list();GtkTreePath *path=gtk_tree_path_new_from_indices(2,-1);GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(view),0);GdkRectangle rect;gtk_tree_view_get_cell_area(GTK_TREE_VIEW(view),path,column,&rect);GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));GtkTreeIter iter;g_assert_true(gtk_tree_model_get_iter(model,&iter,path));gchar *name;gtk_tree_model_get(model,&iter,0,&name,-1);GdkEventButton event={0};event.type=GDK_BUTTON_PRESS;event.button=1;event.x=rect.x+10;event.y=rect.y+rect.height/2;gboolean handled=FALSE;g_signal_emit_by_name(view,"button-press-event",&event,&handled);g_assert_true(handled);g_assert_null(popup());prefix_is(name);g_free(name);gtk_tree_path_free(path);
  keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_UNDO);prefix_is("apricot_");keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_AUTOCOMPLETE);phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==6) { g_assert_nonnull(popup());GtkWidget *window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)),*option=named(window,"insight-detailed-completion-option");g_assert_nonnull(option);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),FALSE);g_assert_null(popup());keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_AUTOCOMPLETE);g_assert_true(SEND(doc->editor->sci,SCI_AUTOCACTIVE,0,0));SEND(doc->editor->sci,SCI_AUTOCCANCEL,0,0);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),TRUE);
  gchar *settings=g_build_filename(profile,"insight-completion.conf",NULL);GKeyFile *config=g_key_file_new();g_assert_true(g_key_file_load_from_file(config,settings,0,NULL));g_assert_true(g_key_file_get_boolean(config,"completion","detailed",NULL));g_key_file_unref(config);g_free(settings);
  SEND(doc->editor->sci,SCI_SETTEXT,0,(sptr_t)content);SEND(doc->editor->sci,SCI_SETSAVEPOINT,0,0);gchar *disk=NULL;g_assert_true(g_file_get_contents(source,&disk,NULL,NULL));g_assert_cmpstr(disk,==,content);g_free(disk);g_print("Detailed completion: threshold, colored metadata/icons, live filtering/backspace, arrows/Enter, mouse, Escape, Undo, classic fallback and persistence passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Completion GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) { (void)argc;(void)argv;g_setenv("GEANY_COMPLETION_DEBUG","1",TRUE);if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;original=g_dir_make_tmp("insight-completion-XXXXXX",NULL);profile=g_build_filename(original,"profile",NULL);source=g_build_filename(original,"completion.cpp",NULL);g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *prefs=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(prefs,"[geany]\nconfirm_exit=false\nsymbolcompletion_min_chars=3\nauto_complete_symbols=true\n",-1,NULL));g_assert_true(g_file_set_contents(source,content,-1,NULL));gchar *args[]={"completion-native","--new-instance","--no-session","--config",profile,source,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);g_free(prefs);g_free(profile);g_free(source);g_free(original);return result; }
