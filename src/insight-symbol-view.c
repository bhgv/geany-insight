/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-symbol-view.h"
#include "insight-i18n.h"
#include "document.h"
#include "filetypes.h"
#include "insight-languages.h"
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
 GtkTreeView *view; GtkListStore *source; GtkWidget *toggle,*scope_toggle,*types,*menu,*status,*more,*checks[FILTER_COUNT]; GeanyFiletype *fallback;
 guint mask,shown,more_step; gboolean setting,loading,has_more;
 guint timer,idle; GtkTreeStore *pending; GHashTable *groups,*scope_groups,*owners; GtkTreeIter iter; gboolean valid,grouped,hierarchical,sorting,collecting_owners;
} Symbols;
static GList *views;
guint insight_symbol_kind_type(const gchar *path,const gchar *kind,GeanyFiletype *fallback) {
 if(kind && g_str_has_prefix(kind,"@decl:")) return (guint)g_ascii_strtoull(kind+6,NULL,10);
 if(kind && g_str_has_prefix(kind,"@scope:")) return tm_tag_namespace_t;
 if(kind && g_str_has_prefix(kind,"@type:")) return (guint)g_ascii_strtoull(kind+6,NULL,10);
 GeanyDocument *doc=path && *path && path[0]!='@' ? document_find_by_filename(path) : NULL;
 GeanyFiletype *ft=doc ? doc->file_type : (path && *path && path[0]!='@' ? insight_languages_detect(path) : fallback);
 if(!ft && path && *path && path[0]!='@') ft=filetypes_detect_from_extension(path);
 gint lang=doc && doc->tm_file ? doc->tm_file->lang : (ft ? ft->lang : -1);
 return lang>=0 && kind && *kind ? tm_parser_get_tag_type(*kind,lang) : tm_tag_undef_t;
}
static guint type_for(Symbols *s,const gchar *path,const gchar *kind) { return insight_symbol_kind_type(path,kind,s->fallback); }
const gchar *insight_symbol_type_name(guint type,gboolean plural) {
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
const gchar *insight_symbol_type_icon(guint type) {
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
 if(GTK_IS_CELL_RENDERER_PIXBUF(renderer)) g_object_set(renderer,"icon-name",insight_symbol_type_icon(type),NULL);
 else {
  gint field=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column),"insight-symbol-field"));
  if(field==LINE) { gchar *text=line ? g_strdup_printf("%lu",line) : g_strdup(""); g_object_set(renderer,"text",text,NULL); g_free(text); }
  if(field==PATH) { const gchar *root=g_object_get_data(G_OBJECT(s->view),"insight-project-root");gchar *relative=NULL;
   if(root && path && *path) { GFile *base=g_file_new_for_path(root),*file=g_file_new_for_path(path);relative=g_file_get_relative_path(base,file);g_object_unref(base);g_object_unref(file); }
   g_object_set(renderer,"text",relative ? relative : (path ? path : ""),NULL);g_free(relative);
  }
  if(field==KIND) { gchar *text=group ? g_strdup("") : g_strdup_printf("%s (%s)",insight_symbol_type_name(type,FALSE),kind && g_str_has_prefix(kind,"@decl:") ? strrchr(kind,':')+1 : (kind ? kind : "")); g_object_set(renderer,"text",text,NULL); g_free(text); }
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
  g_object_set(renderer,"foreground",color,"foreground-set",!selected,"weight",(group || (field==NAME && g_object_get_data(G_OBJECT(s->view),"insight-bold-symbol-names"))) ? PANGO_WEIGHT_BOLD : PANGO_WEIGHT_NORMAL,NULL);
 }
 g_free(path); g_free(kind);
}
void insight_symbol_tree_format(GtkTreeView *view) {
 GList *columns=gtk_tree_view_get_columns(view);
 for(GList *p=columns;p;p=p->next) {
  GtkTreeViewColumn *column=p->data; gint field=gtk_tree_view_column_get_sort_column_id(column); g_object_set_data(G_OBJECT(column),"insight-symbol-field",GINT_TO_POINTER(field));
  if(field==NAME) { GtkCellRenderer *icon=gtk_cell_renderer_pixbuf_new(); gtk_tree_view_column_pack_start(column,icon,FALSE); gtk_cell_layout_reorder(GTK_CELL_LAYOUT(column),icon,0); }
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));
  for(GList *r=cells;r;r=r->next) { Symbols *s=g_new0(Symbols,1); s->view=view; gtk_tree_view_column_set_cell_data_func(column,r->data,cell,s,g_free); } g_list_free(cells);
 } g_list_free(columns);
}
static void cancel(Symbols *s) {
 if(s->timer) { g_source_remove(s->timer); s->timer=0; } if(s->idle) { g_source_remove(s->idle); s->idle=0; }
 g_clear_object(&s->pending); g_clear_pointer(&s->groups,g_hash_table_unref); g_clear_pointer(&s->scope_groups,g_hash_table_unref);g_clear_pointer(&s->owners,g_hash_table_unref);
}
typedef struct { gchar *name,*path,*kind; gulong line; GtkTreeSelection *selection; } Choice;
static gboolean restore_choice(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 Choice *choice=data; gchar *name=NULL,*file=NULL,*kind=NULL; gulong line=0; gtk_tree_model_get(model,iter,NAME,&name,PATH,&file,LINE,&line,KIND,&kind,-1); gboolean match=choice->line ? line==choice->line && !g_strcmp0(name,choice->name) && !g_strcmp0(file,choice->path) : !line && choice->kind && !g_strcmp0(kind,choice->kind);
 if(match) { GtkWidget *view=GTK_WIDGET(gtk_tree_selection_get_tree_view(choice->selection));gtk_tree_view_expand_to_path(GTK_TREE_VIEW(view),path);gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE); }
 g_free(name); g_free(file);g_free(kind); return match;
}
static void page_button(Symbols *s) { if(s->more) { if(s->more_step) { gchar *tip=insight_i18n_printf("Load the next %u symbols",s->more_step);gtk_widget_set_tooltip_text(s->more,tip);g_free(tip); } gtk_widget_set_visible(s->more,s->has_more); gtk_widget_set_sensitive(s->more,gtk_widget_get_sensitive(GTK_WIDGET(s->view))); } }
typedef struct { GtkTreeView *view;GHashTable *collapsed;gboolean remember; } Expansion;
static gboolean preserve_expansion(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) {
 Expansion *state=data;if(!gtk_tree_model_iter_has_child(model,iter)) return FALSE;
 gchar *kind=NULL,*name=NULL,*file=NULL,*scope=NULL;gulong line=0;gtk_tree_model_get(model,iter,KIND,&kind,NAME,&name,PATH,&file,SCOPE,&scope,LINE,&line,-1);
 if(line) { gchar *key=g_strdup_printf("%s\037%s\037%s\037%s",kind ? kind : "",file ? file : "",scope ? scope : "",name ? name : "");g_free(kind);kind=key; }g_free(name);g_free(file);g_free(scope);
 if(state->remember) { if(kind && !gtk_tree_view_row_expanded(state->view,path)) g_hash_table_add(state->collapsed,g_strdup(kind)); }
 else if(!kind || !g_hash_table_contains(state->collapsed,kind)) gtk_tree_view_expand_row(state->view,path,FALSE);
 g_free(kind);return FALSE;
}
static void publish(Symbols *s,GtkTreeModel *model) {
 GtkTreeSelection *selection=gtk_tree_view_get_selection(s->view); GtkTreeModel *old=gtk_tree_view_get_model(s->view); GtkTreeIter iter; Choice choice={.selection=selection};
 if(gtk_tree_selection_get_selected(selection,NULL,&iter)) gtk_tree_model_get(old,&iter,NAME,&choice.name,PATH,&choice.path,LINE,&choice.line,KIND,&choice.kind,-1);
 GHashTable *collapsed=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,NULL);
 Expansion expansion={s->view,collapsed,TRUE};if(GTK_IS_TREE_STORE(old)) gtk_tree_model_foreach(old,preserve_expansion,&expansion);
 guint signal=g_signal_lookup("changed",GTK_TYPE_TREE_SELECTION); g_signal_handlers_block_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL);
 gtk_tree_view_set_model(s->view,model);
 expansion.remember=FALSE;if(GTK_IS_TREE_STORE(model)) gtk_tree_model_foreach(model,preserve_expansion,&expansion);
 if(choice.name || choice.kind) gtk_tree_model_foreach(model,restore_choice,&choice);
 g_signal_handlers_unblock_matched(selection,G_SIGNAL_MATCH_ID,signal,0,NULL,NULL,NULL); g_hash_table_unref(collapsed); g_free(choice.name); g_free(choice.path); g_free(choice.kind); gtk_widget_set_sensitive(GTK_WIDGET(s->view),!s->loading); page_button(s);
}
static void status(Symbols *s) {
 if(!s->status) return;
 guint total=gtk_tree_model_iter_n_children(GTK_TREE_MODEL(s->source),NULL);
 gchar *text=insight_i18n_printf(s->has_more ? "Symbols: %u / %u+" : "Symbols: %u / %u",s->hierarchical || s->grouped || s->mask!=ALL_TYPES ? s->shown : total,total);
 gtk_label_set_text(GTK_LABEL(s->status),text); g_free(text);
}
static void queue(Symbols *s);
static void grouped_sort(GtkTreeSortable *model,gpointer data) {
 Symbols *s=g_object_get_data(G_OBJECT(data),"insight-symbol-view"); if(!s || s->sorting) return; gint column=-1; GtkSortType direction; if(!gtk_tree_sortable_get_sort_column_id(model,&column,&direction) || column<0 || column>=COLUMNS) return;
 s->sorting=TRUE; gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(s->source),column,direction); s->sorting=FALSE; queue(s);
}
static GtkTreeIter *scope_parent(Symbols *s,GtkTreeIter *parent,const gchar *scope,const gchar *path,guint type) {
 if(!s->hierarchical || !scope || !*scope) return parent;
 GeanyDocument *doc=path && *path ? document_find_by_filename(path) : NULL;
 GeanyFiletype *ft=doc ? doc->file_type : (path && *path ? insight_languages_detect(path) : s->fallback);
 if(!ft && path && *path) ft=filetypes_detect_from_extension(path);
 gint lang=doc && doc->tm_file ? doc->tm_file->lang : (ft ? ft->lang : -1);
 const gchar *separator=lang>=0 ? tm_parser_scope_separator_printable(lang) : "::";
 if(!separator || !*separator) separator="::";
 gchar **parts=g_strsplit(scope,separator,64);GString *qualified=g_string_new(NULL);
 for(guint i=0;parts[i];i++) {
  if(!*parts[i]) continue;
  if(qualified->len) g_string_append(qualified,separator);
  g_string_append(qualified,parts[i]);
  gchar *key=g_strdup_printf("@scope:%u:%d:%s:%s",s->grouped ? type : 0,lang,path ? path : "",qualified->str);
  GtkTreeIter *node=g_hash_table_lookup(s->scope_groups,key);
  if(!node) {
   node=g_new(GtkTreeIter,1);gtk_tree_store_append(s->pending,node,parent);
   gtk_tree_store_set(s->pending,node,NAME,parts[i],SCOPE,qualified->str,KIND,key,-1);
   g_hash_table_insert(s->scope_groups,key,node);
  } else g_free(key);
  parent=node;
 }
 g_string_free(qualified,TRUE);g_strfreev(parts);return parent;
}
static gboolean source_order(Symbols *s) {
 gint column=-1;GtkSortType direction;
 return g_object_get_data(G_OBJECT(s->view),"insight-source-nesting") && !s->grouped && !s->hierarchical && gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(s->source),&column,&direction) && column==LINE;
}
static gchar *owner_key(const gchar *path,const gchar *scope) { return g_strdup_printf("%s\037%s",path ? path : "",scope ? scope : ""); }
static const gchar *scope_separator(Symbols *s,const gchar *path) {
 GeanyDocument *doc=path ? document_find_by_filename(path) : NULL;GeanyFiletype *ft=doc ? doc->file_type : s->fallback;
 const gchar *sep=ft ? tm_parser_scope_separator_printable(ft->lang) : "::";return sep && *sep ? sep : "::";
}
static gboolean owns_scope(guint type) { return (type&(tm_tag_class_t|tm_tag_struct_t|tm_tag_union_t|tm_tag_enum_t|tm_tag_namespace_t|tm_tag_function_t|tm_tag_method_t))!=0; }
static gchar *qualified_key(Symbols *s,const gchar *path,const gchar *scope,const gchar *name) {
 gchar *qualified=scope && *scope ? g_strconcat(scope,scope_separator(s,path),name,NULL) : g_strdup(name);gchar *key=owner_key(path,qualified);g_free(qualified);return key;
}
static GtkTreeIter *real_owner(Symbols *s,const gchar *key,guint depth) {
 GtkTreeIter *node=g_hash_table_lookup(s->scope_groups,key);if(node || depth>64) return node;
 GtkTreeIter *source=g_hash_table_lookup(s->owners,key);if(!source) return NULL;
 gchar *name=NULL,*scope=NULL,*path=NULL,*kind=NULL;gulong line=0;gtk_tree_model_get(GTK_TREE_MODEL(s->source),source,NAME,&name,SCOPE,&scope,PATH,&path,LINE,&line,KIND,&kind,-1);
 GtkTreeIter *parent=NULL;if(scope && *scope) { gchar *parent_key=owner_key(path,scope);if(strcmp(parent_key,key)) parent=real_owner(s,parent_key,depth+1);g_free(parent_key); }
 node=g_new(GtkTreeIter,1);gtk_tree_store_append(s->pending,node,parent);gtk_tree_store_set(s->pending,node,NAME,name,SCOPE,scope,PATH,path,LINE,line,KIND,kind,-1);g_hash_table_insert(s->scope_groups,g_strdup(key),node);
 g_free(name);g_free(scope);g_free(path);g_free(kind);return node;
}
static gboolean apply(gpointer data) {
 Symbols *s=data; gint64 deadline=g_get_monotonic_time()+3000;
 if(s->collecting_owners) {
  while(s->valid) { gchar *name=NULL,*scope=NULL,*path=NULL,*kind=NULL;gtk_tree_model_get(GTK_TREE_MODEL(s->source),&s->iter,NAME,&name,SCOPE,&scope,PATH,&path,KIND,&kind,-1);guint type=type_for(s,path,kind);
   if(owns_scope(type) && (s->mask&(1u<<filter_category(type)))) { gchar *key=qualified_key(s,path,scope,name);GtkTreeIter *copy=g_new(GtkTreeIter,1);*copy=s->iter;if(g_hash_table_contains(s->owners,key)) { g_free(copy);g_hash_table_replace(s->owners,key,NULL); }else g_hash_table_insert(s->owners,key,copy); }
   g_free(name);g_free(scope);g_free(path);g_free(kind);s->valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(s->source),&s->iter);if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
  }s->collecting_owners=FALSE;s->valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(s->source),&s->iter);
 }
 while(s->valid) {
  gchar *name=NULL,*scope=NULL,*path=NULL,*kind=NULL; gulong line=0; gtk_tree_model_get(GTK_TREE_MODEL(s->source),&s->iter,NAME,&name,SCOPE,&scope,PATH,&path,LINE,&line,KIND,&kind,-1); guint type=type_for(s,path,kind);
  if(type!=tm_tag_undef_t && !(type==tm_tag_local_var_t && kind && !strcmp(kind,"z")) && (s->mask&(1u<<filter_category(type)))) {
   GtkTreeIter *parent=NULL;
   if(s->grouped) {
    parent=g_hash_table_lookup(s->groups,GUINT_TO_POINTER(type+1));
    if(!parent) { parent=g_new(GtkTreeIter,1); gtk_tree_store_append(s->pending,parent,NULL); gchar *group=g_strdup_printf("@type:%u",type); gtk_tree_store_set(s->pending,parent,NAME,insight_symbol_type_name(type,TRUE),KIND,group,-1); g_free(group); g_hash_table_insert(s->groups,GUINT_TO_POINTER(type+1),parent); }
   }
   parent=scope_parent(s,parent,scope,path,type);
   if(source_order(s) && scope && *scope) { gchar *key=owner_key(path,scope);parent=real_owner(s,key,0);g_free(key); }
   gboolean existing_owner=FALSE;
   if(source_order(s) && owns_scope(type)) { gchar *key=qualified_key(s,path,scope,name);existing_owner=real_owner(s,key,0)!=NULL;g_free(key); }
   if(!existing_owner) { GtkTreeIter child;gtk_tree_store_append(s->pending,&child,parent);gtk_tree_store_set(s->pending,&child,NAME,name,SCOPE,scope,PATH,path,LINE,line,KIND,kind,-1); }s->shown++;
  }
  g_free(name); g_free(scope); g_free(path); g_free(kind);
  s->valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(s->source),&s->iter); if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
 }
 gint column=-1; GtkSortType direction; if(gtk_tree_sortable_get_sort_column_id(GTK_TREE_SORTABLE(s->source),&column,&direction)) gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(s->pending),column,direction);
 g_signal_connect_object(s->pending,"sort-column-changed",G_CALLBACK(grouped_sort),s->view,0); /* callback below retrieves state from the view */
 publish(s,GTK_TREE_MODEL(s->pending)); status(s); g_clear_object(&s->pending); g_clear_pointer(&s->groups,g_hash_table_unref); g_clear_pointer(&s->scope_groups,g_hash_table_unref);g_clear_pointer(&s->owners,g_hash_table_unref); s->idle=0; return G_SOURCE_REMOVE;
}
static gboolean begin(gpointer data) {
 Symbols *s=data; s->timer=0;
 s->shown=0;
 s->pending=gtk_tree_store_new(COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_STRING); s->groups=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free); s->scope_groups=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); s->owners=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);s->collecting_owners=source_order(s);s->valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(s->source),&s->iter); s->idle=g_idle_add(apply,s); return G_SOURCE_REMOVE;
}
static void queue(Symbols *s) { if(!s) return; cancel(s); gtk_widget_set_sensitive(GTK_WIDGET(s->view),FALSE); s->timer=g_timeout_add(100,begin,s); page_button(s); }
static void row_changed(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gpointer data) { (void)model; (void)path; (void)iter; queue(g_object_get_data(G_OBJECT(data),"insight-symbol-view")); }
static void row_deleted(GtkTreeModel *model,GtkTreePath *path,gpointer data) { (void)model; (void)path; queue(g_object_get_data(G_OBJECT(data),"insight-symbol-view")); }
static void reordered(GtkTreeModel *model,GtkTreePath *path,GtkTreeIter *iter,gint *order,gpointer data) { (void)model; (void)path; (void)iter; (void)order; queue(g_object_get_data(G_OBJECT(data),"insight-symbol-view")); }
static void scopes_toggled(GtkToggleButton *toggle,gpointer data) { Symbols *s=g_object_get_data(G_OBJECT(data),"insight-symbol-view");if(!s) return;s->hierarchical=gtk_toggle_button_get_active(toggle);queue(s); }
static void toggled(GtkToggleButton *toggle,gpointer data) { Symbols *s=g_object_get_data(G_OBJECT(data),"insight-symbol-view"); if(!s) return; s->grouped=gtk_toggle_button_get_active(toggle); cancel(s);
 queue(s); }
static void freed(gpointer data) { Symbols *s=data; cancel(s); views=g_list_remove(views,s); g_object_unref(s->source); g_free(s); }
static const gint column_widths[]={220,160,260,70,70};
void insight_symbol_view_columns_configure(GtkTreeView *view,const gint *widths,guint count) {
 GList *columns=gtk_tree_view_get_columns(view);guint position=0;
 for(GList *p=columns;p;p=p->next) { GtkTreeViewColumn *column=p->data;gint field=gtk_tree_view_column_get_sort_column_id(column);g_object_set_data(G_OBJECT(column),"insight-symbol-column-position",GUINT_TO_POINTER(++position));gint width=field>=0 && (guint)field<count ? widths[field] : 100;g_object_set_data(G_OBJECT(column),"insight-column-default-width",GINT_TO_POINTER(width));gtk_tree_view_column_set_fixed_width(column,width);gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED);gtk_tree_view_column_set_resizable(column,TRUE);gtk_tree_view_column_set_reorderable(column,TRUE);GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));for(GList *c=cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_END,NULL);g_list_free(cells); }
 g_list_free(columns);
}
static void column_check(GtkCheckMenuItem *item,gpointer data) { GtkTreeViewColumn *column=data;if(gtk_tree_view_column_get_sort_column_id(column)!=NAME) gtk_tree_view_column_set_visible(column,gtk_check_menu_item_get_active(item)); }
static void column_visible(GObject *column,GParamSpec *spec,gpointer data) { (void)spec;gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(data),gtk_tree_view_column_get_visible(GTK_TREE_VIEW_COLUMN(column))); }
GtkWidget *insight_symbol_view_columns_menu(GtkTreeView *view) {
 GtkWidget *menu=gtk_menu_new();GList *columns=gtk_tree_view_get_columns(view);
 for(GList *p=columns;p;p=p->next) { GtkTreeViewColumn *column=p->data;gint field=gtk_tree_view_column_get_sort_column_id(column);GtkWidget *item=gtk_check_menu_item_new_with_label(gtk_tree_view_column_get_title(column));gchar *name=g_strdup_printf("insight-column-%d",field);gtk_widget_set_name(item,name);g_free(name);GtkWidget *label=gtk_bin_get_child(GTK_BIN(item)),*box=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);g_object_ref(label);gtk_container_remove(GTK_CONTAINER(item),label);gtk_box_pack_start(GTK_BOX(box),gtk_image_new_from_icon_name("view-list-symbolic",GTK_ICON_SIZE_MENU),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),label,TRUE,TRUE,0);g_object_unref(label);gtk_container_add(GTK_CONTAINER(item),box);g_object_set_data(G_OBJECT(item),"insight-icon-added",GINT_TO_POINTER(1));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),gtk_tree_view_column_get_visible(column));gtk_widget_set_sensitive(item,field!=NAME);g_signal_connect_object(item,"toggled",G_CALLBACK(column_check),column,0);g_signal_connect_object(column,"notify::visible",G_CALLBACK(column_visible),item,0);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 g_list_free(columns);return menu;
}
void insight_symbol_view_columns_reset(GtkTreeView *view) {
 GList *columns=gtk_tree_view_get_columns(view);GtkTreeViewColumn *previous=NULL;
 for(guint i=1;i<=g_list_length(columns);i++) for(GList *p=columns;p;p=p->next) {
  GtkTreeViewColumn *column=p->data;if(GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(column),"insight-symbol-column-position"))!=i) continue;
  gtk_tree_view_column_set_fixed_width(column,GPOINTER_TO_INT(g_object_get_data(G_OBJECT(column),"insight-column-default-width")));gtk_tree_view_column_set_visible(column,TRUE);
  gtk_tree_view_move_column_after(view,column,previous);previous=column;break;
 }
 g_list_free(columns);
}
void insight_symbol_view_columns_write(GtkTreeView *view,GKeyFile *settings,const gchar *prefix) {
 GList *columns=gtk_tree_view_get_columns(view);gint order[COLUMNS],widths[COLUMNS],visible[COLUMNS];guint count=0;
 for(GList *p=columns;p && count<COLUMNS;p=p->next,count++) { GtkTreeViewColumn *column=p->data;order[count]=gtk_tree_view_column_get_sort_column_id(column);widths[count]=CLAMP(gtk_tree_view_column_get_fixed_width(column),32,2000);visible[count]=gtk_tree_view_column_get_visible(column); }
 gchar *key=g_strconcat(prefix,"-column-order",NULL);g_key_file_set_integer_list(settings,"workbench",key,order,count);g_free(key);
 key=g_strconcat(prefix,"-column-widths",NULL);g_key_file_set_integer_list(settings,"workbench",key,widths,count);g_free(key);key=g_strconcat(prefix,"-column-visible",NULL);g_key_file_set_integer_list(settings,"workbench",key,visible,count);g_free(key);g_list_free(columns);
}
void insight_symbol_view_columns_read(GtkTreeView *view,GKeyFile *settings,const gchar *prefix) {
 gchar *key=g_strconcat(prefix,"-column-order",NULL);gsize count=0,size=0;gint *order=g_key_file_get_integer_list(settings,"workbench",key,&count,NULL);g_free(key);
 key=g_strconcat(prefix,"-column-widths",NULL);gint *widths=g_key_file_get_integer_list(settings,"workbench",key,&size,NULL);g_free(key);
 key=g_strconcat(prefix,"-column-visible",NULL);gsize visible_count=0;gint *visible=g_key_file_get_integer_list(settings,"workbench",key,&visible_count,NULL);g_free(key);gboolean valid_visible=visible && visible_count==count;if(valid_visible) for(gsize i=0;i<count;i++) if(visible[i]!=0 && visible[i]!=1) valid_visible=FALSE;
 GList *columns=gtk_tree_view_get_columns(view);guint expected=0,seen=0;gboolean valid=order && widths && count==size && count==g_list_length(columns) && count<=COLUMNS;
 for(GList *p=columns;p;p=p->next) { gint field=gtk_tree_view_column_get_sort_column_id(p->data);if(field<0 || field>=COLUMNS) valid=FALSE;else expected|=1u<<field; }
 if(valid) for(gsize i=0;i<count;i++) { if(order[i]<0 || order[i]>=COLUMNS || (seen&(1u<<order[i])) || widths[i]<32 || widths[i]>2000) { valid=FALSE;break; }seen|=1u<<order[i]; }
 if(valid && seen==expected) { GtkTreeViewColumn *previous=NULL;for(gsize i=0;i<count;i++) for(GList *p=columns;p;p=p->next) { GtkTreeViewColumn *column=p->data;if(gtk_tree_view_column_get_sort_column_id(column)!=order[i]) continue;gtk_tree_view_column_set_fixed_width(column,widths[i]);if(valid_visible) gtk_tree_view_column_set_visible(column,order[i]==NAME || visible[i]);gtk_tree_view_move_column_after(view,column,previous);previous=column;break; } }
 g_list_free(columns);g_free(order);g_free(widths);g_free(visible);
}
GtkWidget *insight_symbol_view_attach(GtkTreeView *view,GtkListStore *source,GeanyFiletype *fallback) {
 Symbols *s=g_new0(Symbols,1); s->view=view; s->source=g_object_ref(source); s->fallback=fallback; s->mask=ALL_TYPES & ~(1u<<LOCALS); s->toggle=gtk_check_button_new_with_label("Group by symbol type"); g_object_set_data_full(G_OBJECT(view),"insight-symbol-view",s,freed); g_signal_connect_object(s->toggle,"toggled",G_CALLBACK(toggled),view,0);
 insight_symbol_view_columns_configure(view,column_widths,G_N_ELEMENTS(column_widths));
 GList *columns=gtk_tree_view_get_columns(view);
 for(GList *p=columns;p;p=p->next) { GtkTreeViewColumn *column=p->data; gint field=gtk_tree_view_column_get_sort_column_id(column); g_object_set_data(G_OBJECT(column),"insight-symbol-field",GINT_TO_POINTER(field)); GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column)); for(GList *c=cells;c;c=c->next) { if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_END,NULL);gtk_tree_view_column_set_cell_data_func(column,c->data,cell,s,NULL); } g_list_free(cells); if(field==NAME) { GtkCellRenderer *icon=gtk_cell_renderer_pixbuf_new(); gtk_tree_view_column_pack_start(column,icon,FALSE); gtk_cell_layout_reorder(GTK_CELL_LAYOUT(column),icon,0); gtk_tree_view_column_set_cell_data_func(column,icon,cell,s,NULL); } }
 g_list_free(columns); g_signal_connect_object(source,"row-changed",G_CALLBACK(row_changed),view,0); g_signal_connect_object(source,"row-inserted",G_CALLBACK(row_changed),view,0); g_signal_connect_object(source,"row-deleted",G_CALLBACK(row_deleted),view,0); g_signal_connect_object(source,"rows-reordered",G_CALLBACK(reordered),view,0); views=g_list_prepend(views,s); return s->toggle;
}
GtkWidget *insight_symbol_view_scopes_control(GtkTreeView *view) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view");if(!s) return NULL;
 if(!s->scope_toggle) { s->scope_toggle=gtk_check_button_new_with_label("Scope hierarchy");g_signal_connect_object(s->scope_toggle,"toggled",G_CALLBACK(scopes_toggled),view,0); }
 return s->scope_toggle;
}
gboolean insight_symbol_view_scopes(GtkTreeView *view) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view");return s && s->hierarchical; }
void insight_symbol_view_set_scopes(GtkTreeView *view,gboolean enabled) { GtkWidget *toggle=insight_symbol_view_scopes_control(view);if(toggle) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),enabled); }
void insight_symbol_view_filetype(GtkTreeView *view,GeanyFiletype *ft) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(s && s->fallback!=ft) { s->fallback=ft; queue(s); gtk_widget_queue_draw(GTK_WIDGET(view)); } }
gboolean insight_symbol_view_grouped(GtkTreeView *view) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); return s && s->grouped; }
void insight_symbol_view_set_grouped(GtkTreeView *view,gboolean enabled) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(s) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(s->toggle),enabled); }
void insight_symbol_views_locale_changed(void) { for(GList *p=views;p;p=p->next) { Symbols *s=p->data; if(s->menu) insight_i18n_apply(s->menu); queue(s); gtk_widget_queue_draw(GTK_WIDGET(s->view)); } }

static void filter_changed(GtkCheckMenuItem *item,gpointer data) {
 (void)item; Symbols *s=g_object_get_data(G_OBJECT(data),"insight-symbol-view"); if(!s || s->setting) return;
 s->mask=0; for(guint i=0;i<FILTER_COUNT;i++) if(gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(s->checks[i]))) s->mask|=1u<<i;

 queue(s);
}
void insight_symbol_view_set_types(GtkTreeView *view,guint mask) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(!s) return;
 s->mask=mask&ALL_TYPES; s->setting=TRUE;
 for(guint i=0;i<FILTER_COUNT;i++) if(s->checks[i]) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(s->checks[i]),(s->mask&(1u<<i))!=0);
 s->setting=FALSE;
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
 for(guint i=0;i<FILTER_COUNT;i++) { GtkWidget *item=gtk_check_menu_item_new_with_label(filter_names[i]); GtkWidget *label=gtk_bin_get_child(GTK_BIN(item)),*content=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6); g_object_ref(label); gtk_container_remove(GTK_CONTAINER(item),label); gtk_box_pack_start(GTK_BOX(content),gtk_image_new_from_icon_name(filter_icons[i],GTK_ICON_SIZE_MENU),FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(content),label,TRUE,TRUE,0); g_object_unref(label); gtk_container_add(GTK_CONTAINER(item),content); g_object_set_data(G_OBJECT(item),"insight-icon-added",GINT_TO_POINTER(1)); gchar *name=g_strdup_printf("insight-symbol-type-%u",i); gtk_widget_set_name(item,name); g_free(name); s->checks[i]=item; gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),(s->mask&(1u<<i))!=0); g_signal_connect_object(item,"toggled",G_CALLBACK(filter_changed),view,0); gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 gtk_widget_show_all(menu); gtk_box_pack_start(GTK_BOX(box),button,FALSE,FALSE,0); s->status=gtk_label_new(""); gtk_label_set_xalign(GTK_LABEL(s->status),0); gtk_widget_set_tooltip_text(s->status,"Visible / loaded symbols; + means more query matches remain"); gtk_box_pack_start(GTK_BOX(box),s->status,TRUE,TRUE,0);
 s->more=gtk_button_new_from_icon_name("list-add-symbolic",GTK_ICON_SIZE_MENU); gtk_widget_set_name(s->more,"insight-symbol-more"); gtk_button_set_relief(GTK_BUTTON(s->more),GTK_RELIEF_NONE); gtk_widget_set_tooltip_text(s->more,"Load more symbols"); gtk_widget_show_all(s->more); gtk_widget_set_no_show_all(s->more,TRUE); gtk_box_pack_end(GTK_BOX(box),s->more,FALSE,FALSE,0); page_button(s); status(s); return box;
}

void insight_symbol_view_loading(GtkTreeView *view,gboolean loading) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view"); if(!s) return;
 s->loading=loading;
 gboolean deriving=(s->timer || s->idle) && (s->hierarchical || s->grouped || s->mask!=ALL_TYPES);
 gtk_widget_set_sensitive(GTK_WIDGET(view),!loading && !deriving); page_button(s);
}

void insight_symbol_view_filter_selected(GtkTreeView *view,gboolean only) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view");if(!s) return;
 GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),NULL,&iter)) return;
 gchar *path=NULL,*kind=NULL;gtk_tree_model_get(model,&iter,PATH,&path,KIND,&kind,-1);
 guint bit=1u<<filter_category(type_for(s,path,kind));g_free(path);g_free(kind);insight_symbol_view_set_types(view,only ? bit : s->mask&~bit);
}

void insight_symbol_view_more_loader(GtkTreeView *view,GCallback callback,guint step) {
 Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view");if(!s || !s->more) return;
 s->more_step=step;page_button(s);g_signal_connect_object(s->more,"clicked",callback,view,0);
}
void insight_symbol_view_page_state(GtkTreeView *view,gboolean more) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view");if(s) { s->has_more=more;page_button(s);status(s); } }
gboolean insight_symbol_view_has_more(GtkTreeView *view) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view");return s && s->has_more; }
void insight_symbol_view_load_more(GtkTreeView *view) { Symbols *s=g_object_get_data(G_OBJECT(view),"insight-symbol-view");if(s && s->more && s->has_more && gtk_widget_get_sensitive(s->more) && !s->loading) gtk_button_clicked(GTK_BUTTON(s->more)); }
