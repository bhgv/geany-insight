/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Folder trees and file counts.
 * RU: Деревья каталогов и подсчёт файлов. */

#include "folder-tree-update.inc"

#include "folder-commands.inc"

#include "folder-panel.inc"

static const gchar *grouped_prefix(GroupedPanel *p) { return p->symbols ? "symbol-categories" : "file-types"; }
static gboolean grouped_row(GtkTreeModel *model,GtkTreeIter *iter,GroupedPanel *p,gchar **name,gchar **path,gulong *line) {
 if(p->symbols) { gtk_tree_model_get(model,iter,SYMBOL_NAME,name,SYMBOL_PATH,path,SYMBOL_LINE,line,-1); return *line!=0; }
 gboolean group=FALSE; gtk_tree_model_get(model,iter,0,name,1,path,2,&group,-1); *line=group ? 0 : 1; return !group;
}
static gchar *grouped_key(GtkTreeModel *model,GtkTreeIter *iter,GroupedPanel *p) {
 GString *key=g_string_new(NULL); GtkTreeIter current=*iter;
 for(guint depth=0;depth<64;depth++) {
  gchar *name=NULL,*path=NULL,*scope=NULL,*kind=NULL; gulong line=0; grouped_row(model,&current,p,&name,&path,&line);
  if(p->symbols) gtk_tree_model_get(model,&current,SYMBOL_SCOPE,&scope,SYMBOL_KIND,&kind,-1);
  gchar *header=p->symbols && !line ? g_strdup_printf("%s:%s",kind ? kind : "",scope ? scope : "") : NULL;
  const gchar *value=header ? header : name;
  g_string_append_printf(key,"%lu:%zu:%s:%zu:%s;",line,value ? strlen(value) : 0,value ? value : "",path ? strlen(path) : 0,path ? path : "");
  g_free(header); g_free(name); g_free(path); g_free(scope); g_free(kind); GtkTreeIter parent; if(!gtk_tree_model_iter_parent(model,&parent,&current)) break; current=parent;
 }
 return g_string_free(key,FALSE);
}
typedef struct { GroupedPanel *panel; GHashTable *expanded; const gchar *wanted; GtkTreePath *found; gboolean restore; } GroupedVisit;
static gboolean grouped_visit(GtkTreeModel *model,GtkTreePath *row,GtkTreeIter *iter,gpointer data) {
 GroupedVisit *v=data; gchar *key=grouped_key(model,iter,v->panel);
 if(v->expanded) {
  if(v->restore && g_hash_table_contains(v->expanded,key)) { gtk_tree_view_expand_to_path(GTK_TREE_VIEW(v->panel->view),row); gtk_tree_view_expand_row(GTK_TREE_VIEW(v->panel->view),row,FALSE); }
  else if(!v->restore && gtk_tree_view_row_expanded(GTK_TREE_VIEW(v->panel->view),row)) g_hash_table_add(v->expanded,g_strdup(key));
 }
 if(v->wanted && !strcmp(key,v->wanted)) v->found=gtk_tree_path_copy(row);
 g_free(key); return v->found!=NULL;
}
static gchar *grouped_capture(GroupedPanel *p,GHashTable *expanded) {
 GtkTreeIter iter; GtkTreeModel *model; gchar *selected=NULL;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),&model,&iter)) selected=grouped_key(model,&iter,p);
 GroupedVisit v={.panel=p,.expanded=expanded}; gtk_tree_model_foreach(GTK_TREE_MODEL(p->store),grouped_visit,&v); p->setting=TRUE; return selected;
}
static void grouped_restore(GroupedPanel *p,GHashTable *expanded,gchar *selected) {
 GroupedVisit v={.panel=p,.expanded=expanded,.restore=TRUE}; gtk_tree_model_foreach(GTK_TREE_MODEL(p->store),grouped_visit,&v);
 if(*gtk_entry_get_text(GTK_ENTRY(p->entry))) gtk_tree_view_expand_all(GTK_TREE_VIEW(p->view));
 if(selected) { v=(GroupedVisit){.panel=p,.wanted=selected}; gtk_tree_model_foreach(GTK_TREE_MODEL(p->store),grouped_visit,&v); if(v.found) { gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),v.found); gtk_tree_path_free(v.found); } }
 p->setting=FALSE; g_free(selected); g_hash_table_destroy(expanded);
}
static void grouped_count(GroupedPanel *p,guint shown,guint total) {
 p->shown=shown; p->total=total;
 gchar *text=insight_i18n_printf(p->symbols ? "Category symbols: %u / %u" : "Type files: %u / %u",shown,total); gtk_label_set_text(GTK_LABEL(p->status),text); g_free(text);
}
static void grouped_refresh(GroupedPanel *p) { if(p->symbols) categories_filter(NULL,NULL); else file_types_refresh(); }
static gboolean grouped_idle(gpointer data) { GroupedPanel *p=data; p->timer=0; if(wb.alive) grouped_refresh(p); return G_SOURCE_REMOVE; }
static void grouped_changed(GtkEditable *entry,gpointer data) { (void)entry; GroupedPanel *p=data; p->limit=p->symbols ? 5000 : 10000; if(p->timer) g_source_remove(p->timer); p->timer=g_timeout_add(120,grouped_idle,p); }
static void grouped_flush(GroupedPanel *p) { if(p->timer) { g_source_remove(p->timer); p->timer=0; grouped_refresh(p); } }
static void grouped_selected(GtkTreeSelection *selection,gpointer data) { GroupedPanel *p=data; if(p->setting) return; if(p->symbols) browser_selected(selection,NULL); else tree_selected(selection,NULL); }
enum { GP_OPEN,GP_NAME,GP_PATH,GP_LOCATION,GP_INFO,GP_REFERENCES,GP_FILTER,GP_CLEAR,GP_EXPAND,GP_COLLAPSE,GP_EXPAND_ALL,GP_COLLAPSE_ALL,GP_COLUMNS,GP_RESET,GP_MORE,GP_HELP };
static void grouped_action(GroupedPanel *p,guint action) {
 grouped_flush(p); GtkTreeModel *model=NULL; GtkTreeIter iter; gchar *name=NULL,*path=NULL; gulong line=0;
 gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),&model,&iter),leaf=selected && grouped_row(model,&iter,p,&name,&path,&line);
 switch(action) {
 case GP_OPEN: if(leaf) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter); if(p->symbols) category_activated(GTK_TREE_VIEW(p->view),row,NULL,NULL); else tree_activated(GTK_TREE_VIEW(p->view),row,NULL,NULL); gtk_tree_path_free(row); } break;
 case GP_NAME: if(leaf) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),name,-1); break;
 case GP_PATH: if(leaf && path && path[0]!='@') gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),path,-1); break;
 case GP_LOCATION: if(leaf && path && path[0]!='@') { gchar *text=p->symbols ? g_strdup_printf("%s:%lu",path,line) : g_strdup(path); gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,-1); g_free(text); } break;
 case GP_INFO: if(leaf && p->symbols) symbol_action(GTK_TREE_VIEW(p->view),SP_INFO); break;
 case GP_REFERENCES: if(leaf && p->symbols) symbol_action(GTK_TREE_VIEW(p->view),SP_REFERENCES); break;
 case GP_FILTER: if(leaf) { gchar *query=g_strdup_printf(p->symbols ? "\"%s\"" : "file:\"%s\"",p->symbols ? name : path); if(strlen(query)<=1024) gtk_entry_set_text(GTK_ENTRY(p->entry),query); else ui_set_statusbar(TRUE,"Selected value is too long for a lookup filter"); g_free(query); } break;
 case GP_CLEAR: gtk_entry_set_text(GTK_ENTRY(p->entry),""); gtk_widget_grab_focus(p->entry); break;
 case GP_EXPAND: case GP_COLLAPSE:
  if(selected) { GtkTreePath *row=gtk_tree_model_get_path(model,&iter); if(leaf && !gtk_tree_model_iter_has_child(model,&iter)) gtk_tree_path_up(row); if(gtk_tree_path_get_depth(row)) { if(action==GP_EXPAND) gtk_tree_view_expand_row(GTK_TREE_VIEW(p->view),row,TRUE); else gtk_tree_view_collapse_row(GTK_TREE_VIEW(p->view),row); } gtk_tree_path_free(row); } break;
 case GP_EXPAND_ALL: gtk_tree_view_expand_all(GTK_TREE_VIEW(p->view)); break;
 case GP_COLLAPSE_ALL: gtk_tree_view_collapse_all(GTK_TREE_VIEW(p->view)); break;
 case GP_RESET: insight_symbol_view_columns_reset(GTK_TREE_VIEW(p->view)); break;
 case GP_MORE: if(p->symbols && p->limit<250000) { p->limit=MIN(250000,p->limit+5000); grouped_refresh(p); } break;
 case GP_HELP: insight_help_show(NULL,p->symbols ? "panel-categories.md" : "panel-file-types.md"); break;
 default: break;
 }
 g_free(name); g_free(path);
}
static void grouped_command(GtkMenuItem *item,gpointer data) { grouped_action(data,GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-grouped-action"))); }
static GtkWidget *grouped_menu(GroupedPanel *p) {
 grouped_flush(p); GtkWidget *menu=gtk_menu_new(); gtk_menu_attach_to_widget(GTK_MENU(menu),p->view,NULL); g_object_set_data(G_OBJECT(p->view),"insight-grouped-popup",menu);
 GtkTreeModel *model=NULL; GtkTreeIter iter; gchar *name=NULL,*path=NULL; gulong line=0;
 gboolean selected=gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(p->view)),&model,&iter),leaf=selected && grouped_row(model,&iter,p,&name,&path,&line);
 const gchar *labels[]={"Open selected source","Copy selected name","Copy source path","Copy source location","Symbol Info…","Find usages…","Filter to selected item","Clear panel filter","Expand selected branch","Collapse selected branch","Expand all groups","Collapse all groups","Visible columns","Reset panel columns","Load more category symbols","Panel help"};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) {
  if(!p->symbols && (i==GP_INFO || i==GP_REFERENCES || i==GP_MORE)) continue;
  GtkWidget *item=gtk_menu_item_new_with_label(labels[i]); gchar *id=g_strdup_printf("insight-grouped-action-%u",i); gtk_widget_set_name(item,id); g_free(id); g_object_set_data(G_OBJECT(item),"insight-grouped-action",GUINT_TO_POINTER(i));
  gboolean enabled=i<=GP_FILTER ? leaf : (i==GP_EXPAND || i==GP_COLLAPSE ? selected : (i==GP_MORE ? p->shown<p->total && p->limit<250000 : TRUE));
  if((i==GP_PATH || i==GP_LOCATION) && (!path || path[0]=='@')) enabled=FALSE;
  gtk_widget_set_sensitive(item,enabled);
  if(i==GP_COLUMNS) gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),insight_symbol_view_columns_menu(GTK_TREE_VIEW(p->view))); else g_signal_connect(item,"activate",G_CALLBACK(grouped_command),p);
  gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
 }
 g_free(name); g_free(path); g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu); insight_i18n_apply(menu); gtk_widget_show_all(menu); return menu;
}
static gboolean grouped_popup(GtkWidget *widget,gpointer data) { (void)widget; GroupedPanel *p=data; gtk_menu_popup_at_widget(GTK_MENU(grouped_menu(p)),p->view,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL); return TRUE; }
static gboolean grouped_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 GroupedPanel *p=data; if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
 grouped_flush(p); p->setting=TRUE; GtkTreePath *row=NULL;
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&row,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),row,NULL,FALSE); gtk_tree_path_free(row); } else gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
 p->setting=FALSE; gtk_widget_grab_focus(widget); gtk_menu_popup_at_pointer(GTK_MENU(grouped_menu(p)),(GdkEvent *)event); return TRUE;
}
