/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "insight-symbol-view.h"
#include "tagmanager/tm_parser.h"
#include <glib/gstdio.h>
#include <string.h>
#include <stdio.h>
static GtkWidget *window,*types,*categories,*type_entry,*category_entry;
static gchar *profile,*root,*source,*other,*python,*custom,*settings;
static guint phase,doc_id; static gint caret,preview;
static GtkWidget *named(GtkWidget *w,const char *name) {
 if(!strcmp(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *sub=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w)); if(sub) { GtkWidget *found=named(sub,name); if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w)); GtkWidget *found=NULL; for(GList *p=children;p && !found;p=p->next) found=named(p->data,name); g_list_free(children); return found; } return NULL;
}
static GtkWidget *toplevel(const char *name) { GList *list=gtk_window_list_toplevels(); GtkWidget *result=NULL; for(GList *p=list;p && !result;p=p->next) if(!strcmp(gtk_widget_get_name(p->data),name)) result=p->data; g_list_free(list); return result; }
static GtkWidget *button(GtkWidget *w,const char *text) {
 if(GTK_IS_BUTTON(w) && !g_strcmp0(gtk_button_get_label(GTK_BUTTON(w)),text)) return w;
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w)); GtkWidget *found=NULL; for(GList *p=list;p && !found;p=p->next) found=button(p->data,text); g_list_free(list); return found; } return NULL;
}
typedef struct { const char *name,*path; gboolean symbols,group; GtkTreePath *row; } Find;
static gboolean find_row(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *it,gpointer data) {
 Find *f=data; gchar *name=NULL,*path=NULL; gboolean group=FALSE; gulong line=0;
 if(f->symbols) { gtk_tree_model_get(model,it,0,&name,2,&path,3,&line,-1); group=!line; } else gtk_tree_model_get(model,it,0,&name,1,&path,2,&group,-1);
 gboolean match=group==f->group && (!f->name || !g_strcmp0(f->name,name)) && (!f->path || !g_strcmp0(f->path,path)); if(match) f->row=gtk_tree_path_copy(row); g_free(name); g_free(path); return match;
}
static GtkTreePath *row(GtkWidget *view,const char *name,const char *path,gboolean group) { Find find={.name=name,.path=path,.symbols=view==categories,.group=group}; gtk_tree_model_foreach(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),find_row,&find); return find.row; }
static void select_row(GtkWidget *view,const char *name,const char *path,gboolean group) { GtkTreePath *p=row(view,name,path,group); g_assert_nonnull(p); gtk_tree_view_expand_to_path(GTK_TREE_VIEW(view),p); gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),p,NULL,FALSE); gtk_tree_path_free(p); }
static void show_panel(GtkWidget *view) { GtkWidget *page=view; while(gtk_widget_get_parent(page) && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page); GtkWidget *book=gtk_widget_get_parent(page); gtk_widget_show(page); gtk_widget_show(book); gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page)); }
static void key(guint value,guint modifiers) { GdkEvent *e=gdk_event_new(GDK_KEY_PRESS); e->key.window=g_object_ref(gtk_widget_get_window(window)); e->key.keyval=value; e->key.state=modifiers; gdk_event_set_device(e,gdk_seat_get_keyboard(gdk_display_get_default_seat(gdk_display_get_default()))); gtk_main_do_event(e); gdk_event_free(e); }
static GtkWidget *popup(GtkWidget *view) { gtk_widget_grab_focus(view); key(GDK_KEY_Menu,0); GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-grouped-popup"); g_assert_nonnull(menu); g_object_ref(menu); gtk_menu_popdown(GTK_MENU(menu)); return menu; }
static GtkWidget *command(GtkWidget *menu,guint action) { gchar *id=g_strdup_printf("insight-grouped-action-%u",action); GtkWidget *item=named(menu,id); g_free(id); g_assert_nonnull(item); return item; }
static void act(GtkWidget *view,guint action) { GtkWidget *menu=popup(view),*item=command(menu,action); g_assert_true(gtk_widget_get_sensitive(item)); gtk_menu_item_activate(GTK_MENU_ITEM(item)); gtk_widget_destroy(menu); g_object_unref(menu); }
static void clip(const char *expected) { gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)); g_assert_cmpstr(text,==,expected); g_free(text); }
static gboolean right_click(GtkWidget *view,const char *name,const char *path) {
 GtkTreePath *target=row(view,name,path,FALSE); g_assert_nonnull(target); gtk_tree_view_expand_to_path(GTK_TREE_VIEW(view),target); GdkRectangle rect; gtk_tree_view_get_cell_area(GTK_TREE_VIEW(view),target,NULL,&rect); GtkTreePath *hit=NULL;
 if(!gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),rect.x+4,rect.y+rect.height/2,&hit,NULL,NULL,NULL) || gtk_tree_path_compare(hit,target)) { if(hit) gtk_tree_path_free(hit); gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(view),target,NULL,TRUE,0.5,0); gtk_tree_path_free(target); return FALSE; }
 gtk_tree_path_free(hit); gtk_tree_path_free(target); GdkEvent *e=gdk_event_new(GDK_BUTTON_PRESS); e->button.window=g_object_ref(gtk_tree_view_get_bin_window(GTK_TREE_VIEW(view))); e->button.button=3; e->button.x=rect.x+4; e->button.y=rect.y+rect.height/2; gdk_event_set_device(e,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default()))); gboolean handled=FALSE; g_signal_emit_by_name(view,"button-press-event",e,&handled); gdk_event_free(e); g_assert_true(handled);
 GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-grouped-popup"); g_assert_nonnull(menu); g_object_ref(menu); gtk_menu_popdown(GTK_MENU(menu)); gtk_widget_destroy(menu); g_object_unref(menu); return TRUE;
}
static void capture(void) { GeanyDocument *doc=document_get_current(); doc_id=doc->id; caret=sci_get_current_position(doc->editor->sci); preview=sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))); }
static void unchanged(void) { GeanyDocument *doc=document_get_current(); g_assert_cmpuint(doc->id,==,doc_id); g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret); g_assert_cmpint(sci_get_current_line(SCINTILLA(named(window,"insight-context-code"))),==,preview); }
static gboolean timeout(gpointer data) { (void)data; g_error("Grouped project GUI timeout at phase %u",phase); return G_SOURCE_REMOVE; }
static gboolean step(gpointer data) {
 (void)data; static guint last=G_MAXUINT; if(last!=phase) { g_printerr("Grouped GUI phase %u\n",phase); last=phase; } GeanyDocument *doc=document_get_current(); if(!doc) return G_SOURCE_CONTINUE;
 if(!window) window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
 if(!phase) { document_open_file(source,FALSE,NULL,NULL); document_open_file(other,FALSE,NULL,NULL); phase++; return G_SOURCE_CONTINUE; }
 if(!types) { types=named(window,"insight-file-types"); categories=named(window,"insight-symbol-categories"); type_entry=named(window,"insight-type-query"); category_entry=named(window,"insight-category-query"); g_assert_nonnull(types); g_assert_nonnull(categories); gtk_window_resize(GTK_WINDOW(window),1200,850); gtk_paned_set_position(GTK_PANED(named(window,"insight-project-split")),650); show_panel(types); }
 if(phase==1) {
  GtkTreePath *p=row(types,"Catalog Native",NULL,TRUE); if(!p) return G_SOURCE_CONTINUE; gtk_tree_path_free(p);
  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(named(window,"insight-type-count"))),"Type files: 4 / 4")) return G_SOURCE_CONTINUE;
  GtkTreeViewColumn *first=gtk_tree_view_get_column(GTK_TREE_VIEW(types),0),*second=gtk_tree_view_get_column(GTK_TREE_VIEW(types),1);
  g_assert_cmpint(gtk_tree_view_column_get_sort_column_id(first),==,1); g_assert_cmpint(gtk_tree_view_column_get_fixed_width(first),==,345); g_assert_false(gtk_tree_view_column_get_visible(first)); g_assert_true(gtk_tree_view_column_get_visible(second));
  select_row(types,"a source.cpp",source,FALSE); phase++;
 } else if(phase==2) {
  capture(); act(types,1); clip("a source.cpp"); act(types,2); clip(source); unchanged();
  if(!right_click(types,"b.cpp",other)) return G_SOURCE_CONTINUE;
  unchanged();
  gtk_entry_set_text(GTK_ENTRY(type_entry),"~fced.special"); phase++;
 } else if(phase==3) {
  GtkTreePath *p=row(types,"forced.special",custom,FALSE); if(!p) return G_SOURCE_CONTINUE; gtk_tree_path_free(p);
  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(named(window,"insight-type-count"))),"Type files: 1 / 4")) return G_SOURCE_CONTINUE;
  unchanged(); gtk_entry_set_text(GTK_ENTRY(type_entry),"?["); phase++;
 } else if(phase==4) {
  if(!gtk_style_context_has_class(gtk_widget_get_style_context(type_entry),"error")) return G_SOURCE_CONTINUE;
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-type-count"))),==,"Type files: 1 / 4"); gtk_widget_grab_focus(type_entry); key(GDK_KEY_Return,0); unchanged(); key(GDK_KEY_Escape,0); phase++;
 } else if(phase==5) {
  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(named(window,"insight-type-count"))),"Type files: 4 / 4")) return G_SOURCE_CONTINUE;
  select_row(types,"C++",NULL,TRUE); capture(); act(types,9); GtkTreePath *p=row(types,"C++",NULL,TRUE); g_assert_false(gtk_tree_view_row_expanded(GTK_TREE_VIEW(types),p)); act(types,8); g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(types),p)); gtk_tree_path_free(p); unchanged();
  GtkWidget *menu=popup(types); g_assert_false(gtk_widget_get_sensitive(command(menu,0))); g_assert_false(gtk_widget_get_sensitive(command(menu,2))); gtk_widget_destroy(menu); g_object_unref(menu);
  act(types,13); g_assert_cmpint(gtk_tree_view_column_get_sort_column_id(gtk_tree_view_get_column(GTK_TREE_VIEW(types),0)),==,0); g_assert_true(gtk_tree_view_column_get_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(types),1)));
  show_panel(categories); phase++;
 } else if(phase==6) {
  guint shown=0,total=0; if(sscanf(gtk_label_get_text(GTK_LABEL(named(window,"insight-category-count"))),"Category symbols: %u / %u",&shown,&total)!=2 || total<6000) return G_SOURCE_CONTINUE;
  g_assert_cmpuint(shown,==,5000); g_assert_true(gtk_widget_get_visible(named(window,"insight-category-more"))); gtk_button_clicked(GTK_BUTTON(named(window,"insight-category-more"))); phase++;
 } else if(phase==7) {
  GtkTreePath *p=row(categories,"f_06000",source,FALSE); if(!p) return G_SOURCE_CONTINUE; gtk_tree_path_free(p); g_assert_false(gtk_widget_get_visible(named(window,"insight-category-more")));
  p=row(categories,"Widget",source,FALSE); g_assert_nonnull(p); GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(categories)); GtkTreeIter owner,child; g_assert_true(gtk_tree_model_get_iter(model,&owner,p)); gtk_tree_path_free(p); gboolean method=FALSE,member=FALSE;
  for(gboolean valid=gtk_tree_model_iter_children(model,&child,&owner);valid;valid=gtk_tree_model_iter_next(model,&child)) { gchar *name=NULL; gulong line=0; gtk_tree_model_get(model,&child,0,&name,3,&line,-1); method|=line && !g_strcmp0(name,"run"); member|=line && !g_strcmp0(name,"value"); g_free(name); }
  g_assert_true(method); g_assert_true(member); p=row(categories,"Members",NULL,TRUE); g_assert_nonnull(p); gtk_tree_path_free(p); p=row(categories,"Classes",NULL,TRUE); g_assert_nonnull(p); gtk_tree_path_free(p); select_row(categories,"run",source,FALSE); phase++;
 } else if(phase==8) {
  capture(); act(categories,1); clip("run"); act(categories,2); clip(source); unchanged();
  GtkTreeModel *model; GtkTreeIter it; g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(categories)),&model,&it)); gulong line=0; gtk_tree_model_get(model,&it,3,&line,-1); gchar *location=g_strdup_printf("%s:%lu",source,line); act(categories,3); clip(location); g_free(location); unchanged();
  act(categories,6); g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(category_entry)),==,"\"run\""); phase++;
 } else if(phase==9) {
  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(named(window,"insight-category-count"))),"Category symbols: 1 / 1")) return G_SOURCE_CONTINUE;
  unchanged(); gtk_entry_set_text(GTK_ENTRY(category_entry),"?["); phase++;
 } else if(phase==10) {
  if(!gtk_style_context_has_class(gtk_widget_get_style_context(category_entry),"error")) return G_SOURCE_CONTINUE;
  g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(named(window,"insight-category-count"))),==,"Category symbols: 1 / 1"); unchanged(); act(categories,7); g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(category_entry)),==,""); gtk_entry_set_text(GTK_ENTRY(category_entry),"run"); phase++;
 } else if(phase==11) {
  GtkTreePath *p=row(categories,"run",source,FALSE); if(!p) return G_SOURCE_CONTINUE; gtk_tree_path_free(p); select_row(categories,"run",source,FALSE); phase++;
 } else if(phase==12) {
  capture(); act(categories,4); phase++;
 } else if(phase==13) {
  GtkWidget *info=toplevel("insight-symbol-info-window"); if(!info) return G_SOURCE_CONTINUE; g_assert_true(gtk_widget_get_visible(info)); GtkWidget *close=button(info,"Close"); g_assert_nonnull(close); gtk_button_clicked(GTK_BUTTON(close)); unchanged(); show_panel(categories); gtk_window_present(GTK_WINDOW(window)); phase++;
 } else if(phase==14) {
  select_row(categories,"run",source,FALSE); gtk_widget_grab_focus(categories); key(GDK_KEY_f,GEANY_PRIMARY_MOD_MASK); g_assert_true(gtk_widget_has_focus(category_entry)); gtk_entry_set_text(GTK_ENTRY(category_entry),"f_06000"); phase++;
 } else if(phase==15) {
  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(named(window,"insight-category-count"))),"Category symbols: 1 / 1")) return G_SOURCE_CONTINUE;
  gtk_widget_grab_focus(category_entry); key(GDK_KEY_Return,0); phase++;
 } else if(phase==16) {
  if(g_strcmp0(doc->file_name,source)) return G_SOURCE_CONTINUE;
  document_open_file(python,FALSE,NULL,NULL); gtk_entry_set_text(GTK_ENTRY(category_entry),""); show_panel(categories); phase++;
 } else if(phase==17) {
  GtkTreePath *p=row(categories,"py_method",python,FALSE); if(!p) return G_SOURCE_CONTINUE; gtk_tree_path_free(p);

  p=row(categories,"Methods",NULL,TRUE); g_assert_nonnull(p); gtk_tree_path_free(p); select_row(categories,"py_method",python,FALSE); phase++;
 } else if(phase==18) {
  capture(); if(!right_click(categories,"PythonClass",python)) return G_SOURCE_CONTINUE;
  unchanged(); select_row(categories,"py_method",python,FALSE); capture(); gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-interface-ru")),TRUE); phase++;
 } else if(phase==19) {
  GtkTreePath *p=row(categories,"Методы",NULL,TRUE); if(!p) return G_SOURCE_CONTINUE; gtk_tree_path_free(p); unchanged();
  GtkWidget *menu=popup(categories); g_assert_cmpstr(gtk_menu_item_get_label(GTK_MENU_ITEM(command(menu,13))),==,"Сбросить колонки панели"); gtk_widget_destroy(menu); g_object_unref(menu); act(categories,5); g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(named(window,"insight-relation-query"))),==,"py_method");
  show_panel(types); select_row(types,"b.cpp",other,FALSE); act(types,6); phase++;
 } else if(phase==20) {
  if(g_strcmp0(gtk_label_get_text(GTK_LABEL(named(window,"insight-type-count"))),"Файлы типов: 1 / 4")) return G_SOURCE_CONTINUE;
  gtk_widget_grab_focus(type_entry); key(GDK_KEY_Return,0); phase++;
 } else {
  if(g_strcmp0(doc->file_name,other)) return G_SOURCE_CONTINUE;
  g_print("Grouped project GUI passed: profile-based types, fuzzy/regex/invalid filters, retained snapshots, group actions, clipboard, independent columns/sorts, category paging, native C++/Python types, direct class members, Info/References, keyboard open and RU\n"); gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1"))); return G_SOURCE_REMOVE;
 } return G_SOURCE_CONTINUE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *cwd=g_get_current_dir(),*name=g_strdup_printf("grouped-gui-%u",g_random_int()),*base=g_build_filename(cwd,name,NULL); g_free(cwd); g_free(name); root=g_build_filename(base,"project",NULL); profile=g_build_filename(base,"profile",NULL); source=g_build_filename(root,"a source.cpp",NULL); other=g_build_filename(root,"b.cpp",NULL); python=g_build_filename(root,"model.py",NULL); custom=g_build_filename(root,"forced.special",NULL);
 g_assert_cmpint(g_mkdir_with_parents(root,0700),==,0); gchar *dir=g_build_filename(profile,"insight",NULL); g_assert_cmpint(g_mkdir_with_parents(dir,0700),==,0); settings=g_build_filename(dir,"workbench.conf",NULL); g_free(dir); dir=g_build_filename(profile,"languages","Catalog Native",NULL); g_assert_cmpint(g_mkdir_with_parents(dir,0700),==,0); gchar *lang=g_build_filename(dir,"language.conf",NULL); g_assert_true(g_file_set_contents(lang,"[language]\nname=Catalog Native\nparser=C++\nextensions=special\ncompatible-extensions=cpp;hpp\n",-1,NULL)); g_free(dir); g_free(lang);
 GString *code=g_string_new("namespace Demo {\nclass Widget {\npublic:\n int value;\n int run() { return value; }\n};\n}\n"); for(guint i=0;i<=6000;i++) g_string_append_printf(code,"int f_%05u() { return %u; }\n",i,i); g_assert_true(g_file_set_contents(source,code->str,(gssize)code->len,NULL)); g_string_free(code,TRUE); g_assert_true(g_file_set_contents(other,"int secondary() { return 1; }\n",-1,NULL)); g_assert_true(g_file_set_contents(custom,"int custom_symbol() { return 1; }\n",-1,NULL)); g_assert_true(g_file_set_contents(python,"class PythonClass:\n def py_method(self):\n  return 1\n",-1,NULL));
 gchar *encoded=g_base64_encode((const guchar *)root,strlen(root)),*prefs=g_strdup_printf("[workbench]\nversion=1\nfollow=false\nproject-visible=true\nroot-base64=%s\nfile-types-column-order=1;0;\nfile-types-column-widths=345;221;\nfile-types-column-visible=0;1;\nfile-types-sort-column=1\nfile-types-sort-descending=true\nsymbol-categories-column-order=0;3;2;1;4;\nsymbol-categories-column-widths=222;70;280;160;120;\nsymbol-categories-column-visible=1;1;1;1;0;\nsymbol-categories-sort-column=0\n",encoded); g_assert_true(g_file_set_contents(settings,prefs,-1,NULL)); g_free(encoded); g_free(prefs); gchar *config=g_build_filename(profile,"geany.conf",NULL); g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL)); g_free(config);
 gchar *argv[]={"grouped-gui","--new-instance","--no-session","--config",profile,NULL}; g_timeout_add(250,step,NULL); g_timeout_add_seconds(70,timeout,NULL); g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL); gint result=main_lib(5,argv);
 GKeyFile *keyfile=g_key_file_new(); g_assert_true(g_key_file_load_from_file(keyfile,settings,G_KEY_FILE_NONE,NULL)); g_assert_cmpint(g_key_file_get_integer(keyfile,"workbench","file-types-sort-column",NULL),==,1); g_assert_true(g_key_file_get_boolean(keyfile,"workbench","file-types-sort-descending",NULL)); gsize size=0; gint *order=g_key_file_get_integer_list(keyfile,"workbench","file-types-column-order",&size,NULL); g_assert_cmpuint(size,==,2); g_assert_cmpint(order[0],==,0); g_free(order); order=g_key_file_get_integer_list(keyfile,"workbench","symbol-categories-column-order",&size,NULL); g_assert_cmpuint(size,==,5); g_assert_cmpint(order[1],==,3); g_free(order); g_key_file_unref(keyfile);
 g_free(base); g_free(root); g_free(profile); g_free(source); g_free(other); g_free(python); g_free(custom); g_free(settings); return result;
}
