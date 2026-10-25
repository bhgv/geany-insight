/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_COMPARE_UI_H
#define INSIGHT_COMPARE_UI_H
#include <gtk/gtk.h>
#include "insight-i18n.h"
#include "insight-ai-ui.h"
/* Shared chrome only: no comparison state, source fonts or operation callbacks. */
#define INSIGHT_COMPARE_WIDTH 1100
#define INSIGHT_COMPARE_HEIGHT 700
#define INSIGHT_COMPARE_CODE_POINTS 10
#define INSIGHT_COMPARE_CONTROL_PERCENT 80
static inline void insight_compare_ui_move(GtkWidget *widget,GtkWidget *page)
{
 g_object_ref(widget);GtkWidget *old=gtk_widget_get_parent(widget);if(old) gtk_container_remove(GTK_CONTAINER(old),widget);
 gtk_box_pack_start(GTK_BOX(page),widget,FALSE,FALSE,0);g_object_unref(widget);
}
/* Only the active page contributes its controls' height. GtkNotebook normally
 * reserves the largest page, while a fixed scroller height clips taller groups. */
static gboolean insight_compare_ui_layout(gpointer data) {
 GtkWidget *widget=data;g_object_set_data(G_OBJECT(widget),"insight-compare-layout-idle",NULL);
 if(!gtk_widget_get_parent(widget) || gtk_widget_in_destruction(widget)) return G_SOURCE_REMOVE;
 GtkNotebook *tabs=GTK_NOTEBOOK(widget);if(!gtk_notebook_get_n_pages(tabs)) return G_SOURCE_REMOVE;insight_ai_ui_fold(widget,"Collapse");
 if(g_object_get_data(G_OBJECT(widget),"insight-ai-options-folded")) return G_SOURCE_REMOVE;
 gint current=gtk_notebook_get_current_page(tabs),width=MAX(1,gtk_widget_get_allocated_width(widget)-12),height=700;GtkWidget *top=gtk_widget_get_toplevel(widget);if(GTK_IS_WINDOW(top)) { gint unused;gtk_window_get_size(GTK_WINDOW(top),&unused,&height);GdkRectangle area=insight_ai_workarea(top);height=MIN(height,area.height-32); }
 for(gint i=0;i<gtk_notebook_get_n_pages(tabs);i++) {
  GtkWidget *page=gtk_notebook_get_nth_page(tabs,i);if(!GTK_IS_SCROLLED_WINDOW(page)) continue;
  gint target=0;if(i==current) {
   GtkWidget *content=gtk_bin_get_child(GTK_BIN(page));if(GTK_IS_VIEWPORT(content)) content=gtk_bin_get_child(GTK_BIN(content));
   if(content && gtk_widget_get_visible(content)) { gint minimum,natural,minimum_width,natural_width;gtk_widget_get_preferred_width(content,&minimum_width,&natural_width);gtk_widget_get_preferred_height_for_width(content,MAX(width,minimum_width),&minimum,&natural);target=natural+12;
    if(minimum_width>width) { GtkWidget *bar=gtk_scrolled_window_get_hscrollbar(GTK_SCROLLED_WINDOW(page));gtk_widget_get_preferred_height(bar,&minimum,&natural);target+=MAX(natural,24); }
    target=MIN(target,MAX(64,height-180));
   }
  }
  gint old_w,old_h;gtk_widget_get_size_request(page,&old_w,&old_h);if(old_h!=target) gtk_widget_set_size_request(page,old_w,target);
 }
 return G_SOURCE_REMOVE;
}
static void insight_compare_ui_queue(GtkWidget *tabs) {
 if(!g_object_get_data(G_OBJECT(tabs),"insight-compare-layout-idle") && !gtk_widget_in_destruction(tabs)) { guint id=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,insight_compare_ui_layout,g_object_ref(tabs),g_object_unref);g_object_set_data(G_OBJECT(tabs),"insight-compare-layout-idle",GUINT_TO_POINTER(id)); }
}
static void insight_compare_ui_allocate(GtkWidget *widget,GtkAllocation *allocation,gpointer data) { (void)widget;(void)allocation;insight_compare_ui_queue(data); }
static void insight_compare_ui_switch(GtkNotebook *book,GtkWidget *page,guint number,gpointer data) { (void)page;(void)number;(void)data;insight_compare_ui_queue(GTK_WIDGET(book)); }
static void insight_compare_ui_style(GtkWidget *widget,gpointer data) { (void)data;insight_compare_ui_queue(widget); }
static inline void insight_compare_ui_adopt(GtkWidget *tabs)
{
 if(g_object_get_data(G_OBJECT(tabs),"insight-controls-ready")) return;
 g_object_set_data(G_OBJECT(tabs),"insight-controls-ready",GINT_TO_POINTER(1));
 GdkScreen *screen=gtk_widget_get_screen(tabs);
 if(!g_object_get_data(G_OBJECT(screen),"insight-compare-ui-css")) {
  GtkCssProvider *css=gtk_css_provider_new();gtk_css_provider_load_from_data(css,
   ".insight-compare-code { font-size: " G_STRINGIFY(INSIGHT_COMPARE_CODE_POINTS) "pt; } .insight-compare-toolbar { font-size: " G_STRINGIFY(INSIGHT_COMPARE_CONTROL_PERCENT) "%; } "
   ".insight-compare-toolbar button, .insight-compare-toolbar entry, .insight-compare-toolbar combobox { min-height: 0; min-width: 0; padding: 2.4px 4.8px; } "
   ".insight-compare-toolbar checkbutton check { min-height: 12px; min-width: 12px; } "
   ".insight-compare-toolbar tab { min-height: 0; padding: 2.4px 6px; } "
   ".insight-macos .insight-compare-toolbar > header > tabs > tab label { font-size: 10.8pt; }",-1,NULL);
  gtk_style_context_add_provider_for_screen(screen,GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_set_data_full(G_OBJECT(screen),"insight-compare-ui-css",css,g_object_unref);
 }
 gtk_style_context_add_class(gtk_widget_get_style_context(tabs),"insight-compare-toolbar");
 gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabs),TRUE);g_signal_connect(tabs,"size-allocate",G_CALLBACK(insight_compare_ui_allocate),tabs);g_signal_connect_after(tabs,"switch-page",G_CALLBACK(insight_compare_ui_switch),NULL);g_signal_connect(tabs,"style-updated",G_CALLBACK(insight_compare_ui_style),NULL);insight_compare_ui_queue(tabs);
}
static inline GtkWidget *insight_compare_ui_tabs(GtkWidget *root,const gchar *name)
{
 GtkWidget *tabs=gtk_notebook_new();gtk_widget_set_name(tabs,name);gtk_box_pack_start(GTK_BOX(root),tabs,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(root),tabs,0);insight_compare_ui_adopt(tabs);return tabs;
}
/* Labels are localized by callers; standalone directory fixtures need no Geany. */
static inline GtkWidget *insight_compare_ui_page(GtkWidget *tabs,const gchar *label)
{
 GtkWidget *page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
 gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scroll),0);gtk_scrolled_window_set_propagate_natural_height(GTK_SCROLLED_WINDOW(scroll),FALSE);gtk_container_add(GTK_CONTAINER(scroll),page);g_signal_connect_object(page,"size-allocate",G_CALLBACK(insight_compare_ui_allocate),tabs,0);
 GtkWidget *tab=gtk_label_new(NULL);insight_i18n_key(G_OBJECT(tab),"label",label);gtk_notebook_append_page(GTK_NOTEBOOK(tabs),scroll,tab);insight_compare_ui_queue(tabs);return page;
}
static inline GtkWidget *insight_compare_ui_row(GtkWidget *widget,GtkWidget *box)
{
 while(gtk_widget_get_parent(widget) && gtk_widget_get_parent(widget)!=box) widget=gtk_widget_get_parent(widget);
 return widget;
}
static inline void insight_compare_ui_reveal(GtkWidget *widget)
{
 for(GtkWidget *child=widget,*parent=gtk_widget_get_parent(child);parent;child=parent,parent=gtk_widget_get_parent(child))
  if(GTK_IS_NOTEBOOK(parent)) gtk_notebook_set_current_page(GTK_NOTEBOOK(parent),gtk_notebook_page_num(GTK_NOTEBOOK(parent),child));
}
#endif
