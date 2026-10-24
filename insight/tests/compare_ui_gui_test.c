/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Production comparison chrome; only translations stubbed. */
#include "../../src/insight-compare-ui.h"
const gchar *insight_tr(const gchar *s) { return s; }
static GtkWidget *window,*tabs,*short_page,*long_page,*last,*body,*upper,*lower;
static guint phase,ticks;static gint short_height,long_height,body_height,upper_height,lower_height;
static GtkWidget *control(GtkWidget *page,const gchar *text) { GtkWidget *button=gtk_button_new_with_label(text);gtk_box_pack_start(GTK_BOX(page),button,FALSE,FALSE,0);gtk_widget_show(button);return button; }
static void contained(GtkWidget *w) {
 GtkWidget *viewport=gtk_widget_get_parent(gtk_widget_get_parent(w));gint x,y;g_assert_true(GTK_IS_VIEWPORT(viewport));g_assert_true(gtk_widget_translate_coordinates(w,viewport,0,0,&x,&y));g_assert_cmpint(y,>=,0);g_assert_cmpint(y+gtk_widget_get_allocated_height(w),<=,gtk_widget_get_allocated_height(viewport));
}
static gboolean step(gpointer unused) {
 (void)unused;if(++ticks>100) g_error("Comparison chrome timeout phase %u",phase);
 if(phase==0) { if(!g_object_get_data(G_OBJECT(tabs),"insight-ai-fold")) return G_SOURCE_CONTINUE;short_height=gtk_widget_get_allocated_height(tabs);gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),1);phase++; }
 else if(phase==1) { long_height=gtk_widget_get_allocated_height(tabs);if(long_height<=short_height+30) return G_SOURCE_CONTINUE;contained(last);body_height=gtk_widget_get_allocated_height(body);upper_height=gtk_widget_get_allocated_height(upper);lower_height=gtk_widget_get_allocated_height(lower);InsightAiFold *f=g_object_get_data(G_OBJECT(tabs),"insight-ai-fold");gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),gtk_notebook_page_num(GTK_NOTEBOOK(tabs),f->collapse));phase++; }
 else if(phase==2) { if(gtk_widget_get_allocated_height(body)<=body_height+20) return G_SOURCE_CONTINUE;g_assert_cmpint(gtk_widget_get_allocated_height(tabs),<,short_height);g_assert_cmpint(gtk_widget_get_allocated_height(upper),>,upper_height);g_assert_cmpint(gtk_widget_get_allocated_height(lower),>,lower_height);gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),0);phase++; }
 else if(phase==3) { if(gtk_widget_get_allocated_height(tabs)>short_height+3) return G_SOURCE_CONTINUE;last=control(short_page,"Added after opening");control(short_page,"Another added control");phase++; }
 else if(phase==4) { if(gtk_widget_get_allocated_height(tabs)<=short_height+15) return G_SOURCE_CONTINUE;contained(last);GtkCssProvider *css=gtk_css_provider_new();gtk_css_provider_load_from_data(css,".insight-compare-toolbar { font-size: 18pt; }",-1,NULL);gtk_style_context_add_provider_for_screen(gtk_widget_get_screen(window),GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_USER);g_object_unref(css);gtk_notebook_set_current_page(GTK_NOTEBOOK(tabs),1);phase++; }
 else if(phase==5) { GtkWidget *end=g_object_get_data(G_OBJECT(long_page),"last-control");contained(end);gtk_widget_destroy(window);gtk_main_quit();g_print("Comparison chrome: active natural height, complete controls, collapse, proportional content growth, dynamic children and large fonts passed\n");return G_SOURCE_REMOVE; }
 return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
 if(!g_getenv("DISPLAY")) return 77;gtk_init(&argc,&argv);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_default_size(GTK_WINDOW(window),900,700);GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_container_add(GTK_CONTAINER(window),root);tabs=insight_compare_ui_tabs(root,"fixture-compare-tabs");short_page=insight_compare_ui_page(tabs,"Short");control(short_page,"One row");long_page=insight_compare_ui_page(tabs,"Tall");control(long_page,"A wide control group: left source path / right source path / source offset / source count / result offset / result replacement count / copy / save / compare / navigation / additional settings");for(guint i=0;i<5;i++) last=control(long_page,"A complete control row");g_object_set_data(G_OBJECT(long_page),"last-control",last);body=gtk_paned_new(GTK_ORIENTATION_VERTICAL);upper=gtk_text_view_new();lower=gtk_text_view_new();gtk_paned_pack1(GTK_PANED(body),upper,TRUE,FALSE);gtk_paned_pack2(GTK_PANED(body),lower,TRUE,FALSE);gtk_paned_set_position(GTK_PANED(body),220);gtk_box_pack_start(GTK_BOX(root),body,TRUE,TRUE,0);gtk_widget_show_all(window);g_timeout_add(120,step,NULL);gtk_main();return 0;
}
