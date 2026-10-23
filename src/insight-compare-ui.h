/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef INSIGHT_COMPARE_UI_H
#define INSIGHT_COMPARE_UI_H
#include <gtk/gtk.h>
/* Shared chrome only: no comparison state, source fonts or operation callbacks. */
#define INSIGHT_COMPARE_WIDTH 1100
#define INSIGHT_COMPARE_HEIGHT 700
static inline void insight_compare_ui_move(GtkWidget *widget,GtkWidget *page)
{
 g_object_ref(widget);GtkWidget *old=gtk_widget_get_parent(widget);if(old) gtk_container_remove(GTK_CONTAINER(old),widget);
 gtk_box_pack_start(GTK_BOX(page),widget,FALSE,FALSE,0);g_object_unref(widget);
}
static inline GtkWidget *insight_compare_ui_tabs(GtkWidget *root,const gchar *name)
{
 GdkScreen *screen=gtk_widget_get_screen(root);
 if(!g_object_get_data(G_OBJECT(screen),"insight-compare-ui-css")) {
  GtkCssProvider *css=gtk_css_provider_new();gtk_css_provider_load_from_data(css,
   ".insight-compare-toolbar { font-size: 66.667%; } "
   ".insight-compare-toolbar button, .insight-compare-toolbar entry, .insight-compare-toolbar combobox { min-height: 0; min-width: 0; padding: 2px 4px; } "
   ".insight-compare-toolbar checkbutton check { min-height: 10px; min-width: 10px; } "
   ".insight-compare-toolbar tab { min-height: 0; padding: 2px 5px; }",-1,NULL);
  gtk_style_context_add_provider_for_screen(screen,GTK_STYLE_PROVIDER(css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_set_data_full(G_OBJECT(screen),"insight-compare-ui-css",css,g_object_unref);
 }
 GtkWidget *tabs=gtk_notebook_new();gtk_widget_set_name(tabs,name);gtk_style_context_add_class(gtk_widget_get_style_context(tabs),"insight-compare-toolbar");
 gtk_notebook_set_scrollable(GTK_NOTEBOOK(tabs),TRUE);gtk_box_pack_start(GTK_BOX(root),tabs,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(root),tabs,0);return tabs;
}
/* Labels are localized by callers; standalone directory fixtures need no Geany. */
static inline GtkWidget *insight_compare_ui_page(GtkWidget *tabs,const gchar *label)
{
 GtkWidget *page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);
 gtk_widget_set_size_request(scroll,-1,86);gtk_container_add(GTK_CONTAINER(scroll),page);
 gtk_notebook_append_page(GTK_NOTEBOOK(tabs),scroll,gtk_label_new(label));return page;
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
