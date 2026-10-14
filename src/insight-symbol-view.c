/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-symbol-view.h"
#include "insight-i18n.h"
#include "document.h"
#include "filetypes.h"
#include "tagmanager/tm_parser.h"
#include <string.h>
enum { NAME,SCOPE,PATH,LINE,KIND,COLUMNS };
enum { CALLABLES,TYPES,ENUMS,ENUM_VALUES,MACROS,MEMBERS,VARIABLES,LOCALS,NAMESPACES,OTHER,FILTER_COUNT };
#define ALL_TYPES ((1u<<FILTER_COUNT)-1)
static const gchar *filter_icons[]={"classviewer-method","classviewer-struct","classviewer-struct","classviewer-var","classviewer-macro","classviewer-member","classviewer-var","classviewer-var","classviewer-namespace","classviewer-other"};
static const gchar *filter_names[]={"Functions / methods","Types / structures","Enumerations","Enum constants","Macros","Members / fields","Variables","Local variables","Namespaces / includes","Other symbols"};
static guint filter_category(guint type) {
 if(type&(tm_tag_function_t|tm_tag_method_t|tm_tag_prototype_t)) return CALLABLES;
 if(type&(tm_tag_class_t|tm_tag_interface_t|tm_tag_struct_t|tm_tag_union_t|tm_tag_typedef_t)) return TYPES;
 if(type&tm_tag_enum_t) return ENUMS;
 if(type&tm_tag_enumerator_t) return ENUM_VALUES;
 if(type&(tm_tag_macro_t|tm_tag_macro_with_arg_t)) return MACROS;
 if(type&(tm_tag_member_t|tm_tag_field_t)) return MEMBERS;
 if(type&tm_tag_local_var_t) return LOCALS;
 if(type&(tm_tag_variable_t|tm_tag_externvar_t)) return VARIABLES;
 if(type&(tm_tag_namespace_t|tm_tag_package_t|tm_tag_include_t)) return NAMESPACES;
 return OTHER;
}
typedef struct {
 GtkTreeView *view; GtkListStore *source; GtkWidget *toggle,*types,*menu,*status,*checks[FILTER_COUNT]; GeanyFiletype *fallback;
 guint mask,shown; gboolean setting,loading;
 guint timer,idle; GtkTreeStore *pending; GHashTable *groups; GtkTreeIter iter; gboolean valid,grouped,sorting;
} Symbols;
static GList *views;
static guint type_for(Symbols *s,const gchar *path,const gchar *kind) {
 if(kind && g_str_has_prefix(kind,"@type:")) return (guint)g_ascii_strtoull(kind+6,NULL,10);
 GeanyFiletype *ft=path && *path && path[0]!='@' ? filetypes_detect_from_extension(path) : s->fallback;
 return ft && ft->lang>=0 && kind && *kind ? tm_parser_get_tag_type(*kind,ft->lang) : tm_tag_undef_t;
}
static const gchar *type_name(guint type,gboolean plural) {
 const gchar *one="Other symbol",*many="Other symbols";
 if(type&tm_tag_class_t) { one="Class"; many="Classes"; }
 else if(type&tm_tag_interface_t) { one="Interface"; many="Interfaces"; }
 else if(type&tm_tag_struct_t) { one="Structure"; many="Structures"; }
 else if(type&tm_tag_union_t) { one="Union"; many="Unions"; }
 else if(type&tm_tag_enum_t) { one="Enumeration"; many="Enumerations"; }
 else if(type&tm_tag_enumerator_t) { one="Enumerator"; many="Enumerators"; }
 else if(type&tm_tag_typedef_t) { one="Type alias"; many="Type aliases"; }
 else if(type&tm_tag_function_t) { one="Function"; many="Functions"; }
 else if(type&tm_tag_method_t) { one="Method"; many="Methods"; }
 else if(type&tm_tag_prototype_t) { one="Prototype"; many="Prototypes"; }
 else if(type&(tm_tag_macro_t|tm_tag_macro_with_arg_t)) { one="Macro"; many="Macros"; }
 else if(type&(tm_tag_member_t|tm_tag_field_t)) { one="Member"; many="Members"; }
 else if(type&tm_tag_local_var_t) { one="Local variable"; many="Local variables"; }
 else if(type&tm_tag_externvar_t) { one="External variable"; many="External variables"; }
 else if(type&tm_tag_variable_t) { one="Variable"; many="Variables"; }
 else if(type&(tm_tag_namespace_t|tm_tag_package_t)) { one="Namespace / module"; many="Namespaces / modules"; }
 else if(type&tm_tag_include_t) { one="Include"; many="Includes"; }
 return insight_tr(plural ? many : one);
}
static const gchar *type_icon(guint type) {
 if(type&(tm_tag_function_t|tm_tag_method_t|tm_tag_prototype_t)) return "classviewer-method";
 if(type&(tm_tag_class_t|tm_tag_interface_t)) return "classviewer-class";
 if(type&(tm_tag_struct_t|tm_tag_union_t|tm_tag_enum_t|tm_tag_typedef_t)) return "classviewer-struct";
 if(type&(tm_tag_macro_t|tm_tag_macro_with_arg_t)) return "classviewer-macro";
 if(type&(tm_tag_namespace_t|tm_tag_package_t)) return "classviewer-namespace";
 if(type&(tm_tag_field_t|tm_tag_member_t)) return "classviewer-member";
 if(type&(tm_tag_variable_t|tm_tag_externvar_t|tm_tag_local_var_t|tm_tag_enumerator_t)) return "classviewer-var";
 return "classviewer-other";
}
static void cell(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 Symbols *s=data; gchar *path=NULL,*kind=NULL; gulong line=0; gtk_tree_model_get(model,iter,PATH,&path,KIND,&kind,LINE,&line,-1); guint type=type_for(s,path,kind); gboolean group=!line;
 if(GTK_IS_CELL_RENDERER_PIXBUF(renderer)) g_object_set(renderer,"icon-name",type_icon(type),NULL);
 else {
  gint field=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column),"insight-symbol-field"));
  if(field==LINE) { gchar *text=line ? g_strdup_printf("%lu",line) : g_strdup(""); g_object_set(renderer,"text",text,NULL); g_free(text); }
  if(field==KIND) { gchar *text=group ? g_strdup("") : g_strdup_printf("%s (%s)",type_name(type,FALSE),kind ? kind : ""); g_object_set(renderer,"text",text,NULL); g_free(text); }
  GdkRGBA foreground; gtk_style_context_get_color(gtk_widget_get_style_context(GTK_WIDGET(s->view)),GTK_STATE_FLAG_NORMAL,&foreground); gboolean dark=(foreground.red+foreground.green+foreground.blue)>1.8;
  const gchar *color=dark ? "#bac4d0" : "#586575";
  if(type&(tm_tag_function_t|tm_tag_method_t|tm_tag_prototype_t)) color=dark ? "#82b5ff" : "#285da8";
  else if(type&(tm_tag_class_t|tm_tag_interface_t|tm_tag_typedef_t)) color=dark ? "#dba2ef" : "#8a3c9f";
  else if(type&(tm_tag_struct_t|tm_tag_union_t)) color=dark ? "#7dd3cd" : "#127a78";
  else if(type&(tm_tag_enum_t|tm_tag_enumerator_t)) color=dark ? "#f0b5c7" : "#a83b64";
  else if(type&(tm_tag_macro_t|tm_tag_macro_with_arg_t)) color=dark ? "#f0c67b" : "#99630b";
  else if(type&(tm_tag_namespace_t|tm_tag_package_t)) color=dark ? "#b8d88a" : "#567e23";
  else if(type&(tm_tag_variable_t|tm_tag_externvar_t|tm_tag_local_var_t|tm_tag_member_t|tm_tag_field_t)) color=dark ? "#8fd4aa" : "#247649";
  GtkTreePath *row=gtk_tree_model_get_path(model,iter); gboolean selected=gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(s->view),row); gtk_tree_path_free(row);
  g_object_set(renderer,"foreground",color,"foreground-set",!selected,"weight",group ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,NULL);
 }
 g_free(path); g_free(kind);
}
static void cancel(Symbols *s) {
 if(s->timer) { g_source_remove(s->timer); s->timer=0; } if(s->idle) { g_source_remove(s->idle); s->idle=0; }
 g_clear_object(&s->pending); g_clear_pointer(&s->groups,g_hash_table_unref);
}
typedef struct { gchar *name,*path; gulong line; GtkTreeSelection *selection; } Choice;
static gboolean restore_choice(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 (void)path; Choice *choice=data; gchar *name=NULL,*file=NULL; gulong line=0; gtk_tree_model_get(model,iter,NAME,&name,PATH,&file,LINE,&line,-1); gboolean match=line && line==choice->line && !g_strcmp0(name,choice->name) && !g_strcmp0(file,choice->path);
 if(match) gtk_tree_selection_select_iter(choice->selection,iter);
 g_free(name); g_free(file); return match;
}
static void publish(Symbols *s,GtkTreeModel *model) {
 GtkTreeSelection *selection=gtk_tree_view_get_selection(s->view); GtkTreeModel *old=gtk_tree_view_get_model(s->view); GtkTreeIter iter; Choice choice={.selection=selection};
 if(gtk_tree_selection_get_selected(selection,NULL,&iter)) gtk_tree_model_get(old,&iter,NAME,&choice.name,PATH,&choice.path,LINE,&choice.line,-1);
 GHashTable *collapsed=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 if(GTK_IS_TREE_STORE(old)) for(gboolean valid=gtk_tree_model_get_iter_first(old,&iter);valid;valid=gtk_tree_model_iter_next(old,&iter)) { GtkTreePath *path=gtk_tree_model_get_path(old,&iter); if(!gtk_tree_view_row_expanded(s->view,path)) { gchar *kind=NULL; gtk_tree_model_get(old,&iter,KIND,&kind,-1); if(kind) g_hash_table_add(collapsed,kind); } gtk_tree_path_free(path); }
 guint signal=g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION); g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);
 gtk_tree_view_set_model(s->view,model);
 if(GTK_IS_TREE_STORE(model)) for(gboolean valid=gtk_tree_model_get_iter_first(model,&iter);valid;valid=gtk_tree_model_iter_next(model,&iter)) { gchar *kind=NULL; gtk_tree_model_get(model,&iter,KIND,&kind,-1); if(!kind || !g_hash_table_contains(collapsed,kind)) { GtkTreePath *path=gtk_tree_model_get_path(model,&iter); gtk_tree_view_expand_row(s->view,path,FALSE); gtk_tree_path_free(path); } g_free(kind); }
 if(choice.line) gtk_tree_model_foreach(model,restore_choice,&choice);
 g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL); g_hash_table_unref(collapsed); g_free(choice.name); g_free(choice.path); gtk_widget_set_sensitive(GTK_WIDGET(s->view),!s->loading);
}
static void status(Symbols *s) {
 if(!s->status) return;
 guint total=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(s->source),NULL);
 gchar *text=insight_i18n_printf("Symbols: %u / %u",s->grouped || s->mask!=ALL_TYPES ? s->shown : total,total);
 gtk_label_set_text(GTK_LABEL(s->status),text); g_free(text);
}
static void queue(Symbols *s);
static void grouped_sort(GtkTreeSortable *model,gpointer data) {
 Symbols *s=g_object_get_data(G_OBJECT(data),"insight-symbol-view"); if(!s || s->sorting) return; gint column=-1; GtkSortType direction; if(!gtk_tree_sortable_get_sort_column_id(model,&column,&direction) || column<0 || column>=COLUMNS) return;
 s->sorting=TRUE; gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(s->source),column,direction); s->sorting=FALSE; queue(s);
}
static gboolean apply(gpointer data) {
 Symbols *s=data; gint64 deadline=g_get_monotonic_time()+3000;
 while(s->valid) {
  gchar *name=NULL,*scope=NULL,*path=NULL,*kind=NULL; gulong line=0; gtk_tree_model_get(GTK_TREE_MODEL(s->source),&s->iter,NAME,&name,SCOPE,&scope,PATH,&path,LINE,&line,KIND,&kind,-1); guint type=type_for(s,path,kind);
  if(s->mask&(1u<<filter_category(type))) {
   GtkTreeIter *parent=NULL;
   if(s->grouped) {
    parent=g_hash_table_lookup(s->groups,GUINT_TO_POINTER(type+1));
    if(!parent) { parent=g_new(GtkTreeIter,1); gtk_tree_store_append(s->pending,parent,NULL); gchar *group=g_strdup_printf("@type:%u",type); gtk_tree_store_set(s->pending,parent,NAME,type_name(type,TRUE),KIND,group,-1); g_free(group); g_hash_table_insert(s->groups,GUINT_TO_POINTER(type+1),parent); }
   }
   GtkTreeIter child; gtk_tree_store_append(s->pending,&child,parent); gtk_tree_store_set(s->pending,&child,NAME,name,SCOPE,scope,PATH,path,LINE,line,KIND,kind,-1); s->shown++;
  }
  g_free(name); g_free(scope); g_free(path); g_free(kind);
  s->valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(s->source),&s->iter); if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 gint column=-1; GtkSortType direction; if(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(s->source),&column,&direction)) gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(s->pending),column,direction);
 g_signal_connect_object(s->pending,"sort-column-changed",G_CALLBACK(grouped_sort),s->view,0); /* callback below retrieves state from the view */
 publish(s,GTK_TREE_MODEL(s->pending)); status(s); g_clear_object(&s->pending); g_clear_pointer(&s->groups,g_hash_table_unref); s->idle=0; return G_SOURCE_REMOVE;
}
static gboolean begin(gpointer data) {
 Symbols *s=data; s->timer=0; if(!s->grouped && s->mask==ALL_TYPES) { status(s); return G_SOURCE_REMOVE; }
 s->shown=0;
 s->pending=gtk_tree_store_new(COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING); s->groups=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free); s->valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(s->source),&s->iter); s->idle=g_idle_add(apply,s); return G_SOURCE_REMOVE;
}
static void queue(Symbols *s) { if(!s) return; cancel(s); if(s->grouped || s->mask!=ALL_TYPES) gtk_widget_set_sensitive(GTK_WIDGET(s->view),FALSE); s->timer=g_timeout_add(100,begin,s); }
static void row_changed(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) { (void)model; (void)path; (void)iter; queue(g_object_get_data(G_OBJECT(data),"insight-symbol-view")); }
static void row_deleted(GtkTreeModel *model,GtkTreePath *path,gpointer data) { (void)model; (void)path; queue(g_object_get_data(G_OBJECT(data),"insight-symbol-view")); }
static void reordered(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gint *order,gpointer data) { (void)model; (void)path; (void)iter; (void)order; queue(g_object_get_data(G_OBJECT(data),"insight-symbol-view")); }
static void toggled(GtkToggleButton *toggle,gpointer data) { Symbols *s=g_object_get_data(G_OBJECT(data),"insight-symbol-view"); if(!s) return; s->grouped=gtk_toggle_button_get_active(toggle); cancel(s); if(!s->grouped && s->mask==ALL_TYPES) publish(s,GTK_TREE_MODEL(s->source));
 queue(s); }
static void freed(gpointer data) { Symbols *s=data; cancel(s); views=g_list_remove(views,s); g_object_unref(s->source); g_free(s); }
GtkWidget *insight_symbol_view_attach(GtkTreeView *view,GtkListStore *source,GeanyFiletype *fallback) {
 Symbols *s=g_new0(Symbols,1); s->view=view; s->source=g_object_ref(source); s->fallback=fallback; s->mask=ALL_TYPES; s->toggle=gtk_check_button_new_with_label("Group by symbol type"); g_object_set_data_full(G_OBJECT(view),"insight-symbol-view",s,freed); g_signal_connect_object(s->toggle,"toggled",G_CALLBACK(toggled),view,0);
 GList *columns=gtk_tree_view_get_columns(view);
 for(GList *p=columns;p;p=p->next) { GtkTreeViewColumn *column=p->data; gint field=gtk_tree_view_column_get_sort_column_id(column); g_object_set_data(G_OBJECT(column),"insight-symbol-field",GINT_TO_POINTER(field)); GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column)); for(GList *c=cells;c;c=c->next) gtk_tree_view_column_set_cell_data_func(column,c->data,cell,s,NULL); g_list_free(cells); if(field==NAME) { GtkCellRenderer *icon=gtk_cell_renderer_pixbuf_new(); gtk_tree_view_column_pack_start(column,icon,FALSE); gtk_cell_layout_reorder(GTK_CELL_LAYOUT(column),icon,0); gtk_tree_view_column_set_cell_data_func(column,icon,cell,s,NULL); } }
 g_list_free(columns); g_signal_connect_object(source,"row-changed",G_CALLBACK(row_changed),view,0); g_signal_connect_object(source,"row-inserted",G_CALLBACK(row_changed),view,0); g_signal_connect_object(source,"row-deleted",G_CALLBACK(row_deleted),view,0); g_signal_connect_object(source,"rows-reordered",G_CALLBACK(reordered),view,0); views=g_list_prepend(views,s); return s->toggle;
}
void insight_symbol_view_filetype(GtkTreeView *view,GeanyFiletype *ft) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(s && s->fallback!=ft) { s->fallback=ft; queue(s); gtk_widget_queue_draw(GTK_WIDGET(view)); } }
gboolean insight_symbol_view_grouped(GtkTreeView *view) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); return s && s->grouped; }
void insight_symbol_view_set_grouped(GtkTreeView *view,gboolean enabled) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(s) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->toggle),enabled); }
void insight_symbol_views_locale_changed(void) { for(GList *p=views;p;p=p->next) { Symbols *s=p->data; if(s->menu) insight_i18n_apply(s->menu); queue(s); gtk_widget_queue_draw(GTK_WIDGET(s->view)); } }

static void filter_changed(GtkCheckMenuItem *item,gpointer data) {
 (void)item; Symbols *s=g_object_get_data(G_OBJECT(data),"insight-symbol-view"); if(!s || s->setting) return;
 s->mask=0; for(guint i=0;i<FILTER_COUNT;i++) if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->checks[i]))) s->mask|=1u<<i;
 if(!s->grouped && s->mask==ALL_TYPES) publish(s,GTK_TREE_MODEL(s->source));
 queue(s);
}
void insight_symbol_view_set_types(GtkTreeView *view,guint mask) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(!s) return;
 s->mask=mask&ALL_TYPES; s->setting=TRUE;
 for(guint i=0;i<FILTER_COUNT;i++) if(s->checks[i]) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->checks[i]),(s->mask&(1u<<i))!=0);
 s->setting=FALSE; if(!s->grouped && s->mask==ALL_TYPES) publish(s,GTK_TREE_MODEL(s->source));
 queue(s);
}
guint insight_symbol_view_types_mask(GtkTreeView *view) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); return s ? s->mask : ALL_TYPES; }
static void filter_all(GtkMenuItem *item,gpointer data) { guint mask=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-symbol-mask")); insight_symbol_view_set_types(GTK_TREE_VIEW(data),mask); }
GtkWidget *insight_symbol_view_types_control(GtkTreeView *view) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(!s || s->types) return NULL;
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*button=gtk_menu_button_new(),*menu=gtk_menu_new(); s->types=box; s->menu=menu;
 gtk_widget_set_name(button,"insight-symbol-types"); gtk_button_set_image(GTK_BUTTON(button),gtk_image_new_from_icon_name("view-filter-symbolic",GTK_ICON_SIZE_MENU)); gtk_button_set_relief(GTK_BUTTON(button),GTK_RELIEF_NONE); gtk_widget_set_tooltip_text(button,"Visible symbol types — choose which categories to show");
 gtk_menu_button_set_popup(GTK_MENU_BUTTON(button),menu);
 const gchar *commands[]={"Show all symbol types","Hide all symbol types"};
 for(guint i=0;i<2;i++) { GtkWidget *item=gtk_image_menu_item_new_with_label(commands[i]); gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),gtk_image_new_from_icon_name(i ? "edit-clear-symbolic" : "view-list-symbolic",GTK_ICON_SIZE_MENU)); gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE); g_object_set_data(G_OBJECT(item),"insight-symbol-mask",GUINT_TO_POINTER(i ? 0 : ALL_TYPES)); g_signal_connect_object(item,"activate",G_CALLBACK(filter_all),view,0); gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 for(guint i=0;i<FILTER_COUNT;i++) { GtkWidget *item=gtk_check_menu_item_new_with_label(filter_names[i]); GtkWidget *label=gtk_bin_get_child(GTK_BIN(item)),*content=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); g_object_ref(label); gtk_container_remove(GTK_CONTAINER(item),label); gtk_box_pack_start(GTK_BOX(content),gtk_image_new_from_icon_name(filter_icons[i],GTK_ICON_SIZE_MENU),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(content),label,TRUE,TRUE,0); g_object_unref(label); gtk_container_add(GTK_CONTAINER(item),content); gchar *name=g_strdup_printf("insight-symbol-type-%u",i); gtk_widget_set_name(item,name); g_free(name); s->checks[i]=item; gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),(s->mask&(1u<<i))!=0); g_signal_connect_object(item,"toggled",G_CALLBACK(filter_changed),view,0); gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 gtk_widget_show_all(menu); gtk_box_pack_start(GTK_BOX(box),button,FALSE,FALSE,0); s->status=gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(s->status),0); gtk_widget_set_tooltip_text(s->status,"Visible symbols / symbols matching the name filter; type filtering keeps the index unchanged"); gtk_box_pack_start(GTK_BOX(box),s->status,TRUE,TRUE,0); status(s); return box;
}

void insight_symbol_view_loading(GtkTreeView *view,gboolean loading) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(!s) return;
 s->loading=loading;
 gboolean deriving=(s->timer || s->idle) && (s->grouped || s->mask!=ALL_TYPES);
 gtk_widget_set_sensitive(GTK_WIDGET(view),!loading && !deriving);
}
