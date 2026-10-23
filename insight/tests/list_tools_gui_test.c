/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real production list tools, isolated GTK model; unrelated i18n is stubbed. */
#include "../../src/insight-list-tools.h"
#include <string.h>
const gchar *insight_tr(const gchar *text) { return text; }
void insight_i18n_apply(GtkWidget *widget) { (void)widget; }
static guint activations;
static void activated(GtkTreeView *v,GtkTreePath *p,GtkTreeViewColumn *c,gpointer d) { (void)v;(void)p;(void)c;(void)d;activations++; }
static GtkWidget *named(GtkWidget *w,const gchar *name) {
    if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
    if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *dialog(void) {
    GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(!g_strcmp0(gtk_widget_get_name(p->data),"insight-list-search")) found=p->data;g_list_free(list);g_assert_nonnull(found);return found;
}
static void settle(GtkWidget *view) {
    gint64 until=g_get_monotonic_time()+5*G_USEC_PER_SEC;
    while(g_object_get_data(G_OBJECT(view),"insight-list-tools-busy") && g_get_monotonic_time()<until) g_main_context_iteration(NULL,FALSE);
    g_assert_null(g_object_get_data(G_OBJECT(view),"insight-list-tools-busy"));
}
static void command(GtkWidget *menu,const gchar *name) { GtkWidget *item=named(menu,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static gchar *selected(GtkWidget *view) {
    GtkTreeModel *m;GtkTreeIter i;gchar *text=NULL;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&m,&i)) gtk_tree_model_get(m,&i,0,&text,-1);return text;
}
static void selected_is(GtkWidget *view,const gchar *expected) { gchar *text=selected(view);g_assert_cmpstr(text,==,expected);g_free(text); }
static void option(GtkWidget *d,const gchar *name,gboolean active) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(named(d,name)),active); }
static void search(GtkWidget *view,GtkWidget *d,const gchar *pattern,gboolean regex,gboolean match_case,gboolean beginning,gboolean wrap) {
    gtk_entry_set_text(GTK_ENTRY(named(d,"insight-list-search-pattern")),pattern);
    option(d,"insight-list-search-regex",regex);option(d,"insight-list-search-case",match_case);option(d,"insight-list-search-beginning",beginning);option(d,"insight-list-search-wrap",wrap);
    gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);settle(view);
}
static void key(GtkWidget *view,guint modifiers) { GdkEventKey event={0};event.type=GDK_KEY_PRESS;event.keyval=GDK_KEY_F4;event.state=modifiers;gboolean handled=FALSE;g_signal_emit_by_name(view,"key-press-event",&event,&handled);g_assert_true(handled);settle(view); }
static gboolean visible(GtkTreeModel *m,GtkTreeIter *i,gpointer d) { (void)d;gchar *text=NULL;gtk_tree_model_get(m,i,0,&text,-1);gboolean show=g_strcmp0(text,"hidden")!=0;g_free(text);return show; }
static void clipboard_is(const gchar *expected) { gchar *text=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));g_assert_cmpstr(text,==,expected);g_free(text); }
static guint print_cancelled;
static gboolean cancel_print(gpointer data) {
    guint *attempts=data;g_assert_cmpuint(++*attempts,<,100);GList *list=gtk_window_list_toplevels();
    for(GList *p=list;p;p=p->next) if(GTK_IS_DIALOG(p->data) && gtk_widget_get_visible(p->data) && g_str_has_prefix(G_OBJECT_TYPE_NAME(p->data),"GtkPrint")) { print_cancelled++;gtk_dialog_response(GTK_DIALOG(p->data),GTK_RESPONSE_CANCEL);g_list_free(list);return G_SOURCE_REMOVE; }
    g_list_free(list);return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
    if(!gtk_init_check(&argc,&argv)) return 77;
    g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
    GtkListStore *store=gtk_list_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT);GtkTreeIter iter;
    const gchar *names[]={"zeta","hidden","alpha","АЛЬФА"};
    for(guint i=0;i<G_N_ELEMENTS(names);i++) { gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,names[i],1,i==0 ? "a\tb\nc":"sample.c",2,i+1,-1); }
    GtkTreeModel *filter=gtk_tree_model_filter_new(GTK_TREE_MODEL(store),NULL);gtk_tree_model_filter_set_visible_func(GTK_TREE_MODEL_FILTER(filter),visible,NULL,NULL);
    GtkTreeModel *sort=gtk_tree_model_sort_new_with_model(filter);gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(sort),0,GTK_SORT_ASCENDING);
    GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL),*view=gtk_tree_view_new_with_model(sort),*menu=gtk_menu_new();g_object_ref_sink(menu);gtk_container_add(GTK_CONTAINER(window),view);
    GtkTreeViewColumn *name=gtk_tree_view_column_new_with_attributes("Name",gtk_cell_renderer_text_new(),"text",0,NULL),*file=gtk_tree_view_column_new_with_attributes("File",gtk_cell_renderer_text_new(),"text",1,NULL),*line=gtk_tree_view_column_new_with_attributes("Line",gtk_cell_renderer_text_new(),"text",2,NULL);
    gtk_tree_view_append_column(GTK_TREE_VIEW(view),name);gtk_tree_view_append_column(GTK_TREE_VIEW(view),file);gtk_tree_view_append_column(GTK_TREE_VIEW(view),line);gtk_tree_view_column_set_visible(line,FALSE);
    insight_list_tools_menu(GTK_TREE_VIEW(view),menu);insight_list_tools_menu(GTK_TREE_VIEW(view),menu);g_signal_connect(view,"row-activated",G_CALLBACK(activated),NULL);gtk_widget_show_all(window);
    command(menu,"insight-list-copy");settle(view);clipboard_is("Name\tFile\nalpha\tsample.c\nzeta\ta\\tb\\nc\nАЛЬФА\tsample.c\n");
    gtk_tree_view_move_column_after(GTK_TREE_VIEW(view),file,NULL);command(menu,"insight-list-copy");settle(view);clipboard_is("File\tName\nsample.c\talpha\na\\tb\\nc\tzeta\nsample.c\tАЛЬФА\n");
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"print-sentinel",-1);guint attempts=0;g_timeout_add(50,cancel_print,&attempts);command(menu,"insight-list-print");settle(view);g_assert_cmpuint(print_cancelled,==,1);clipboard_is("print-sentinel");g_assert_cmpstr(g_object_get_data(G_OBJECT(view),"insight-list-tools-status"),==,"Print snapshot ready");
    command(menu,"insight-list-search-command");GtkWidget *d=dialog();search(view,d,"альфа",FALSE,FALSE,TRUE,FALSE);selected_is(view,"АЛЬФА");
    search(view,d,"альфа",FALSE,TRUE,TRUE,FALSE);selected_is(view,"АЛЬФА");g_assert_nonnull(strstr(g_object_get_data(G_OBJECT(view),"insight-list-tools-status"),"No matching"));
    search(view,d,"sample\\.c",TRUE,TRUE,TRUE,FALSE);selected_is(view,"alpha");key(view,0);selected_is(view,"АЛЬФА");key(view,GDK_SHIFT_MASK);selected_is(view,"alpha");key(view,GDK_SHIFT_MASK);selected_is(view,"alpha");
    search(view,d,"sample.c",FALSE,FALSE,TRUE,TRUE);key(view,GDK_SHIFT_MASK);selected_is(view,"АЛЬФА");key(view,0);selected_is(view,"alpha");
    search(view,d,"[",TRUE,FALSE,TRUE,FALSE);g_assert_nonnull(strstr(g_object_get_data(G_OBJECT(view),"insight-list-tools-status"),"Invalid"));selected_is(view,"alpha");
    /* A new job must discard an old partial copy, never publish stale text. */
    gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);command(menu,"insight-list-copy");gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,"late",1,"later.c",-1);settle(view);clipboard_is("sentinel");
    command(menu,"insight-list-copy");gtk_tree_view_column_set_visible(line,TRUE);settle(view);clipboard_is("sentinel");
    GtkListStore *other_store=gtk_list_store_new(1,G_TYPE_STRING);GtkTreeIter other_iter;gtk_list_store_append(other_store,&other_iter);gtk_list_store_set(other_store,&other_iter,0,"newest",-1);
    GtkWidget *other=gtk_tree_view_new_with_model(GTK_TREE_MODEL(other_store)),*other_menu=gtk_menu_new();g_object_ref_sink(other);g_object_ref_sink(other_menu);gtk_tree_view_append_column(GTK_TREE_VIEW(other),gtk_tree_view_column_new_with_attributes("Other",gtk_cell_renderer_text_new(),"text",0,NULL));insight_list_tools_menu(GTK_TREE_VIEW(other),other_menu);
    command(menu,"insight-list-copy");command(other_menu,"insight-list-copy");settle(other);settle(view);clipboard_is("Other\nnewest\n");g_assert_nonnull(strstr(g_object_get_data(G_OBJECT(view),"insight-list-tools-status"),"superseded"));gtk_widget_destroy(other);g_object_unref(other);gtk_widget_destroy(other_menu);g_object_unref(other_menu);g_object_unref(other_store);gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),"sentinel",-1);
    search(view,d,"sample.c",FALSE,FALSE,TRUE,FALSE);gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);gtk_tree_selection_unselect_all(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)));settle(view);g_assert_null(selected(view));
    gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_ACCEPT);gtk_widget_destroy(d);settle(view);g_assert_null(selected(view));
    /* Work limits are atomic even when 20000 preceding rows were processed. */
    gtk_list_store_clear(store);for(guint i=0;i<20001;i++) { gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,"row",1,"sample.c",-1); }
    command(menu,"insight-list-copy");settle(view);clipboard_is("sentinel");g_assert_nonnull(strstr(g_object_get_data(G_OBJECT(view),"insight-list-tools-status"),"20000"));
    gtk_list_store_clear(store);gchar *huge=g_strnfill(65537,'x');gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,huge,1,"sample.c",-1);g_free(huge);command(menu,"insight-list-copy");settle(view);clipboard_is("sentinel");
    gtk_list_store_clear(store);gchar *hard=g_strnfill(512,'a');gchar *difficult=g_strconcat(hard,"!",NULL);g_free(hard);gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,difficult,1,"sample.c",-1);g_free(difficult);
    command(menu,"insight-list-search-command");d=dialog();search(view,d,"(a+)+$",TRUE,TRUE,TRUE,FALSE);g_assert_nonnull(strstr(g_object_get_data(G_OBJECT(view),"insight-list-tools-status"),"work limit"));gtk_widget_destroy(d);
    /* Model replacement and destruction while idle work is pending. */
    command(menu,"insight-list-copy");gtk_tree_view_set_model(GTK_TREE_VIEW(view),GTK_TREE_MODEL(store));settle(view);clipboard_is("sentinel");
    insight_list_tools_popup_attach(GTK_TREE_VIEW(view));gboolean handled=FALSE;g_signal_emit_by_name(view,"popup-menu",&handled);g_assert_true(handled);GtkWidget *popup=g_object_get_data(G_OBJECT(view),"insight-list-popup");g_assert_nonnull(named(popup,"insight-list-copy"));gtk_menu_popdown(GTK_MENU(popup));
    command(menu,"insight-list-copy");g_object_ref(view);gtk_widget_destroy(window);settle(view);g_object_unref(view);gtk_widget_destroy(menu);g_object_unref(menu);
    /* A collapsed ancestor is expanded to show a loaded descendant. */
    GtkTreeStore *tree=gtk_tree_store_new(1,G_TYPE_STRING);GtkTreeIter root,child;gtk_tree_store_append(tree,&root,NULL);gtk_tree_store_set(tree,&root,0,"parent",-1);gtk_tree_store_append(tree,&child,&root);gtk_tree_store_set(tree,&child,0,"nested needle",-1);
    window=gtk_window_new(GTK_WINDOW_TOPLEVEL);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(tree));gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Symbol",gtk_cell_renderer_text_new(),"text",0,NULL));gtk_container_add(GTK_CONTAINER(window),view);menu=gtk_menu_new();g_object_ref_sink(menu);insight_list_tools_menu(GTK_TREE_VIEW(view),menu);gtk_widget_show_all(window);
    command(menu,"insight-list-search-command");d=dialog();search(view,d,"needle",FALSE,FALSE,TRUE,FALSE);selected_is(view,"nested needle");GtkTreePath *path=gtk_tree_path_new_first();g_assert_true(gtk_tree_view_row_expanded(GTK_TREE_VIEW(view),path));gtk_tree_path_free(path);
    command(menu,"insight-list-copy");settle(view);clipboard_is("Symbol\nparent\n  nested needle\n");gtk_widget_destroy(window);gtk_widget_destroy(menu);g_object_unref(menu);
    g_object_unref(tree);g_object_unref(sort);g_object_unref(filter);g_object_unref(store);g_assert_cmpuint(activations,==,0);
    g_print("List tools: sorted/filtered/hidden/reordered columns, escaped copy, Unicode/regex, both directions/wrap, stale guards, limits, cancellation, hierarchy and destruction passed\n");return 0;
}
