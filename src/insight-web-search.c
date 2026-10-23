/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-web-search.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "utils.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "../insight/desktop/web_search.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
typedef struct { gchar *name,*url; } Engine;
static GPtrArray *engines;static guint selected;static GtkWidget *search,*query,*choice,*preview,*status,*manage,*tree,*name_entry,*url_entry,*manager_status;static GtkListStore *model;static gboolean refreshing,alive;
static void update(void);
static void engine_free(gpointer data) { Engine *e=data;g_free(e->name);g_free(e->url);g_free(e); }
static void append(const gchar *name,const gchar *url) { Engine *e=g_new0(Engine,1);e->name=g_strdup(name);e->url=g_strdup(url);g_ptr_array_add(engines,e); }
static gchar *settings_path(void) { return g_build_filename(app->configdir,"insight","web-search.conf",NULL); }
static void save(void) {
 GKeyFile *key=g_key_file_new();g_key_file_set_integer(key,"search","selected",selected);g_key_file_set_integer(key,"search","count",engines->len);
 for(guint i=0;i<engines->len;i++) { Engine *e=g_ptr_array_index(engines,i);gchar *group=g_strdup_printf("engine-%u",i);g_key_file_set_string(key,group,"name",e->name);g_key_file_set_string(key,group,"url",e->url);g_free(group); }
 gchar *path=settings_path(),*dir=g_path_get_dirname(path);gsize length;gchar *text=g_key_file_to_data(key,&length,NULL);if(g_mkdir_with_parents(dir,0700) || !g_file_set_contents(path,text,length,NULL)) ui_set_statusbar(TRUE,"%s",insight_tr("Cannot save web search settings"));g_free(text);g_free(dir);g_free(path);g_key_file_unref(key);
}
static void choices(void) {
 if(!choice) return;refreshing=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(choice));for(guint i=0;i<engines->len;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(choice),((Engine *)g_ptr_array_index(engines,i))->name);gtk_combo_box_set_active(GTK_COMBO_BOX(choice),selected);refreshing=FALSE;update();
}
static gchar *url_for_query(GError **error) { if(!query || selected>=engines->len) return NULL;Engine *e=g_ptr_array_index(engines,selected);return insight_web_search_url(e->url,gtk_entry_get_text(GTK_ENTRY(query)),error); }
static void update(void) {
 if(!search || refreshing) return;GError *error=NULL;gchar *url=url_for_query(&error);gtk_entry_set_text(GTK_ENTRY(preview),url ? url : "");gtk_label_set_text(GTK_LABEL(status),error ? insight_tr(error->message) : "");gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(search),GTK_RESPONSE_ACCEPT),url!=NULL);gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(search),101),url!=NULL);g_free(url);g_clear_error(&error);
}
static void query_changed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;update(); }
static void engine_changed(GtkComboBox *combo,gpointer data) { (void)data;if(refreshing) return;gint i=gtk_combo_box_get_active(combo);if(i>=0 && (guint)i<engines->len) { selected=i;save(); }update(); }
static void search_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;search=query=choice=preview=status=NULL; }
static void search_response(GtkDialog *dialog,gint response,gpointer data) {
 (void)data;if(response==GTK_RESPONSE_ACCEPT || response==101) { GError *error=NULL;gchar *url=url_for_query(&error);if(url) { save();if(response==101) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),url,-1);else utils_open_browser(url);g_free(url); }else { gtk_label_set_text(GTK_LABEL(status),error ? insight_tr(error->message) : insight_tr("Select a search engine")); }g_clear_error(&error);return; }
 if(response==102) { insight_web_search_command(1);return; }gtk_widget_destroy(GTK_WIDGET(dialog));
}
static gchar *source_query(void) {
 GeanyDocument *doc=document_get_current();if(!DOC_VALID(doc)) return g_strdup("");ScintillaObject *sci=doc->editor->sci;
 if(SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || sci_get_selection_mode(sci)!=SC_SEL_STREAM || SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) || SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0)) return g_strdup("");
 gint a=SEND(sci,SCI_GETSELECTIONSTART,0,0),b=SEND(sci,SCI_GETSELECTIONEND,0,0);if(a==b) { a=SEND(sci,SCI_WORDSTARTPOSITION,a,TRUE);b=SEND(sci,SCI_WORDENDPOSITION,b,TRUE); }if(b<=a || b-a>4096) return g_strdup("");gchar *text=sci_get_contents_range(sci,a,b);if(!g_utf8_validate(text,-1,NULL) || memchr(text,0,b-a)) { g_free(text);return g_strdup(""); }for(const gchar *p=text;*p;p=g_utf8_next_char(p)) if(g_unichar_iscntrl(g_utf8_get_char(p))) { g_free(text);return g_strdup(""); }return text;
}
static void show_search(void) {
 gchar *text=source_query();if(search) { gtk_entry_set_text(GTK_ENTRY(query),text);g_free(text);gtk_window_present(GTK_WINDOW(search));return; }
 search=gtk_dialog_new_with_buttons(insight_tr("Search Web"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Search Engines…"),102,insight_tr("Copy URL"),101,insight_tr("Search"),GTK_RESPONSE_ACCEPT,NULL);gtk_widget_set_name(search,"insight-web-search-window");gtk_window_set_default_size(GTK_WINDOW(search),650,240);GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(search));gtk_container_set_border_width(GTK_CONTAINER(box),8);
 query=gtk_entry_new();gtk_entry_set_max_length(GTK_ENTRY(query),4096);gtk_widget_set_name(query,"insight-web-query");gtk_entry_set_text(GTK_ENTRY(query),text);g_free(text);gtk_entry_set_activates_default(GTK_ENTRY(query),TRUE);gtk_dialog_set_default_response(GTK_DIALOG(search),GTK_RESPONSE_ACCEPT);
 choice=gtk_combo_box_text_new();gtk_widget_set_name(choice,"insight-web-engine");preview=gtk_entry_new();gtk_widget_set_name(preview,"insight-web-preview");gtk_editable_set_editable(GTK_EDITABLE(preview),FALSE);status=gtk_label_new("");gtk_label_set_line_wrap(GTK_LABEL(status),TRUE);GtkWidget *items[]={query,choice,preview,status};for(guint i=0;i<G_N_ELEMENTS(items);i++) gtk_box_pack_start(GTK_BOX(box),items[i],FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(box),insight_panel_help_button("web-search.md"),FALSE,FALSE,0);
 g_signal_connect(query,"changed",G_CALLBACK(query_changed),NULL);g_signal_connect(choice,"changed",G_CALLBACK(engine_changed),NULL);g_signal_connect(search,"response",G_CALLBACK(search_response),NULL);g_signal_connect(search,"destroy",G_CALLBACK(search_destroyed),NULL);choices();gtk_widget_show_all(search);gtk_widget_grab_focus(query);
}
static gint manager_selection(void) { GtkTreeIter iter;GtkTreeModel *m=NULL;if(!tree || !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&m,&iter)) return -1;GtkTreePath *path=gtk_tree_model_get_path(m,&iter);gint i=gtk_tree_path_get_indices(path)[0];gtk_tree_path_free(path);return i; }
static void manager_refresh(gint chosen) { gtk_list_store_clear(model);for(guint i=0;i<engines->len;i++) { Engine *e=g_ptr_array_index(engines,i);GtkTreeIter iter;gtk_list_store_append(model,&iter);gtk_list_store_set(model,&iter,0,e->name,1,e->url,-1); }if(chosen>=0 && (guint)chosen<engines->len) { GtkTreePath *path=gtk_tree_path_new_from_indices(chosen,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path); }choices(); }
static void manager_changed(GtkTreeSelection *selection,gpointer data) { (void)selection;(void)data;gint i=manager_selection();if(i>=0 && (guint)i<engines->len) { Engine *e=g_ptr_array_index(engines,i);gtk_entry_set_text(GTK_ENTRY(name_entry),e->name);gtk_entry_set_text(GTK_ENTRY(url_entry),e->url); } }
static void manager_action(GtkButton *button,gpointer data) {
 (void)button;gint action=GPOINTER_TO_INT(data),i=manager_selection();if(action<=1) {
  const gchar *name=gtk_entry_get_text(GTK_ENTRY(name_entry)),*url=gtk_entry_get_text(GTK_ENTRY(url_entry));GError *error=NULL;gchar *test=insight_web_search_url(url,"query",&error);gboolean valid=*name && strlen(name)<=128 && g_utf8_validate(name,-1,NULL);for(const gchar *p=name;valid && *p;p=g_utf8_next_char(p)) valid=!g_unichar_iscntrl(g_utf8_get_char(p));
  if(!test || !valid || (!action && engines->len>=32) || (action==1 && i<0)) { gtk_label_set_text(GTK_LABEL(manager_status),error ? insight_tr(error->message) : insight_tr("Choose an engine and enter a name up to 128 bytes; maximum 32 engines"));g_free(test);g_clear_error(&error);return; }g_free(test);g_clear_error(&error);
  if(!action) { append(name,url);i=engines->len-1; }else { Engine *e=g_ptr_array_index(engines,i);g_free(e->name);g_free(e->url);e->name=g_strdup(name);e->url=g_strdup(url); }
 }else if(i>=0) {
  if(action==2) { if(engines->len==1) return;g_ptr_array_remove_index(engines,i);if(selected>(guint)i) selected--;else if(selected>=(guint)engines->len) selected=engines->len-1;i=MIN(i,(gint)engines->len-1); }
  else { gint next=i+(action==3 ? -1 : 1);if(next<0 || (guint)next>=engines->len) return;gpointer swap=engines->pdata[i];engines->pdata[i]=engines->pdata[next];engines->pdata[next]=swap;if(selected==(guint)i) selected=next;else if(selected==(guint)next) selected=i;i=next; }
 }
 gtk_label_set_text(GTK_LABEL(manager_status),"");save();manager_refresh(i);
}
static void manager_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;manage=tree=name_entry=url_entry=manager_status=NULL;g_clear_object(&model); }
static void show_manager(void) {
 if(manage) { gtk_window_present(GTK_WINDOW(manage));return; }manage=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(manage,"insight-web-engines-window");gtk_window_set_title(GTK_WINDOW(manage),insight_tr("Search Engines"));gtk_window_set_transient_for(GTK_WINDOW(manage),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(manage),TRUE);gtk_window_set_default_size(GTK_WINDOW(manage),720,400);GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,5);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(manage),box);
 model=gtk_list_store_new(2,G_TYPE_STRING,G_TYPE_STRING);tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));gtk_widget_set_name(tree,"insight-web-engines-list");gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,insight_tr("Name"),gtk_cell_renderer_text_new(),"text",0,NULL);gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,"URL (%S)",gtk_cell_renderer_text_new(),"text",1,NULL);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 name_entry=gtk_entry_new();url_entry=gtk_entry_new();gtk_widget_set_name(name_entry,"insight-web-engine-name");gtk_widget_set_name(url_entry,"insight-web-engine-url");gtk_entry_set_placeholder_text(GTK_ENTRY(name_entry),insight_tr("Engine name"));gtk_entry_set_placeholder_text(GTK_ENTRY(url_entry),"https://example.com/search?q=%S");gtk_entry_set_max_length(GTK_ENTRY(name_entry),128);gtk_entry_set_max_length(GTK_ENTRY(url_entry),4096);gtk_box_pack_start(GTK_BOX(box),name_entry,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),url_entry,FALSE,FALSE,0);
 GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);const gchar *labels[]={"Add","Update selected","Remove selected","Move up","Move down"},*names[]={"add","update","remove","up","down"};for(guint i=0;i<5;i++) { GtkWidget *button=gtk_button_new_with_label(insight_tr(labels[i]));gchar *name=g_strconcat("insight-web-engine-",names[i],NULL);gtk_widget_set_name(button,name);g_free(name);gtk_box_pack_start(GTK_BOX(bar),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",G_CALLBACK(manager_action),GINT_TO_POINTER(i)); }gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);manager_status=gtk_label_new("");gtk_label_set_line_wrap(GTK_LABEL(manager_status),TRUE);gtk_box_pack_start(GTK_BOX(box),manager_status,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),insight_panel_help_button("web-search.md"),FALSE,FALSE,0);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),"changed",G_CALLBACK(manager_changed),NULL);g_signal_connect(manage,"destroy",G_CALLBACK(manager_destroyed),NULL);manager_refresh(selected);gtk_widget_show_all(manage);
}
void insight_web_search_command(guint action) { if(!alive) return;if(action==1) show_manager();else show_search(); }
static void command(GtkMenuItem *item,gpointer data) { (void)item;insight_web_search_command(GPOINTER_TO_UINT(data)); }
void insight_web_search_init(void) {
 if(alive) return;alive=TRUE;engines=g_ptr_array_new_with_free_func(engine_free);gchar *path=settings_path();GKeyFile *key=g_key_file_new();GStatBuf st;
 if(!g_stat(path,&st) && st.st_size<=160*1024 && g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL)) {
  gint count=g_key_file_get_integer(key,"search","count",NULL);for(gint i=0;i<count && i<32;i++) { gchar *group=g_strdup_printf("engine-%d",i),*name=g_key_file_get_string(key,group,"name",NULL),*url=g_key_file_get_string(key,group,"url",NULL),*test=insight_web_search_url(url,"query",NULL);if(name && *name && strlen(name)<=128 && g_utf8_validate(name,-1,NULL) && test) append(name,url);g_free(test);g_free(name);g_free(url);g_free(group); }gint index=g_key_file_get_integer(key,"search","selected",NULL);if(index>=0 && (guint)index<engines->len) selected=index;
 }
 if(!engines->len) { append("DuckDuckGo","https://duckduckgo.com/?q=%S");append("Google","https://www.google.com/search?q=%S");append("Bing","https://www.bing.com/search?q=%S"); }g_key_file_unref(key);g_free(path);
 GtkWidget *menus[]={ui_lookup_widget(main_widgets.window,"search1_menu"),main_widgets.editor_menu};const gchar *labels[]={"Search Web…","Search Engines…"};const guint ids[]={GEANY_KEYS_INSIGHT_SEARCHWEB,GEANY_KEYS_INSIGHT_SEARCHENGINES};for(guint m=0;m<2;m++) for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(insight_tr(labels[i]));gchar *name=g_strdup_printf("insight-web-%s-%u",m ? "popup" : "command",i);gtk_widget_set_name(item,name);g_free(name);g_signal_connect(item,"activate",G_CALLBACK(command),GUINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menus[m]),item);if(!m) keybindings_attach_menu(GEANY_KEY_GROUP_SEARCH,ids[i],item);gtk_widget_show(item); }
}
void insight_web_search_finalize(void) { alive=FALSE;if(search) gtk_widget_destroy(search);if(manage) gtk_widget_destroy(manage);g_clear_pointer(&engines,g_ptr_array_unref); }
