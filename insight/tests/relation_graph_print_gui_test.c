/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-relation-graph.h"
#include "../core/relation_graph_print.h"
#include "../core/relation_graph_layout.h"
#include <glib/gstdio.h>
#include <string.h>
static guint pages;
static void page_drawn(GtkPrintOperation *op,GtkPrintContext *context,gint page,gpointer data) {
 (void)op;(void)context;(void)data;g_assert_cmpint(page,==,(gint)pages);pages++;
}
static void export_pdf(GtkPrintOperation *op,guint expected,const gchar *present,const gchar *absent) {
 GError *error=NULL;gchar *folder=g_dir_make_tmp("insight-graph-print-XXXXXX",&error);g_assert_no_error(error);g_assert_nonnull(folder);gchar *path=g_build_filename(folder,"graph.pdf",NULL);
 gtk_print_operation_set_export_filename(op,path);g_signal_connect(op,"draw-page",G_CALLBACK(page_drawn),NULL);pages=0;
 GtkPrintOperationResult result=gtk_print_operation_run(op,GTK_PRINT_OPERATION_ACTION_EXPORT,NULL,&error);g_assert_no_error(error);g_assert_cmpint(result,==,GTK_PRINT_OPERATION_RESULT_APPLY);g_assert_cmpuint(pages,==,expected);
 gchar *text=NULL,*stderr_text=NULL;gint status=0;gchar *args[]={"pdftotext","-layout",path,"-",NULL};
 g_assert_true(g_spawn_sync(NULL,args,NULL,G_SPAWN_SEARCH_PATH,NULL,NULL,&text,&stderr_text,&status,&error));g_assert_no_error(error);g_assert_cmpint(status,==,0);g_assert_nonnull(strstr(text,"Snapshot query"));g_assert_nonnull(strstr(text,present));if(absent) g_assert_null(strstr(text,absent));g_assert_null(strstr(text,"MUTATED"));
 g_assert_cmpuint(strlen(text),>,50);g_free(text);g_free(stderr_text);g_remove(path);g_rmdir(folder);g_free(path);g_free(folder);
}
static void paper(GtkPrintOperation *op,gboolean landscape) {
 GtkPageSetup *setup=gtk_page_setup_new();GtkPaperSize *size=gtk_paper_size_new(GTK_PAPER_NAME_A4);gtk_page_setup_set_paper_size(setup,size);gtk_paper_size_free(size);gtk_page_setup_set_orientation(setup,landscape ? GTK_PAGE_ORIENTATION_LANDSCAPE : GTK_PAGE_ORIENTATION_PORTRAIT);
 gtk_page_setup_set_top_margin(setup,10,GTK_UNIT_MM);gtk_page_setup_set_bottom_margin(setup,10,GTK_UNIT_MM);gtk_page_setup_set_left_margin(setup,10,GTK_UNIT_MM);gtk_page_setup_set_right_margin(setup,10,GTK_UNIT_MM);gtk_print_operation_set_default_page_setup(op,setup);g_object_unref(setup);
}
static void flat(gboolean tiled,gboolean vertical,gint zoom) {
 GtkListStore *model=gtk_list_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG);GtkTreeIter iter;
 for(guint i=0;i<40;i++) { gchar *name=g_strdup_printf("caller_%02u",i);gtk_list_store_append(model,&iter);gtk_list_store_set(model,&iter,0,name,1,"example.c",2,(gulong)i+1,-1);g_free(name); }
 GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model)),*query=gtk_entry_new();g_object_ref_sink(tree);g_object_ref_sink(query);gtk_entry_set_text(GTK_ENTRY(query),"Snapshot query");
 GtkWidget *graph=insight_relation_graph_new(GTK_TREE_VIEW(tree),GTK_ENTRY(query),0,1,2);g_object_ref_sink(graph);insight_relation_graph_set_options(graph,vertical,zoom);
 GtkPrintOperation *op=insight_relation_graph_print_operation(graph);g_assert_nonnull(op);paper(op,vertical);
 /* Exercise the actual custom tab; GTK applies its selection before begin-print. */
 GtkWidget *options=NULL;g_signal_emit_by_name(op,"create-custom-widget",&options);g_assert_nonnull(options);g_object_ref_sink(options);
 GtkWidget *combo=g_object_get_data(G_OBJECT(options),"layout");g_assert_nonnull(combo);gtk_combo_box_set_active(GTK_COMBO_BOX(combo),tiled ? 1 : 0);g_signal_emit_by_name(op,"custom-widget-apply",options);g_assert_cmpint(gtk_print_settings_get_bool(gtk_print_operation_get_print_settings(op),"insight-graph-tiled"),==,tiled);gtk_widget_destroy(options);g_object_unref(options);
 GtkPageSetup *setup=gtk_print_operation_get_default_page_setup(op);InsightGraphRect rect=insight_graph_rect(39,vertical);InsightGraphPrintPlan plan;
 g_assert_true(insight_graph_print_plan(rect.x+rect.width+16,rect.y+rect.height+16,gtk_page_setup_get_page_width(setup,GTK_UNIT_POINTS),gtk_page_setup_get_page_height(setup,GTK_UNIT_POINTS),tiled,&plan));if(tiled) g_assert_cmpint(plan.pages,>,1);
 /* Destroy live widgets and mutate their model before PDF generation. */
 gtk_entry_set_text(GTK_ENTRY(query),"MUTATED");gtk_list_store_clear(model);gtk_widget_destroy(graph);g_object_unref(graph);gtk_widget_destroy(tree);gtk_widget_destroy(query);g_object_unref(tree);g_object_unref(query);g_object_unref(model);
 export_pdf(op,plan.pages,"caller_39",NULL);g_object_unref(op);
}
static void tree_snapshot(void) {
 GtkTreeStore *model=gtk_tree_store_new(3,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG);GtkTreeIter root,child,hidden;
 gtk_tree_store_append(model,&root,NULL);gtk_tree_store_set(model,&root,0,"Root",2,(gulong)1,-1);gtk_tree_store_append(model,&child,&root);gtk_tree_store_set(model,&child,0,"VisibleBranch",1,"branch.c",2,(gulong)10,-1);gtk_tree_store_append(model,&hidden,&child);gtk_tree_store_set(model,&hidden,0,"HiddenChild",1,"hidden.c",2,(gulong)20,-1);
 GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model)),*query=gtk_entry_new();g_object_ref_sink(tree);g_object_ref_sink(query);gtk_entry_set_text(GTK_ENTRY(query),"Snapshot query");GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);gtk_tree_view_expand_row(GTK_TREE_VIEW(tree),path,FALSE);gtk_tree_path_free(path);
 GtkWidget *graph=insight_relation_graph_new_tree(GTK_TREE_VIEW(tree),GTK_ENTRY(query),0,1,2);g_object_ref_sink(graph);GtkPrintOperation *op=insight_relation_graph_print_operation(graph);paper(op,FALSE);
 gtk_widget_destroy(graph);g_object_unref(graph);gtk_widget_destroy(tree);gtk_widget_destroy(query);g_object_unref(tree);g_object_unref(query);g_object_unref(model);export_pdf(op,1,"VisibleBranch","HiddenChild");g_object_unref(op);
}
int main(int argc,char **argv) {
 if(!gtk_init_check(&argc,&argv)) return 77;g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 flat(FALSE,FALSE,5);flat(FALSE,FALSE,200);flat(TRUE,FALSE,29);flat(TRUE,TRUE,200);tree_snapshot();
 g_print("Graph printing GUI: vector PDF, custom layout tab, both orientations, zoom-independent pagination, immutable snapshot after widget destruction and collapsed branches passed\n");return 0;
}
