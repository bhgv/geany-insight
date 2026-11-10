/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Relation history and navigation commands.
 * RU: История связей и команды навигации. */

static void relation_travel(gboolean forward,guint distance) {
 GPtrArray *from=forward ? wb.relation_future : wb.relation_history,*to=forward ? wb.relation_history : wb.relation_future;
 if(!distance || distance>from->len) return;
 relation_visit_push(to,relation_visit_current());
 /* Move intervening visits without issuing intermediate asynchronous queries. */
 while(distance-->1) relation_visit_push(to,relation_visit_pop(from));
 relation_visit_restore(relation_visit_pop(from));
}
static void relation_back(GtkWidget *widget,gpointer data) { (void)widget;(void)data;relation_travel(FALSE,1); }
static void relation_forward(GtkWidget *widget,gpointer data) { (void)widget;(void)data;relation_travel(TRUE,1); }
static void relation_history_pick(GtkMenuItem *item,gpointer data) {
 (void)data;relation_travel(GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"forward")),GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"distance")));
}
static void relation_history_clear(GtkMenuItem *item,gpointer data) {
 (void)item;(void)data;g_ptr_array_set_size(wb.relation_history,0);g_ptr_array_set_size(wb.relation_future,0);relation_history_state();
}
static void relation_history_show(GtkWidget *menu,gpointer data) {
 (void)data;GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 GPtrArray *stacks[]={wb.relation_history,wb.relation_future};const gchar *headings[]={"Back","Forward"};
 for(guint side=0;side<2;side++) {
  GtkWidget *heading=gtk_menu_item_new_with_label(headings[side]);gtk_widget_set_sensitive(heading,FALSE);gtk_menu_shell_append(GTK_MENU_SHELL(menu),heading);
  GPtrArray *stack=stacks[side];for(guint i=0;i<stack->len;i++) {
   RelationVisit *visit=g_ptr_array_index(stack,stack->len-1-i);GtkTreeModel *modes=gtk_combo_box_get_model(GTK_COMBO_BOX(wb.relation_mode));GtkTreeIter mode_iter;gchar *method=NULL;
   if(gtk_tree_model_iter_nth_child(modes,&mode_iter,NULL,visit->mode)) gtk_tree_model_get(modes,&mode_iter,0,&method,-1);
   gchar *short_query=g_utf8_substring(visit->query,0,MIN((glong)64,g_utf8_strlen(visit->query,-1))),*label=g_strdup_printf("%u. %s — %s",i+1,short_query,method ? method : "");g_free(short_query);g_free(method);
   GtkWidget *item=gtk_menu_item_new_with_label(label);g_free(label);gchar *name=g_strdup_printf("insight-relation-history-%s-%u",side ? "forward" : "back",i+1);gtk_widget_set_name(item,name);g_free(name);gtk_widget_set_tooltip_text(item,visit->query);g_object_set_data(G_OBJECT(item),"insight-no-i18n",GINT_TO_POINTER(1));g_object_set_data(G_OBJECT(item),"forward",GUINT_TO_POINTER(side));g_object_set_data(G_OBJECT(item),"distance",GUINT_TO_POINTER(i+1));g_signal_connect(item,"activate",G_CALLBACK(relation_history_pick),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
  }
 }
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());GtkWidget *clear=gtk_menu_item_new_with_label("Clear relation history");gtk_widget_set_name(clear,"insight-relation-history-clear");gtk_widget_set_sensitive(clear,wb.relation_history->len || wb.relation_future->len);g_signal_connect(clear,"activate",G_CALLBACK(relation_history_clear),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),clear);insight_i18n_apply(menu);gtk_widget_show_all(menu);
}
static void relation_selected_query(GtkWidget *widget,gpointer data) {
 (void)widget;GtkTreeModel *model;GtkTreeIter iter;
 gboolean tree_view=g_str_has_prefix(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"tree");GtkTreeView *view=tree_view ? insight_relation_tree_view(wb.relation_tree) : GTK_TREE_VIEW(wb.usage_view);
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) { ui_set_statusbar(TRUE,"Select a relation row first");return; }
 gchar *name=NULL;gulong line=0;gtk_tree_model_get(model,&iter,USAGE_SYMBOL,&name,USAGE_LINE,&line,-1);if(tree_view && !line) { g_free(name);return; }
 if(!name || !*name) { g_free(name);return; }
 relation_visit_push(wb.relation_history,relation_visit_current());g_ptr_array_set_size(wb.relation_future,0);relation_history_state();
 if(tree_view && (GPOINTER_TO_INT(data)==9 || GPOINTER_TO_INT(data)==10) && gtk_tree_model_get_n_columns(model)>13) {gchar *usr=NULL,*source=NULL;gtk_tree_model_get(model,&iter,10,&usr,13,&source,-1);if(usr && *usr) {g_object_set_data(G_OBJECT(wb.usage_entry),"insight-semantic-restore",GINT_TO_POINTER(1));g_object_set_data_full(G_OBJECT(wb.usage_entry),"insight-semantic-usr",usr,g_free);g_object_set_data_full(G_OBJECT(wb.usage_entry),"insight-semantic-source",source,g_free);}else {g_free(usr);g_free(source);}}
 wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);
 gtk_entry_set_text(GTK_ENTRY(wb.relation_filter_entry),"");gtk_entry_set_text(GTK_ENTRY(wb.usage_entry),name);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),GPOINTER_TO_INT(data));wb.relation_setting=FALSE;
 g_free(name);relation_controls(NULL,NULL);
}
static gboolean relation_source_selected(GtkTreeView *view) {
 GtkTreeModel *model;GtkTreeIter iter;gulong line=0;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) return FALSE;gtk_tree_model_get(model,&iter,USAGE_LINE,&line,-1);return line>0;
}
static void relation_node_action(gboolean restore,gpointer data) {
 (void)data;if(restore) { usage_start(NULL,NULL);return; }
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));gboolean tree=shown && g_str_has_prefix(shown,"tree");GtkTreeView *view=tree ? insight_relation_tree_view(wb.relation_tree) : GTK_TREE_VIEW(wb.usage_view);
 if(!relation_source_selected(view)) { set_usage_status("Select a source node; query roots and notices cannot be removed");return; }
 wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_follow),FALSE);wb.relation_setting=FALSE;stop_usage();
 gboolean removed=FALSE;
 if(tree) removed=insight_relation_tree_remove_selected(wb.relation_tree);
 else {
  GtkTreeModel *model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) {
   /* Selection coordinates belong to the visible sorted/filtered model. */
   while(GTK_IS_TREE_MODEL_SORT(model) || GTK_IS_TREE_MODEL_FILTER(model)) { GtkTreeIter child;
    if(GTK_IS_TREE_MODEL_SORT(model)) { gtk_tree_model_sort_convert_iter_to_child_iter(GTK_TREE_MODEL_SORT(model),&child,&iter);model=gtk_tree_model_sort_get_model(GTK_TREE_MODEL_SORT(model)); }
    else { gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER(model),&child,&iter);model=gtk_tree_model_filter_get_model(GTK_TREE_MODEL_FILTER(model)); }iter=child;
   }
   if(model==GTK_TREE_MODEL(wb.matches)) { gtk_list_store_remove(wb.matches,&iter);removed=TRUE; }
  }
 }
 if(removed) set_usage_status("Node removed from view; refresh to restore");
}
static void relation_node_button(GtkWidget *widget,gpointer data) { (void)widget;relation_node_action(GPOINTER_TO_INT(data),NULL); }
static void relation_node_menu_free(gpointer data) { gtk_widget_destroy(GTK_WIDGET(data));g_object_unref(data); }
static GtkWidget *relation_node_menu(GtkTreeView *view) {
 GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-node-menu");
 if(!menu) {
  menu=gtk_menu_new();g_object_ref_sink(menu);const gchar *labels[]={"Remove node from view","Restore nodes (refresh)"};
  for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,i ? "insight-relation-node-restore-menu" : "insight-relation-node-remove-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_node_button),GUINT_TO_POINTER(i)); }
  const gchar *reports[]={"View Relation Outline…","Create Bookmarks from Relation Items…"};for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label(reports[i]);gtk_widget_set_name(item,i ? "insight-relation-bookmark-menu":"insight-relation-outline-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(relation_report_show),GUINT_TO_POINTER(i)); }
  GtkWidget *print=gtk_menu_item_new_with_label("Print Relation Window…");gtk_widget_set_name(print,"insight-relation-print-menu");gtk_menu_shell_append(GTK_MENU_SHELL(menu),print);g_signal_connect(print,"activate",G_CALLBACK(relation_print_show),NULL);
  insight_list_tools_menu(view,menu);g_object_set_data_full(G_OBJECT(view),"insight-node-menu",menu,relation_node_menu_free);
 }
 GList *items=gtk_container_get_children(GTK_CONTAINER(menu));gtk_widget_set_sensitive(items->data,relation_source_selected(view));g_list_free(items);insight_i18n_apply(menu);gtk_widget_show_all(menu);return menu;
}
static gboolean relation_node_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)data;if(!(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK)) && (event->keyval==GDK_KEY_Delete || event->keyval==GDK_KEY_KP_Delete || event->keyval==GDK_KEY_F5)) { relation_node_action(event->keyval==GDK_KEY_F5,NULL);return TRUE; }
 if(event->keyval==GDK_KEY_Menu || (event->keyval==GDK_KEY_F10 && (event->state&GDK_SHIFT_MASK))) { gtk_menu_popup_at_widget(GTK_MENU(relation_node_menu(GTK_TREE_VIEW(widget))),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,(GdkEvent *)event);return TRUE; }return FALSE;
}
static gboolean relation_node_popup(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)data;if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;GtkTreePath *path=NULL;
 if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget),(gint)event->x,(gint)event->y,&path,NULL,NULL,NULL)) { gtk_tree_view_set_cursor(GTK_TREE_VIEW(widget),path,NULL,FALSE);gtk_tree_path_free(path); }else gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(widget)));
 gtk_widget_grab_focus(widget);gtk_menu_popup_at_pointer(GTK_MENU(relation_node_menu(GTK_TREE_VIEW(widget))),(GdkEvent *)event);return TRUE;
}
static void relation_graph_query(gint mode,gpointer data) {
 (void)data;relation_selected_query(NULL,GINT_TO_POINTER(mode));
}
static void relation_tree_selected(GtkTreeSelection *selection,gpointer data) {
 GtkTreeModel *model;GtkTreeIter iter;gulong line=0;if(gtk_tree_selection_get_selected(selection,&model,&iter)) gtk_tree_model_get(model,&iter,USAGE_LINE,&line,-1);if(line) usage_selected(selection,data);
}
static void relation_tree_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) {
 GtkTreeModel *model=gtk_tree_view_get_model(view);GtkTreeIter iter;gulong line=0;if(!gtk_tree_model_get_iter(model,&iter,path)) return;gtk_tree_model_get(model,&iter,USAGE_LINE,&line,-1);
 if(line) usage_activated(view,path,column,data);else if(gtk_tree_view_row_expanded(view,path)) gtk_tree_view_collapse_row(view,path);else gtk_tree_view_expand_row(view,path,FALSE);
}
static void relation_view_update(void) {
 if(!wb.relation_tree_toggle || !wb.relation_stack) return;
 if(!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"routes")) return;
 gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(wb.relation_mode));
 if(wb.usage_view) {GtkTreeView *view=GTK_TREE_VIEW(wb.usage_view);GtkTreeViewColumn *extra=gtk_tree_view_get_column(view,4);if(extra) gtk_tree_view_column_set_visible(extra,mode==12);gtk_tree_view_column_set_title(gtk_tree_view_get_column(view,0),insight_tr(mode==12 ? "Severity":"Symbol"));gtk_tree_view_column_set_title(gtk_tree_view_get_column(view,3),insight_tr(mode==12 ? "Message / code":"Source / kind • scope"));insight_i18n_key(G_OBJECT(gtk_tree_view_get_column(view,0)),"title",mode==12 ? "Severity":"Symbol");insight_i18n_key(G_OBJECT(gtk_tree_view_get_column(view,3)),"title",mode==12 ? "Message / code":"Source / kind • scope");}
 gboolean semantic=mode==9 || mode==10;gboolean supported=mode==2 || mode==4 || mode==5 || semantic;
 if(semantic && !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle))) {gboolean setting=wb.relation_setting;wb.relation_setting=TRUE;gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle),TRUE);wb.relation_setting=setting;}
 gtk_widget_set_sensitive(wb.relation_tree_toggle,supported && !semantic);gboolean tree=semantic || (supported && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_tree_toggle)));
 const gchar *old=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));gboolean graph=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
 if(old && wb.relation_tree_graph && (!strcmp(old,"treegraph") || !strcmp(old,"graph"))) {
  gboolean vertical=FALSE;gint zoom=100;gboolean old_tree=!strcmp(old,"treegraph");insight_relation_graph_get_options(old_tree ? wb.relation_tree_graph : wb.relation_graph,&vertical,&zoom);insight_relation_graph_set_options(old_tree ? wb.relation_graph : wb.relation_tree_graph,vertical,zoom);
 }
 gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),tree ? (graph ? "treegraph" : "tree") : (graph ? "graph" : "list"));
}
/* Presentation changes deliberately do not dispatch usage_start: keep the
 * current query, loaded branches, filter and selection intact. */
static void relation_next_view(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!wb.relation_stack || !wb.relation_graph_toggle) return;focus_widget_panel(wb.usage_entry);
 if(!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"routes")) {gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");relation_view_update();}
 gboolean graph=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle));
 gboolean tree=!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"treegraph");
 gboolean vertical=FALSE;gint zoom=100;
 insight_relation_graph_get_options(tree ? wb.relation_tree_graph : wb.relation_graph,&vertical,&zoom);
 if(!graph) {
  /* Read the displayed outline's graph settings before switching. */
  gboolean outline_tree=!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack)),"tree");
  insight_relation_graph_get_options(outline_tree ? wb.relation_tree_graph : wb.relation_graph,NULL,&zoom);
  insight_relation_graph_set_options(wb.relation_graph,FALSE,zoom);
  insight_relation_graph_set_options(wb.relation_tree_graph,FALSE,zoom);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),TRUE);
 } else if(!vertical) {
  insight_relation_graph_set_options(wb.relation_graph,TRUE,zoom);
  insight_relation_graph_set_options(wb.relation_tree_graph,TRUE,zoom);
 } else gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),FALSE);
}
static void source_routes_show(GtkWidget *widget,gpointer data) {
 (void)widget;focus_widget_panel(wb.relation_routes);gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"routes");relation_stack_changed(G_OBJECT(wb.relation_stack),NULL,NULL);
 if(GPOINTER_TO_INT(data)) insight_source_routes_capture(wb.relation_routes);
}
static void call_paths_command(GtkWidget *widget,gpointer data) {
 (void)widget;focus_widget_panel(wb.relation_routes);gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"routes");relation_stack_changed(G_OBJECT(wb.relation_stack),NULL,NULL);insight_source_routes_calls(wb.relation_routes,(InsightCallCommand)GPOINTER_TO_INT(data));
}
static GtkWidget *call_paths_menu(const gchar *prefix) {
 GtkWidget *menu=gtk_menu_new();const gchar *labels[]={"Open call path graph…","Call path start from caret","Call path target from caret","Find call paths"};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) {GtkWidget *item=gtk_menu_item_new_with_label(insight_tr(labels[i]));gchar *name=g_strdup_printf("%s-%u",prefix,i);gtk_widget_set_name(item,name);g_free(name);insight_i18n_key(G_OBJECT(item),"label",labels[i]);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(call_paths_command),GUINT_TO_POINTER(i));}
 GtkWidget *help=gtk_menu_item_new_with_label(insight_tr("Call path tutorial…"));insight_i18n_key(G_OBJECT(help),"label","Call path tutorial…");gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());gtk_menu_shell_append(GTK_MENU_SHELL(menu),help);g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"source-routes.md");gtk_widget_show_all(menu);return menu;
}
static void append_call_paths_menu(GtkWidget *parent,const gchar *prefix) {
 GtkWidget *item=gtk_menu_item_new_with_label(insight_tr("Call paths"));insight_i18n_key(G_OBJECT(item),"label","Call paths");gtk_menu_item_set_submenu(GTK_MENU_ITEM(item),call_paths_menu(prefix));gtk_menu_shell_append(GTK_MENU_SHELL(parent),item);gtk_widget_show(item);
}
static void relation_routes_back(GtkWidget *widget,gpointer data) { (void)widget;(void)data;gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");relation_view_update(); }
static void relation_select_view(GtkWidget *widget,gpointer data) {
 (void)widget;gint mode=GPOINTER_TO_INT(data);if(!wb.relation_stack) return;focus_widget_panel(wb.usage_entry);
 const gchar *shown=gtk_stack_get_visible_child_name(GTK_STACK(wb.relation_stack));
 gboolean tree=shown && g_str_has_prefix(shown,"tree");gint zoom=100;
 if(!g_strcmp0(shown,"routes")) gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");
 insight_relation_graph_get_options(tree ? wb.relation_tree_graph:wb.relation_graph,NULL,&zoom);
 insight_relation_graph_set_options(wb.relation_graph,mode==2,zoom);
 insight_relation_graph_set_options(wb.relation_tree_graph,mode==2,zoom);
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.relation_graph_toggle),mode!=0);relation_view_update();
}
static void window_lock_item(GtkWidget *menu,GtkWidget *follow,const gchar *label,const gchar *name) {
 GtkWidget *item=gtk_check_menu_item_new_with_label(label);gtk_widget_set_name(item,name);
 /* One authoritative state; selection previews and saved preferences update
  * the menu too. Bindings disconnect automatically when either side dies. */
 g_object_bind_property(follow,"active",item,"active",G_BINDING_BIDIRECTIONAL|G_BINDING_SYNC_CREATE|G_BINDING_INVERT_BOOLEAN);
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);
}
static void relation_graph_toggle(GtkToggleButton *button,gpointer data) { (void)button;(void)data;gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");relation_view_update(); }
static void relation_tree_toggle(GtkToggleButton *button,gpointer data) { (void)data;gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");relation_view_update();if(!wb.relation_setting && gtk_toggle_button_get_active(button) && *gtk_entry_get_text(GTK_ENTRY(wb.usage_entry))) usage_start(NULL,NULL); }
static void reference_region_changed(GtkWidget *widget,gpointer data) {
 (void)data;if(wb.reference_setting) return;wb.reference_setting=TRUE;
 if(widget==wb.usage_code && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.usage_code))) gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),0);
 else if(widget==wb.usage_comments && gtk_combo_box_get_active(GTK_COMBO_BOX(wb.usage_comments))>0) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.usage_code),FALSE);
 wb.reference_setting=FALSE;relation_controls(NULL,NULL);
}
static void relation_stack_changed(GObject *object,GParamSpec *spec,gpointer data) { (void)spec;(void)data;gboolean routes=!g_strcmp0(gtk_stack_get_visible_child_name(GTK_STACK(object)),"routes");if(wb.relation_options) {gtk_widget_set_no_show_all(wb.relation_options,routes);gtk_widget_set_visible(wb.relation_options,!routes);}gtk_widget_set_no_show_all(wb.usage_status,routes);gtk_widget_set_visible(wb.usage_status,!routes);}
static void create_usage_browser(void) {
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2);
 wb.usage_entry=gtk_search_entry_new(); gtk_widget_set_name(wb.usage_entry,"insight-relation-query"); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.usage_entry),"Symbol or qualified owner…");
 wb.usage_status=gtk_label_new("Comments and strings included; no semantic scope resolution"); gtk_widget_set_name(wb.usage_status,"insight-relation-status"); gtk_label_set_line_wrap(GTK_LABEL(wb.usage_status),TRUE);
 wb.usage_buffer=gtk_check_button_new_with_label("Current buffer only");
 wb.usage_code=gtk_check_button_new_with_label("References: code only");gtk_widget_set_name(wb.usage_code,"insight-references-code-only");
 wb.usage_comments=gtk_combo_box_text_new();gtk_widget_set_name(wb.usage_comments,"insight-references-comments");
 gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments),"References: all text");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments),"References: skip comments");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.usage_comments),"References: comments only");gtk_combo_box_set_active(GTK_COMBO_BOX(wb.usage_comments),0);
 gtk_widget_set_tooltip_text(wb.usage_comments,"Lexical comment regions; skip comments retains strings, code only excludes both. Unknown languages are skipped in filtered modes.");
 wb.relation_follow=gtk_check_button_new_with_label("Follow symbol in editor");gtk_widget_set_name(wb.relation_follow,"insight-relations-follow");
 wb.relation_enclosing=gtk_check_button_new_with_label("Track enclosing function");
 wb.relation_transitive=gtk_check_button_new_with_label("Include indirect inheritance (tags)");gtk_widget_set_name(wb.relation_transitive,"insight-relation-transitive");gtk_widget_set_sensitive(wb.relation_transitive,FALSE);
 wb.relation_mode=gtk_combo_box_text_new(); gtk_widget_set_name(wb.relation_mode,"insight-relation-mode");
 const char *modes[]={"References (lexical)","Definitions / declarations","Direct members (tags)","Text search (project)","Base types (tags)","Derived types (tags)","Calls (current function, lexical)","Call sites (project, lexical)","References (Clang C/C++)","Callees (Clang tree)","Callers (Clang tree)","References (slang HDL, by name)","Diagnostics (slang HDL)"};
 for(guint i=0;i<G_N_ELEMENTS(modes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(wb.relation_mode),modes[i]);
 gtk_combo_box_set_active(GTK_COMBO_BOX(wb.relation_mode),0);
 GtkWidget *search=gtk_button_new_with_label("Search"),*cancel=gtk_button_new_with_label("Stop");
 gtk_box_pack_start(GTK_BOX(controls),search,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(controls),cancel,FALSE,FALSE,0);
 const gchar *report_icons[]={"view-list-symbolic","bookmark-new-symbolic"},*report_tips[]={"View Relation Outline…","Create Bookmarks from Relation Items…"};for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_from_icon_name(report_icons[i],GTK_ICON_SIZE_MENU);gtk_widget_set_name(button,i ? "insight-relation-bookmarks":"insight-relation-outline");gtk_widget_set_tooltip_text(button,report_tips[i]);gtk_box_pack_start(GTK_BOX(controls),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",G_CALLBACK(relation_report_show),GUINT_TO_POINTER(i)); }

 wb.matches=gtk_list_store_new(USAGE_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_ULONG,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG);
 wb.relation_filter_entry=gtk_search_entry_new();gtk_widget_set_tooltip_text(wb.relation_filter_entry,"Prefix with ~ for fuzzy subsequence matching");gtk_widget_set_name(wb.relation_filter_entry,"insight-relation-filter");gtk_entry_set_placeholder_text(GTK_ENTRY(wb.relation_filter_entry),"Filter results: symbol, file or source…");
 GtkTreeModel *filtered=gtk_tree_model_filter_new(GTK_TREE_MODEL(wb.matches),NULL);wb.relation_filter=GTK_TREE_MODEL_FILTER(filtered);
 gtk_tree_model_filter_set_visible_func(wb.relation_filter,relation_row_visible,NULL,NULL);
 GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(filtered);
 wb.usage_view=gtk_tree_view_new_with_model(sorted);g_object_unref(sorted);g_object_unref(filtered); gtk_widget_set_name(wb.usage_view,"insight-relations");
 GtkCellRenderer *relation_symbol_renderer=gtk_cell_renderer_text_new();GtkTreeViewColumn *relation_symbol_column=gtk_tree_view_column_new_with_attributes("Symbol",relation_symbol_renderer,NULL);gtk_tree_view_column_set_cell_data_func(relation_symbol_column,relation_symbol_renderer,hdl_text_cell,GINT_TO_POINTER(USAGE_SYMBOL),NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),relation_symbol_column);
 GtkCellRenderer *relation_file_renderer=gtk_cell_renderer_text_new();GtkTreeViewColumn *relation_file_column=gtk_tree_view_column_new_with_attributes("File",relation_file_renderer,NULL);gtk_tree_view_column_set_cell_data_func(relation_file_column,relation_file_renderer,hdl_text_cell,GINT_TO_POINTER(USAGE_DISPLAY),NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),relation_file_column);
 GtkCellRenderer *relation_line_renderer=gtk_cell_renderer_text_new();GtkTreeViewColumn *relation_line_column=gtk_tree_view_column_new_with_attributes("Line",relation_line_renderer,NULL);gtk_tree_view_column_set_cell_data_func(relation_line_column,relation_line_renderer,hdl_position_cell,GINT_TO_POINTER(USAGE_LINE),NULL);gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),relation_line_column);
 gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),gtk_tree_view_column_new_with_attributes("Source / kind • scope",gtk_cell_renderer_text_new(),"text",USAGE_SNIPPET,NULL));
 GtkCellRenderer *hdl_column_renderer=gtk_cell_renderer_text_new();GtkTreeViewColumn *hdl_column=gtk_tree_view_column_new_with_attributes(insight_tr("Column"),hdl_column_renderer,NULL);gtk_tree_view_column_set_cell_data_func(hdl_column,hdl_column_renderer,hdl_position_cell,GINT_TO_POINTER(USAGE_COLUMN),NULL);insight_i18n_key(G_OBJECT(hdl_column),"title","Column");gtk_tree_view_column_set_visible(hdl_column,FALSE);gtk_tree_view_append_column(GTK_TREE_VIEW(wb.usage_view),hdl_column);
 const gint columns[]={USAGE_SYMBOL,USAGE_DISPLAY,USAGE_LINE,USAGE_SNIPPET};
 const gint widths[]={100,150,55,260};
 for(guint i=0;i<G_N_ELEMENTS(columns);i++) {
  GtkTreeViewColumn *column=gtk_tree_view_get_column(GTK_TREE_VIEW(wb.usage_view),(gint)i);
  gtk_tree_view_column_set_sort_column_id(column,columns[i]); gtk_tree_view_column_set_sizing(column,GTK_TREE_VIEW_COLUMN_FIXED); gtk_tree_view_column_set_fixed_width(column,widths[i]); gtk_tree_view_column_set_resizable(column,TRUE);
  GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column)); for(GList *cell=cells;cell;cell=cell->next) g_object_set(cell->data,"ellipsize",PANGO_ELLIPSIZE_END,NULL); g_list_free(cells);
 }
 gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(wb.usage_view),USAGE_SNIPPET);
 GtkWidget *drill=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,2),*definitions=gtk_button_new_with_label("Definitions"),*sites=gtk_button_new_with_label("Call sites");
 gtk_widget_set_name(definitions,"insight-relation-definitions");
 gtk_widget_set_tooltip_text(definitions,"Show definitions for the selected result; choose among overloads");gtk_widget_set_tooltip_text(sites,"Search call sites for the selected result");
 wb.relation_history=g_ptr_array_new_with_free_func(relation_visit_free);wb.relation_future=g_ptr_array_new_with_free_func(relation_visit_free);wb.relation_back=gtk_button_new_from_icon_name("go-previous",GTK_ICON_SIZE_BUTTON);gtk_widget_set_name(wb.relation_back,"insight-relation-back");gtk_widget_set_tooltip_text(wb.relation_back,"Back to the previous relation query");gtk_widget_set_sensitive(wb.relation_back,FALSE);g_signal_connect(wb.relation_back,"clicked",G_CALLBACK(relation_back),NULL);gtk_box_pack_start(GTK_BOX(drill),wb.relation_back,FALSE,FALSE,0);
 wb.relation_forward=gtk_button_new_from_icon_name("go-next",GTK_ICON_SIZE_BUTTON);gtk_widget_set_name(wb.relation_forward,"insight-relation-forward");gtk_widget_set_tooltip_text(wb.relation_forward,"Forward to the next relation query");gtk_widget_set_sensitive(wb.relation_forward,FALSE);g_signal_connect(wb.relation_forward,"clicked",G_CALLBACK(relation_forward),NULL);gtk_box_pack_start(GTK_BOX(drill),wb.relation_forward,FALSE,FALSE,0);
 GtkWidget *history=gtk_menu_button_new(),*history_menu=gtk_menu_new();gtk_widget_set_name(history,"insight-relation-history");gtk_button_set_image(GTK_BUTTON(history),gtk_image_new_from_icon_name("document-open-recent",GTK_ICON_SIZE_BUTTON));gtk_widget_set_tooltip_text(history,"Relation query history");gtk_menu_button_set_popup(GTK_MENU_BUTTON(history),history_menu);g_signal_connect(history_menu,"show",G_CALLBACK(relation_history_show),NULL);gtk_box_pack_start(GTK_BOX(drill),history,FALSE,FALSE,0);
 const gchar *node_icons[]={"list-remove","view-refresh"},*node_tips[]={"Remove selected node from view (Delete)","Restore nodes by refreshing the query (F5)"},*node_names[]={"insight-relation-node-remove","insight-relation-node-restore"};
 for(guint i=0;i<2;i++) { GtkWidget *button=gtk_button_new_from_icon_name(node_icons[i],GTK_ICON_SIZE_BUTTON);gtk_widget_set_name(button,node_names[i]);gtk_widget_set_tooltip_text(button,node_tips[i]);gtk_box_pack_start(GTK_BOX(drill),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",G_CALLBACK(relation_node_button),GUINT_TO_POINTER(i)); }
 gtk_box_pack_start(GTK_BOX(drill),definitions,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(drill),sites,TRUE,TRUE,0);
 g_signal_connect(definitions,"clicked",G_CALLBACK(relation_selected_query),GINT_TO_POINTER(1));g_signal_connect(sites,"clicked",G_CALLBACK(relation_selected_query),GINT_TO_POINTER(7));
 for(guint i=0;i<2;i++) {GtkWidget *button=gtk_button_new_from_icon_name(i ? "go-up-symbolic":"go-down-symbolic",GTK_ICON_SIZE_BUTTON);gtk_widget_set_name(button,i ? "insight-relation-callers":"insight-relation-callees");gtk_widget_set_tooltip_text(button,i ? "Browse callers of selected function (Clang)":"Browse callees of selected function (Clang)");gtk_widget_set_sensitive(button,insight_clang_available());gtk_box_pack_start(GTK_BOX(drill),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",G_CALLBACK(relation_selected_query),GINT_TO_POINTER(i ? 10:9));}

 g_signal_connect(wb.relation_filter_entry,"changed",G_CALLBACK(relation_filter_changed),NULL);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),wb.usage_view);
 wb.relation_stack=gtk_stack_new();gtk_stack_add_named(GTK_STACK(wb.relation_stack),scroll,"list");
 wb.relation_routes=insight_source_routes_new();gtk_stack_add_named(GTK_STACK(wb.relation_stack),wb.relation_routes,"routes");g_signal_connect(insight_source_routes_back_button(wb.relation_routes),"clicked",G_CALLBACK(relation_routes_back),NULL);g_signal_connect(wb.relation_stack,"notify::visible-child-name",G_CALLBACK(relation_stack_changed),NULL);
 wb.relation_tree=insight_relation_tree_new();gtk_stack_add_named(GTK_STACK(wb.relation_stack),wb.relation_tree,"tree");
 GtkTreeView *tree_view=insight_relation_tree_view(wb.relation_tree);g_signal_connect(gtk_tree_view_get_selection(tree_view),"changed",G_CALLBACK(relation_tree_selected),NULL);g_signal_connect(tree_view,"row-activated",G_CALLBACK(relation_tree_activated),NULL);
 wb.relation_tree_toggle=gtk_check_button_new_with_label("Expandable relation tree");gtk_widget_set_name(wb.relation_tree_toggle,"insight-relation-tree-toggle");gtk_widget_set_sensitive(wb.relation_tree_toggle,FALSE);gtk_box_pack_start(GTK_BOX(controls),wb.relation_tree_toggle,FALSE,FALSE,0);g_signal_connect(wb.relation_tree_toggle,"toggled",G_CALLBACK(relation_tree_toggle),NULL);
 wb.relation_graph=insight_relation_graph_new(GTK_TREE_VIEW(wb.usage_view),GTK_ENTRY(wb.usage_entry),USAGE_SYMBOL,USAGE_DISPLAY,USAGE_LINE);insight_relation_graph_set_query_handler(wb.relation_graph,relation_graph_query,NULL);insight_relation_graph_set_node_handler(wb.relation_graph,relation_node_action,NULL);gtk_stack_add_named(GTK_STACK(wb.relation_stack),wb.relation_graph,"graph");gtk_stack_set_visible_child_name(GTK_STACK(wb.relation_stack),"list");
 wb.relation_tree_graph=insight_relation_graph_new_tree(tree_view,GTK_ENTRY(wb.usage_entry),USAGE_SYMBOL,USAGE_DISPLAY,USAGE_LINE);insight_relation_graph_set_query_handler(wb.relation_tree_graph,relation_graph_query,NULL);insight_relation_graph_set_node_handler(wb.relation_tree_graph,relation_node_action,NULL);gtk_stack_add_named(GTK_STACK(wb.relation_stack),wb.relation_tree_graph,"treegraph");
 wb.relation_graph_toggle=gtk_check_button_new_with_label("Graph view");gtk_widget_set_name(wb.relation_graph_toggle,"insight-relation-graph-toggle");gtk_box_pack_start(GTK_BOX(controls),wb.relation_graph_toggle,FALSE,FALSE,0);g_signal_connect(wb.relation_graph_toggle,"toggled",G_CALLBACK(relation_graph_toggle),NULL);
 GtkWidget *routes=gtk_button_new_with_label("Source routes / flow"),*relations=gtk_button_new_with_label("Relations");gtk_widget_set_name(routes,"insight-relation-source-routes");gtk_widget_set_tooltip_text(routes,"Graphs of source locations and function control flow");gtk_box_pack_start(GTK_BOX(controls),routes,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(controls),relations,FALSE,FALSE,0);g_signal_connect(routes,"clicked",G_CALLBACK(source_routes_show),NULL);g_signal_connect(relations,"clicked",G_CALLBACK(relation_routes_back),NULL);
 GtkWidget *next_view=gtk_button_new_from_icon_name("view-refresh-symbolic",GTK_ICON_SIZE_MENU);gtk_widget_set_name(next_view,"insight-relation-next-view");gtk_widget_set_tooltip_text(next_view,"Next Relation Window View");gtk_box_pack_start(GTK_BOX(controls),next_view,FALSE,FALSE,0);g_signal_connect(next_view,"clicked",G_CALLBACK(relation_next_view),NULL);
 gtk_box_pack_start(GTK_BOX(box),wb.usage_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_mode,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_transitive,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_follow,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_enclosing,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_code,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),wb.usage_comments,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_buffer,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.usage_status,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_filter_entry,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),drill,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(box),wb.relation_stack,TRUE,TRUE,0);
 GtkWidget *options=insight_compare_ui_tabs(box,"insight-relation-toolbar-tabs");wb.relation_options=options;
 GtkWidget *query=insight_compare_ui_page(options,"Query"),*filter=insight_compare_ui_page(options,"Filters"),*view=insight_compare_ui_page(options,"View"),*navigation=insight_compare_ui_page(options,"Navigation");
 GtkWidget *query_widgets[]={wb.usage_entry,wb.relation_mode,wb.relation_follow,wb.relation_enclosing,wb.relation_transitive};for(guint n=0;n<G_N_ELEMENTS(query_widgets);n++) insight_compare_ui_move(query_widgets[n],query);
 GtkWidget *filter_widgets[]={wb.usage_code,wb.usage_comments,wb.usage_buffer,wb.relation_filter_entry};for(guint n=0;n<G_N_ELEMENTS(filter_widgets);n++) insight_compare_ui_move(filter_widgets[n],filter);
 insight_compare_ui_move(controls,view);insight_compare_ui_move(drill,navigation);insight_ai_ui_fold(options,"Collapse");

 gtk_notebook_append_page(GTK_NOTEBOOK(wb.project_notebook),box,gtk_label_new("Relations"));
 g_signal_connect(search,"clicked",G_CALLBACK(usage_start),NULL); g_signal_connect(cancel,"clicked",G_CALLBACK(usage_cancel),NULL); g_signal_connect(wb.usage_entry,"activate",G_CALLBACK(usage_start),NULL);
 g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(wb.usage_view)),"changed",G_CALLBACK(usage_selected),NULL); g_signal_connect(wb.usage_view,"row-activated",G_CALLBACK(usage_activated),NULL);
 g_signal_connect(wb.usage_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 g_signal_connect(wb.usage_view,"key-press-event",G_CALLBACK(relation_node_key),NULL);g_signal_connect(wb.usage_view,"key-press-event",G_CALLBACK(browser_key),NULL);g_signal_connect(wb.usage_view,"button-press-event",G_CALLBACK(relation_node_popup),NULL);g_signal_connect(tree_view,"key-press-event",G_CALLBACK(relation_node_key),NULL);g_signal_connect(tree_view,"button-press-event",G_CALLBACK(relation_node_popup),NULL);
 g_signal_connect(wb.relation_transitive,"toggled",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.relation_mode,"changed",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.relation_follow,"toggled",G_CALLBACK(relation_follow_changed),NULL);
 g_signal_connect(wb.relation_enclosing,"toggled",G_CALLBACK(relation_controls),NULL);
 g_signal_connect(wb.usage_code,"toggled",G_CALLBACK(reference_region_changed),NULL);g_signal_connect(wb.usage_comments,"changed",G_CALLBACK(reference_region_changed),NULL);
 g_signal_connect(wb.usage_buffer,"toggled",G_CALLBACK(relation_controls),NULL);
 gtk_widget_show_all(box);
}
static void browser_symbol_info(GtkWidget *widget,gpointer data) {
 (void)widget;GtkTreeView *view=GTK_TREE_VIEW(data);GtkTreeModel *model;GtkTreeIter iter;
 if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(view),&model,&iter)) { ui_set_statusbar(TRUE,"Select a symbol first");return; }
 gchar *name=NULL,*path=NULL;gulong line=0;gtk_tree_model_get(model,&iter,SYMBOL_NAME,&name,SYMBOL_PATH,&path,SYMBOL_LINE,&line,-1);
 if(!line) { g_free(name); g_free(path); ui_set_statusbar(TRUE,"Select a symbol, not a type group"); return; }
 if(GTK_WIDGET(view)==wb.local_view && (!path || !strcmp(path,"@untitled"))) { g_free(path);path=g_strdup_printf("@buffer:%u",wb.local_doc_id); }
 if(name) info_show_query(name,path,line);
 g_free(name);g_free(path);
}
typedef struct { const TMTag *tag; GtkTreePath *path; } SymbolLocate;
