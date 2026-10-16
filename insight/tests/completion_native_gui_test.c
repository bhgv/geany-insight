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
static const gchar *content="int apricot_count;\ndouble apricot_ratio;\nint apricot_compute(int value) { return value; }\nint BerryCount;\nint berryCount;\nint BERRY_MODE;\nstruct Basket { int BerryField; int berryField; };\nBasket basket;\nvoid unrelated() { int berryHidden = 0; }\nvoid demo() {\n    \n}\n";
static gchar *source,*profile,*original;static GeanyDocument *doc;static gint start;static guint phase;static gboolean restarted;
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *menu=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(menu) { GtkWidget *v=named(menu,name);if(v) return v; } }if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *result=NULL;for(GList *p=children;p && !result;p=p->next) result=named(p->data,name);g_list_free(children);return result; }return NULL; }
static GtkWidget *popup(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-completion-popup") && gtk_widget_get_visible(p->data)) result=p->data;g_list_free(windows);return result; }
static GtkWidget *list(void) { GtkWidget *p=popup();g_assert_nonnull(p);GtkWidget *view=named(p,"insight-completion-list");g_assert_nonnull(view);return view; }
static guint count(void) { return gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(list())),NULL); }
static void typed(const gchar *text) { for(const gchar *p=text;*p;p++) { gchar one[]={*p,0};SEND(doc->editor->sci,SCI_REPLACESEL,0,(sptr_t)one);SEND(doc->editor->sci,SCI_COLOURISE,0,-1);SCNotification nt={0};nt.nmhdr.code=SCN_CHARADDED;nt.ch=*p;g_signal_emit_by_name(doc->editor->sci,"sci-notify",0,&nt); } }
static void key(guint value) { ScintillaObject *sci=doc->editor->sci;GdkEvent *e=gdk_event_new(GDK_KEY_PRESS);e->key.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(sci)));e->key.send_event=TRUE;e->key.keyval=value;gboolean handled=FALSE;g_signal_emit_by_name(sci,"key-press-event",&e->key,&handled);g_assert_true(handled);gdk_event_free(e); }
static gchar *selected(void) { GtkTreeIter iter;GtkTreeModel *model;g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(list())),&model,&iter));gchar *name;gtk_tree_model_get(model,&iter,0,&name,-1);return name; }
static void prefix_is(const gchar *expected) { gchar *value=sci_get_contents_range(doc->editor->sci,start,sci_get_current_position(doc->editor->sci));g_assert_cmpstr(value,==,expected);g_free(value); }
static GtkWidget *settings_dialog(void) { GList *windows=gtk_window_list_toplevels();GtkWidget *result=NULL;for(GList *p=windows;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-completion-options-dialog")) result=p->data;g_list_free(windows);return result; }
static void setting_toggle(GtkWidget *d,const gchar *id,gboolean active) { gchar *name=g_strconcat("insight-completion-setting-",id,NULL);GtkWidget *w=named(d,name);g_assert_nonnull(w);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(w),active);g_free(name); }
static gboolean settings_action(gpointer data) {
 GtkWidget *d=settings_dialog();if(!d) return G_SOURCE_CONTINUE;guint mode=GPOINTER_TO_UINT(data);
 if(mode==3) { g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(d,"insight-completion-setting-case-sensitive"))));g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(named(d,"insight-completion-setting-threshold"))),==,4);g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(named(d,"insight-completion-setting-rows"))),==,5);g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(d,"insight-completion-setting-accept-tab"))));g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(named(d,"insight-completion-setting-parentheses"))));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE; }
 setting_toggle(d,"accept-tab",FALSE);setting_toggle(d,"comments",mode==0 || mode==1);setting_toggle(d,"case-sensitive",mode!=4);setting_toggle(d,"parentheses",TRUE);setting_toggle(d,"space",TRUE);gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(d,"insight-completion-setting-threshold")),mode==0 ? 12 : 4);gtk_spin_button_set_value(GTK_SPIN_BUTTON(named(d,"insight-completion-setting-rows")),5);gtk_dialog_response(GTK_DIALOG(d),mode==0 ? GTK_RESPONSE_CANCEL : GTK_RESPONSE_OK);return G_SOURCE_REMOVE;
}
static void options(guint mode) { GtkWidget *window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)),*item=named(window,"insight-completion-options");g_assert_nonnull(item);g_timeout_add(30,settings_action,GUINT_TO_POINTER(mode));gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void reset_prefix(const gchar *text) { SEND(doc->editor->sci,SCI_SETTEXT,0,(sptr_t)content);sci_set_current_position(doc->editor->sci,start,FALSE);gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));typed(text); }
static void inserted_is(const gchar *expected) { gchar *value=sci_get_contents_range(doc->editor->sci,start,start+strlen(expected));g_assert_cmpstr(value,==,expected);g_free(value); }
static void finish(void) { GtkWidget *window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));SEND(doc->editor->sci,SCI_SETTEXT,0,(sptr_t)content);SEND(doc->editor->sci,SCI_SETSAVEPOINT,0,0);gchar *disk=NULL;g_assert_true(g_file_get_contents(source,&disk,NULL,NULL));g_assert_cmpstr(disk,==,content);g_free(disk);g_print("Detailed completion settings: Cancel/OK, threshold, Tab fallback, function parentheses/spacing/Undo, existing calls, comments and restart passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1"))); }
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;g_printerr("completion test phase %u popup=%d\n",phase,popup()!=NULL);
 if(phase==0) { doc=document_open_file(source,FALSE,NULL,NULL);g_assert_nonnull(doc);if(!doc->tm_file || doc->tm_file->tags_array->len<4) return G_SOURCE_CONTINUE;gtk_widget_grab_focus(GTK_WIDGET(doc->editor->sci));start=strstr(content,"    \n")-content+4;sci_set_current_position(doc->editor->sci,start,FALSE);SEND(doc->editor->sci,SCI_EMPTYUNDOBUFFER,0,0);typed("ap");g_assert_null(popup());typed(restarted ? "ri" : "r");phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) {
  if(restarted) { g_assert_cmpuint(count(),==,3);options(3);finish();return G_SOURCE_REMOVE; }
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
  options(0);reset_prefix("apr");g_assert_nonnull(popup());key(GDK_KEY_Escape);options(1);reset_prefix("apr");g_assert_null(popup());typed("icot_com");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==7) { g_assert_cmpuint(count(),==,1);key(GDK_KEY_Tab);g_assert_null(popup());gchar *value=sci_get_contents_range(doc->editor->sci,start,sci_get_current_position(doc->editor->sci));g_assert_null(strstr(value,"apricot_compute"));g_free(value);reset_prefix("apricot_com");phase++;return G_SOURCE_CONTINUE; }
 if(phase==8) { g_assert_cmpuint(count(),==,1);key(GDK_KEY_Return);inserted_is("apricot_compute ()");g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,start+(gint)strlen("apricot_compute ("));keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_UNDO);prefix_is("apricot_com");reset_prefix("");SEND(doc->editor->sci,SCI_REPLACESEL,0,(sptr_t)"apricot_com (17)");sci_set_current_position(doc->editor->sci,start+strlen("apricot_com"),FALSE);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_AUTOCOMPLETE);phase++;return G_SOURCE_CONTINUE; }
 if(phase==9) { g_assert_cmpuint(count(),==,1);key(GDK_KEY_Return);inserted_is("apricot_compute (17)");reset_prefix("// apri");phase++;return G_SOURCE_CONTINUE; }
 if(phase==10) { g_assert_cmpuint(count(),==,3);key(GDK_KEY_Escape);options(2);reset_prefix("// apricot_c");phase++;return G_SOURCE_CONTINUE; }
 if(phase==11) { g_assert_null(popup());options(4);reset_prefix("bErR");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==12) { GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(list()));GtkTreeIter iter;gboolean upper=FALSE,lower=FALSE,constant=FALSE;g_assert_true(gtk_tree_model_get_iter_first(model,&iter));do { gchar *name;gtk_tree_model_get(model,&iter,0,&name,-1);if(!strcmp(name,"BerryCount")) upper=TRUE;if(!strcmp(name,"berryCount")) lower=TRUE;if(!strcmp(name,"BERRY_MODE")) constant=TRUE;g_assert_cmpstr(name,!=,"berryHidden");g_free(name); }while(gtk_tree_model_iter_next(model,&iter));g_assert_true(upper);g_assert_true(lower);g_assert_true(constant);key(GDK_KEY_Escape);reset_prefix("berry");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==13) { gchar *name=selected();g_assert_true(g_str_has_prefix(name,"berry"));key(GDK_KEY_Return);prefix_is(name);g_free(name);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_UNDO);prefix_is("berry");options(5);reset_prefix("bErR");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==14) { g_assert_null(popup());options(4);reset_prefix("basket.bEr");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==15) { g_assert_cmpuint(count(),==,2);gchar *name=selected(),*expected=g_strconcat("basket.",name,NULL);key(GDK_KEY_Return);prefix_is(expected);g_free(name);g_free(expected);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_UNDO);prefix_is("basket.bEr");reset_prefix("bErR");phase++;return G_SOURCE_CONTINUE;
 }
 if(phase==16) { g_assert_nonnull(popup());GtkWidget *window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci)),*option=named(window,"insight-detailed-completion-option");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),FALSE);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_AUTOCOMPLETE);g_assert_true(SEND(doc->editor->sci,SCI_AUTOCACTIVE,0,0));g_assert_true(SEND(doc->editor->sci,SCI_AUTOCGETIGNORECASE,0,0));SEND(doc->editor->sci,SCI_AUTOCCANCEL,0,0);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(option),TRUE);finish();return G_SOURCE_REMOVE;
 }return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Completion GUI timeout phase %u",phase);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 restarted=argc==3 && !strcmp(argv[1],"--restart");original=restarted ? g_strdup(argv[2]) : g_dir_make_tmp("insight-completion-XXXXXX",NULL);profile=g_build_filename(original,"profile",NULL);source=g_build_filename(original,"completion.cpp",NULL);
 if(!restarted) { g_assert_cmpint(g_mkdir_with_parents(profile,0700),==,0);gchar *prefs=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(prefs,"[geany]\nconfirm_exit=false\nsymbolcompletion_min_chars=3\nauto_complete_symbols=true\n",-1,NULL));g_assert_true(g_file_set_contents(source,content,-1,NULL));g_free(prefs); }
 gchar *args[]={"completion-native","--new-instance","--no-session","--config",profile,source,NULL};g_timeout_add(250,step,NULL);g_timeout_add_seconds(30,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);
 if(!restarted) { gchar *executable=g_canonicalize_filename(argv[0],NULL),*child[]={executable,"--restart",original,NULL};GError *error=NULL;gint status;g_assert_true(g_spawn_sync(NULL,child,NULL,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);g_free(executable); }
 g_free(profile);g_free(source);g_free(original);return result;
}
