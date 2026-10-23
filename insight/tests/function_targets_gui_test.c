/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real editor/index/Symbol Info; closed headers, overloads and language guard. */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include "keybindings.h"
#include <glib/gstdio.h>
#include <string.h>
static GtkWidget *window;static GeanyDocument *source;
static gchar *cpp,*header,*header_text;static guint phase;static gint origin;
static const gchar *code="#include \"api.hpp\"\nnamespace api {\nint work(int value) { return value+1; }\ndouble work(double value) { return value+2; }\nAlias make_cell() { return {}; }\nint use() { return work(3); }\n}\nint overload(double value) { return (int)value; }\n// work in comment\nconst char *hint = \"work\";\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) { if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=c;p && !found;p=p->next) found=named(p->data,name);g_list_free(c);return found; }return NULL; }
static GtkWidget *info(void) { GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),"insight-symbol-info-window")) found=p->data;g_list_free(list);return found; }
static GtkWidget *button(GtkWidget *w,const gchar *label) { if(GTK_IS_BUTTON(w) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(w)),label)) return w;if(GTK_IS_CONTAINER(w)) { GList *c=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=c;p && !found;p=p->next) found=button(p->data,label);g_list_free(c);return found; }return NULL; }
static void click(const gchar *name) { GtkWidget *w=named(info(),name);g_assert_nonnull(w);g_assert_true(gtk_widget_get_sensitive(w));gtk_button_clicked(GTK_BUTTON(w)); }
static void refresh(void) { gtk_button_clicked(GTK_BUTTON(button(info(),"Refresh"))); }
static guint count(void) { GtkWidget *w=info();if(!w) return 0;return gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(named(w,"insight-symbol-info-candidates"))),NULL); }
static void query(const gchar *text,gint kind) { gtk_combo_box_set_active(GTK_COMBO_BOX(named(info(),"insight-symbol-info-kind")),kind);gtk_entry_set_text(GTK_ENTRY(named(info(),"insight-symbol-info-query")),text);refresh(); }
static void command(const gchar *name) { GtkWidget *navigate=g_object_get_data(G_OBJECT(window),"insight-navigate");GtkWidget *item=named(gtk_menu_item_get_submenu(GTK_MENU_ITEM(navigate)),name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static void caret(const gchar *needle) { const gchar *at=strstr(code,needle);g_assert_nonnull(at);origin=(gint)(at-code);sci_set_selection(source->editor->sci,origin,origin);gtk_widget_grab_focus(GTK_WIDGET(source->editor->sci)); }
static void same_source(void) { g_assert_true(document_get_current()==source);g_assert_cmpint(sci_get_current_position(source->editor->sci),==,origin);gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,code);g_free(text);g_assert_false(source->changed);g_assert_false(scintilla_send_message(source->editor->sci,SCI_CANUNDO,0,0)); }
static void close_info(void) { if(info()) gtk_widget_destroy(info()); }
static gboolean step(gpointer data) {
 (void)data;if(!window) { source=document_get_current();if(!source || g_strcmp0(source->file_name,cpp)) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));caret("work(3)");command("insight-jump-prototype-command");return G_SOURCE_CONTINUE; }
 if(phase==0) {
  if(count()!=2) { refresh();return G_SOURCE_CONTINUE; }
  g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(named(info(),"insight-symbol-info-kind"))),==,1);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(info(),"insight-symbol-info-count"))),==,"2 / 2");same_source();g_assert_null(document_find_by_filename(header));
  click("insight-symbol-info-implementation");g_assert_cmpuint(count(),==,2);g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(info(),"insight-symbol-info-query"))),==,"api::work");same_source();
  click("insight-symbol-info-prototype");g_assert_cmpuint(count(),==,2);same_source();query("work",0);g_assert_cmpuint(count(),==,4);query("work",2);g_assert_cmpuint(count(),==,2);phase++;
 } else if(phase==1) {
  query("overload",2);if(count()!=1) { refresh();return G_SOURCE_CONTINUE; }g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(info(),"insight-symbol-info-count"))),==,"1 / 1");same_source();
  query("overload",1);g_assert_cmpuint(count(),==,512);g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(info(),"insight-symbol-info-count"))),==,"512 / 600");same_source();
  query("N599::overload",1);g_assert_cmpuint(count(),==,1);query("overload",4);g_assert_cmpuint(count(),==,0);g_assert_false(gtk_widget_get_sensitive(named(info(),"insight-symbol-info-prototype")));g_assert_false(gtk_widget_get_sensitive(named(info(),"insight-symbol-info-base")));same_source();phase++;
 } else if(phase==2) {
  query("api::instance",4);g_assert_cmpuint(count(),==,1);g_assert_false(gtk_widget_get_sensitive(named(info(),"insight-symbol-info-prototype")));click("insight-symbol-info-base");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(info(),"insight-symbol-info-query"))),==,"api::Cell");g_assert_cmpuint(count(),==,1);same_source();
  query("api::make_cell",2);g_assert_cmpuint(count(),==,1);click("insight-symbol-info-base");g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(info(),"insight-symbol-info-query"))),==,"api::Cell");same_source();
  query("api::LoopA",3);g_assert_cmpuint(count(),==,1);g_assert_false(gtk_widget_get_sensitive(named(info(),"insight-symbol-info-base")));same_source();phase++;
 } else if(phase==3) {
  query("api::work",1);g_assert_cmpuint(count(),==,2);gchar *changed=g_strconcat(header_text,"\n// disk changed\n",NULL);g_assert_true(g_file_set_contents(header,changed,-1,NULL));g_free(changed);
  gtk_button_clicked(GTK_BUTTON(button(info(),"Jump")));g_assert_nonnull(info());g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(info(),"insight-symbol-info-status"))),"Source changed"));same_source();
  refresh();gtk_button_clicked(GTK_BUTTON(button(info(),"Jump")));g_assert_null(info());g_assert_cmpstr(document_get_current()->file_name,==,header);keybindings_send_command(GEANY_KEY_GROUP_GOTO,GEANY_KEYS_GOTO_BACK);same_source();phase++;
 } else if(phase==4) {
  caret("work in comment");command("insight-jump-prototype-command");g_assert_null(info());same_source();caret("work\"");command("insight-jump-implementation-command");g_assert_null(info());same_source();
  caret("work(3)");command("insight-jump-implementation-command");g_assert_cmpuint(count(),==,2);close_info();same_source();
  command("insight-symbol-info-command");g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(named(info(),"insight-symbol-info-kind"))),==,0);close_info();same_source();
  g_print("Function targets: prototype/implementation filters, scoped peers, language guard, filtering before 512 cap, counters, nominal alias/return links, cycles, stale-source refusal, jump/back and comment/string guards passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
 }
 return G_SOURCE_CONTINUE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Function target GUI timeout phase %u count %u",phase,count());return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("insight-targets-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL),*settings_dir=g_build_filename(profile,"insight",NULL);g_assert_cmpint(g_mkdir_with_parents(settings_dir,0700),==,0);
 cpp=g_build_filename(dir,"z_impl.cpp",NULL);header=g_build_filename(dir,"api.hpp",NULL);header_text=g_strdup("namespace api {\nstruct Cell { int value; };\nusing Alias = Cell;\nextern Alias instance;\nint work(int value);\ndouble work(double value);\nusing LoopA = LoopB;\nusing LoopB = LoopA;\n}\n");g_assert_true(g_file_set_contents(cpp,code,-1,NULL));g_assert_true(g_file_set_contents(header,header_text,-1,NULL));
 gchar *bulk=g_build_filename(dir,"bulk.hpp",NULL);GString *prototypes=g_string_new(NULL);for(guint i=0;i<600;i++) g_string_append_printf(prototypes,"namespace N%03u { int overload(int value); }\n",i);g_assert_true(g_file_set_contents(bulk,prototypes->str,prototypes->len,NULL));g_string_free(prototypes,TRUE);g_free(bulk);
 gchar *foreign=g_build_filename(dir,"foreign.py",NULL);g_assert_true(g_file_set_contents(foreign,"def work(value):\n    return value\n",-1,NULL));g_free(foreign);
 gchar *encoded=g_base64_encode((const guchar *)dir,strlen(dir)),*seed=g_strdup_printf("[workbench]\nversion=1\nfollow=false\nrelation-follow=false\nproject-visible=true\nroot-base64=%s\n",encoded),*settings=g_build_filename(settings_dir,"workbench.conf",NULL);g_assert_true(g_file_set_contents(settings,seed,-1,NULL));g_free(encoded);g_free(seed);g_free(settings);
 gchar *conf=g_build_filename(profile,"geany.conf",NULL);g_assert_true(g_file_set_contents(conf,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(conf);
 gchar *args[]={"function-targets","--new-instance","--no-session","--config",profile,cpp,NULL};g_timeout_add(200,step,NULL);g_timeout_add_seconds(45,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);gint result=main_lib(6,args);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(cpp,&disk,NULL,NULL));g_assert_cmpstr(disk,==,code);g_free(disk);return result;
}
