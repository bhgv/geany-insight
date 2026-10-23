/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Production GTK/Pango printing exported to PDF; no printer is required. */
#include "../../src/insight-text-print.h"
#include <glib/gstdio.h>
#include <string.h>
const gchar *insight_tr(const gchar *text) { return text; }
static guint drawn;
static void page(GtkPrintOperation *op,GtkPrintContext *context,gint n,gpointer data) { (void)op;(void)context;(void)data;g_assert_cmpint(n,==,(gint)drawn++); }
static void paper(GtkPrintOperation *op,gdouble width,gdouble height) {
    GtkPageSetup *setup=gtk_page_setup_new();GtkPaperSize *size=gtk_paper_size_new_custom("fixture","Fixture paper",width,height,GTK_UNIT_POINTS);gtk_page_setup_set_paper_size(setup,size);gtk_paper_size_free(size);
    gtk_page_setup_set_top_margin(setup,0,GTK_UNIT_POINTS);gtk_page_setup_set_bottom_margin(setup,0,GTK_UNIT_POINTS);gtk_page_setup_set_left_margin(setup,0,GTK_UNIT_POINTS);gtk_page_setup_set_right_margin(setup,0,GTK_UNIT_POINTS);gtk_print_operation_set_default_page_setup(op,setup);g_object_unref(setup);
}
static gchar *export(GtkPrintOperation *op,guint *pages) {
    GError *error=NULL;gchar *dir=g_dir_make_tmp("insight-text-print-XXXXXX",&error);g_assert_no_error(error);gchar *path=g_build_filename(dir,"list.pdf",NULL);
    drawn=0;g_signal_connect(op,"draw-page",G_CALLBACK(page),NULL);gtk_print_operation_set_export_filename(op,path);
    GtkPrintOperationResult result=gtk_print_operation_run(op,GTK_PRINT_OPERATION_ACTION_EXPORT,NULL,&error);g_assert_no_error(error);g_assert_cmpint(result,==,GTK_PRINT_OPERATION_RESULT_APPLY);g_assert_null(g_object_get_data(G_OBJECT(op),"insight-text-print-error"));
    gint n=0;g_object_get(op,"n-pages",&n,NULL);g_assert_cmpint(n,==,(gint)drawn);*pages=drawn;
    gchar *text=NULL,*errors=NULL;gint status=0;gchar *args[]={"pdftotext","-layout",path,"-",NULL};g_assert_true(g_spawn_sync(NULL,args,NULL,G_SPAWN_SEARCH_PATH,NULL,NULL,&text,&errors,&status,&error));g_assert_no_error(error);g_assert_cmpint(status,==,0);g_free(errors);
    g_remove(path);g_rmdir(dir);g_free(path);g_free(dir);return text;
}
static void rejected_paper(gdouble width,gdouble height,guint lines) {
    GString *text=g_string_new(NULL);for(guint i=0;i<lines;i++) g_string_append(text,"row\n");GtkPrintOperation *op=insight_text_print_operation("Limits",text->str,text->len);g_string_free(text,TRUE);g_assert_nonnull(op);paper(op,width,height);
    gchar *dir=g_dir_make_tmp("insight-print-reject-XXXXXX",NULL),*path=g_build_filename(dir,"rejected.pdf",NULL);gtk_print_operation_set_export_filename(op,path);GError *error=NULL;
    GtkPrintOperationResult result=gtk_print_operation_run(op,GTK_PRINT_OPERATION_ACTION_EXPORT,NULL,&error);g_assert_cmpint(result,!=,GTK_PRINT_OPERATION_RESULT_APPLY);g_assert_nonnull(g_object_get_data(G_OBJECT(op),"insight-text-print-error"));g_clear_error(&error);g_object_unref(op);g_remove(path);g_rmdir(dir);g_free(path);g_free(dir);
}
static gboolean cancel_print(gpointer data) {
    guint *attempts=data;g_assert_cmpuint(++*attempts,<,100);GList *list=gtk_window_list_toplevels();
    for(GList *p=list;p;p=p->next) if(GTK_IS_DIALOG(p->data) && gtk_widget_get_visible(p->data) && g_str_has_prefix(G_OBJECT_TYPE_NAME(p->data),"GtkPrint")) { gtk_dialog_response(GTK_DIALOG(p->data),GTK_RESPONSE_CANCEL);g_list_free(list);return G_SOURCE_REMOVE; }
    g_list_free(list);return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
    if(!gtk_init_check(&argc,&argv)) return 77;g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
    g_assert_null(insight_text_print_operation("Bad","x\0y",3));g_assert_null(insight_text_print_operation("Bad","\xff",1));g_assert_null(insight_text_print_operation("Bad","\001",1));
    gchar *large=g_strnfill(1024*1024+1,'x');g_assert_null(insight_text_print_operation("Big",large,1024*1024+1));g_free(large);large=g_strnfill(65537,'x');g_assert_null(insight_text_print_operation("Long line",large,65537));g_free(large);
    large=g_strnfill(20000,'\n');g_assert_null(insight_text_print_operation("Many lines",large,20000));g_free(large);
    GString *source=g_string_new("Привет — source\r\n");for(guint i=0;i<200;i++) g_string_append_printf(source,"  row_%03u    sample.c:%u\n",i,i+1);
    gchar *title=g_strdup("Immutable print fixture");GtkPrintOperation *op=insight_text_print_operation(title,source->str,source->len);g_assert_nonnull(op);g_string_assign(source,"MUTATED");g_string_free(source,TRUE);memset(title,'x',strlen(title));g_free(title);paper(op,420,250);
    guint pages=0;gchar *pdf=export(op,&pages);g_assert_cmpuint(pages,>,1);g_assert_nonnull(strstr(pdf,"Привет"));g_assert_null(strstr(pdf,"MUTATED"));
    for(guint i=0;i<200;i++) { gchar *needle=g_strdup_printf("row_%03u",i);g_assert_nonnull(strstr(pdf,needle));g_free(needle); }
    for(guint i=1;i<=pages;i++) { gchar *footer=g_strdup_printf("Page %u / %u",i,pages);g_assert_nonnull(strstr(pdf,footer));g_free(footer); }
    gchar **parts=g_strsplit(pdf,"Immutable print fixture",-1);g_assert_cmpuint(g_strv_length(parts)-1,==,pages);g_strfreev(parts);g_free(pdf);g_object_unref(op);
    source=g_string_new(NULL);for(guint i=0;i<200;i++) g_string_append_printf(source,"word%03u ",i);g_string_append(source,"WRAPPED_END");op=insight_text_print_operation("Wrapped",source->str,source->len);g_string_free(source,TRUE);paper(op,180,170);pdf=export(op,&pages);g_assert_cmpuint(pages,>,1);
    for(guint i=0;i<200;i++) { gchar *needle=g_strdup_printf("word%03u",i);g_assert_nonnull(strstr(pdf,needle));g_free(needle); }g_assert_nonnull(strstr(pdf,"WRAPPED_END"));g_free(pdf);g_object_unref(op);
    op=insight_text_print_operation("Empty","",0);g_assert_nonnull(op);pdf=export(op,&pages);g_assert_cmpuint(pages,==,1);g_free(pdf);g_object_unref(op);
    rejected_paper(40,40,1);rejected_paper(80,100,10000);
    op=insight_text_print_operation("Cancel","unchanged",9);guint attempts=0;g_timeout_add(50,cancel_print,&attempts);insight_text_print_run(op,NULL);g_assert_cmpuint(attempts,>,0);
    g_print("Text print: immutable UTF-8 snapshots, CRLF, wrapped lines across pages, headers/footers, PDF text, limits, blank page and native cancellation passed\n");return 0;
}
