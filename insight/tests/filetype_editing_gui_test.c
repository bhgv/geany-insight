/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/filetype_editing.h"
static GtkWidget *named(GtkWidget *w,const gchar *name)
{ if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p && !found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL; }
int main(int argc,char **argv)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !gtk_init_check(&argc,&argv)) return 77;
 GtkWidget *window=gtk_window_new(GTK_WINDOW_TOPLEVEL),*page=insight_filetype_editing_new();gtk_window_set_default_size(GTK_WINDOW(window),680,330);gtk_container_add(GTK_CONTAINER(window),page);gtk_widget_show_all(window);
 GtkWidget *enabled=named(page,"insight-filetype-editing-enabled"),*type=named(page,"insight-filetype-editing-type"),*indent=named(page,"insight-filetype-editing-indent");g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(enabled)));g_assert_false(gtk_widget_is_sensitive(indent));
 InsightFiletypeOptions p,q;insight_filetype_options_default(&p);p.enabled=TRUE;p.indent_type=2;p.indent_width=2;p.tab_width=8;p.soft_wrap=TRUE;p.white_space=TRUE;p.right_margin=TRUE;
 insight_filetype_editing_set(page,&p);g_assert_true(gtk_widget_is_sensitive(indent));insight_filetype_editing_get(page,&q);g_assert_cmpint(q.indent_width,==,2);g_assert_cmpint(q.tab_width,==,8);g_assert_true(q.white_space);
 gtk_combo_box_set_active(GTK_COMBO_BOX(type),1);g_assert_false(gtk_widget_is_sensitive(indent));insight_filetype_editing_get(page,&q);g_assert_cmpint(q.indent_width,==,8);insight_filetype_editing_set(page,&p);
 const gchar *shot=g_getenv("GEANY_GUI_SCREENSHOT");if(shot) { gint64 end=g_get_monotonic_time()+200000;while(g_get_monotonic_time()<end) { while(g_main_context_iteration(NULL,FALSE));g_usleep(1000); }GdkWindow *w=gtk_widget_get_window(window);GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,gdk_window_get_width(w),gdk_window_get_height(w));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,shot,"jpeg",NULL,"quality","82",NULL));g_object_unref(image); }
 gtk_widget_destroy(window);g_print("Production file type editing page: inheritance, toggles, mixed indentation and roundtrip passed\n");return 0;
}
