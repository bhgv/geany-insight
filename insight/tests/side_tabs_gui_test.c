/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../../src/insight-side-tabs.h"
#include <string.h>
static GtkWidget *window,*book,*wrapper,*help;
static guint help_clicks;
static gboolean translated;
static const gchar *translate(const gchar *text) {return translated && !g_strcmp0(text,"File Symbols") ? "Символы файла":text;}
static void help_clicked(GtkButton *button,gpointer data) {(void)button;(void)data;help_clicks++;}
static guint phase,ticks;
static gint partial_width;
static const gchar *names[]={"File Symbols","Project Files","Project Symbols"};
static GtkWidget *find(GtkWidget *root,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(root),name)) return root;
 if(GTK_IS_CONTAINER(root)) {GList *children=gtk_container_get_children(GTK_CONTAINER(root));GtkWidget *result=NULL;for(GList *p=children;p && !result;p=p->next) result=find(p->data,name);g_list_free(children);return result;}return NULL;
}
static GtkWidget *tab(guint n) {gchar *name=g_strdup_printf("insight-side-tab-%u",n);GtkWidget *button=find(wrapper,name);g_free(name);g_assert_nonnull(button);return button;}
static gboolean carousel(void) {return g_object_get_data(G_OBJECT(wrapper),"insight-side-tabs-carousel")!=NULL;}
static void pages_intact(void) {for(gint n=0;n<gtk_notebook_get_n_pages(GTK_NOTEBOOK(book));n++) g_assert_true(gtk_widget_get_visible(gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),n)));}
static void single(gint at) {
 g_assert_true(carousel());for(gint n=0;n<3;n++) g_assert_cmpint(gtk_widget_get_visible(tab(n)),==,n==at);g_assert_cmpint(gtk_notebook_get_current_page(GTK_NOTEBOOK(book)),==,at);pages_intact();
}
/* Exercise GTK event propagation through the actual tab button and its parent
 * capture gesture, rather than only invoking drag callbacks directly. */
static void pointer_event(GtkWidget *button,GdkEventType type,gdouble x,guint state) {
 GdkWindow *native=gtk_button_get_event_window(GTK_BUTTON(button));g_assert_nonnull(native);gint root_x,root_y;gdk_window_get_origin(native,&root_x,&root_y);
 GdkEvent *event=gdk_event_new(type);
 if(type==GDK_MOTION_NOTIFY) {event->motion.window=g_object_ref(native);event->motion.send_event=TRUE;event->motion.time=GDK_CURRENT_TIME;event->motion.x=x;event->motion.y=5;event->motion.x_root=root_x+x;event->motion.y_root=root_y+5;event->motion.state=state;}
 else {event->button.window=g_object_ref(native);event->button.send_event=TRUE;event->button.time=GDK_CURRENT_TIME;event->button.x=x;event->button.y=5;event->button.x_root=root_x+x;event->button.y_root=root_y+5;event->button.state=state;event->button.button=GDK_BUTTON_PRIMARY;}
 gdk_event_set_device(event,gdk_seat_get_pointer(gdk_display_get_default_seat(gdk_display_get_default())));gdk_event_put(event);gdk_event_free(event);
}
static gboolean step(gpointer data) {
 (void)data;g_printerr("Side tabs phase %u, page %d\n",phase,gtk_notebook_get_current_page(GTK_NOTEBOOK(book)));if(++ticks>80) g_error("Side tabs timeout phase %u",phase);
 if(phase==0) {
  if(ticks<3) return G_SOURCE_CONTINUE;g_assert_false(carousel());gint first=gtk_widget_get_allocated_width(tab(0));g_assert_cmpint(first,>,200);
  for(guint n=0;n<3;n++) {g_assert_true(gtk_widget_get_visible(tab(n)));g_assert_cmpint(ABS(first-gtk_widget_get_allocated_width(tab(n))),<=,1);gchar *tip=gtk_widget_get_tooltip_text(tab(n));g_assert_cmpstr(tip,==,names[n]);g_free(tip);}
  g_assert_true(gtk_widget_get_visible(find(wrapper,"insight-side-tabs-next")));g_assert_true(gtk_widget_get_visible(find(wrapper,"insight-side-tabs-previous")));
  PangoLayout *layout=gtk_widget_create_pango_layout(tab(0),names[0]);gint text_width;pango_layout_get_pixel_size(layout,&text_width,NULL);g_object_unref(layout);GtkBorder pad,border;gtk_style_context_get_padding(gtk_widget_get_style_context(tab(0)),GTK_STATE_FLAG_NORMAL,&pad);gtk_style_context_get_border(gtk_widget_get_style_context(tab(0)),GTK_STATE_FLAG_NORMAL,&border);gint slot=text_width*3/4+pad.left+pad.right+border.left+border.right+4;partial_width=2*((text_width+1)/2+pad.left+pad.right+border.left+border.right+4)+28+gtk_widget_get_allocated_width(help)+2;gtk_window_resize(GTK_WINDOW(window),3*slot+28+gtk_widget_get_allocated_width(help),300);phase=11;return G_SOURCE_CONTINUE;
 }
 if(phase==11) {g_assert_false(carousel());g_assert_true(gtk_widget_get_visible(find(wrapper,"insight-side-tabs-next")));for(guint i=0;i<8;i++) insight_side_tabs_refresh(wrapper);phase=12;return G_SOURCE_CONTINUE;}
 if(phase==12) {g_assert_false(carousel());gtk_window_resize(GTK_WINDOW(window),partial_width,300);phase=13;return G_SOURCE_CONTINUE;}
 if(phase==13) {g_assert_false(carousel());g_assert_true(gtk_widget_get_visible(tab(0)));g_assert_true(gtk_widget_get_visible(tab(1)));g_assert_false(gtk_widget_get_visible(tab(2)));g_signal_emit_by_name(find(wrapper,"insight-side-tabs-next"),"clicked");g_signal_emit_by_name(find(wrapper,"insight-side-tabs-next"),"clicked");phase=14;return G_SOURCE_CONTINUE;}
 if(phase==14) {g_assert_false(carousel());g_assert_false(gtk_widget_get_visible(tab(0)));g_assert_true(gtk_widget_get_visible(tab(1)));g_assert_true(gtk_widget_get_visible(tab(2)));gtk_notebook_set_current_page(GTK_NOTEBOOK(book),0);gtk_window_resize(GTK_WINDOW(window),95,300);phase=1;return G_SOURCE_CONTINUE;}
 if(phase==1) {single(0);g_signal_emit_by_name(find(wrapper,"insight-side-tabs-next"),"clicked");phase=2;return G_SOURCE_CONTINUE;}
 if(phase==2) {single(1);g_signal_emit_by_name(help,"clicked");g_assert_cmpuint(help_clicks,==,1);g_signal_emit_by_name(find(wrapper,"insight-side-tabs-previous"),"clicked");phase=3;return G_SOURCE_CONTINUE;}
 if(phase==3) {single(0);g_signal_emit_by_name(find(wrapper,"insight-side-tabs-previous"),"clicked");phase=4;return G_SOURCE_CONTINUE;}
 if(phase==4) {single(2);GtkWidget *button=tab(2);gdouble start=MAX(72,gtk_widget_get_allocated_width(button)-8);pointer_event(button,GDK_BUTTON_PRESS,start,0);pointer_event(button,GDK_MOTION_NOTIFY,start-70,GDK_BUTTON1_MASK);pointer_event(button,GDK_BUTTON_RELEASE,start-70,GDK_BUTTON1_MASK);phase=5;return G_SOURCE_CONTINUE;}
 if(phase==5) {single(0);gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(book),gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0),"Символы текущего файла — полное название");phase=6;return G_SOURCE_CONTINUE;}
 if(phase==6) {single(0);gchar *tip=gtk_widget_get_tooltip_text(tab(0));g_assert_cmpstr(tip,==,"Символы текущего файла — полное название");g_free(tip);gtk_window_resize(GTK_WINDOW(window),24,300);phase=7;return G_SOURCE_CONTINUE;}
 if(phase==7) {single(0);g_assert_false(gtk_widget_get_visible(find(wrapper,"insight-side-tabs-next")));gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(book),gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),0),names[0]);gtk_window_resize(GTK_WINDOW(window),900,300);phase=8;return G_SOURCE_CONTINUE;}
 if(phase==8) {g_assert_true(gtk_widget_get_visible(find(wrapper,"insight-side-tabs-next")));g_assert_true(gtk_widget_get_visible(find(wrapper,"insight-side-tabs-previous")));g_assert_false(carousel());for(guint n=0;n<3;n++) g_assert_true(gtk_widget_get_visible(tab(n)));GtkWidget *page=gtk_notebook_get_nth_page(GTK_NOTEBOOK(book),1);g_object_ref(page);gtk_notebook_remove_page(GTK_NOTEBOOK(book),1);gtk_notebook_append_page(GTK_NOTEBOOK(book),page,gtk_label_new("Reattached panel"));g_object_unref(page);phase=9;return G_SOURCE_CONTINUE;}
 if(phase==9) {pages_intact();gchar *tip=gtk_widget_get_tooltip_text(tab(2));g_assert_cmpstr(tip,==,"Reattached panel");g_free(tip);translated=TRUE;insight_side_tabs_refresh(wrapper);phase=10;return G_SOURCE_CONTINUE;}
 if(phase==10) {gchar *tip=gtk_widget_get_tooltip_text(tab(0));g_assert_cmpstr(tip,==,"Символы файла");g_free(tip);GtkWidget *label=gtk_bin_get_child(GTK_BIN(tab(0)));g_assert_cmpint(gtk_label_get_width_chars(GTK_LABEL(label)),==,1);g_assert_cmpint(gtk_label_get_ellipsize(GTK_LABEL(label)),==,PANGO_ELLIPSIZE_END);insight_side_tabs_refresh(wrapper);gtk_widget_destroy(window);g_print("Proportional side tabs, active-only carousel, cyclic arrows, GTK pointer drag propagation, live captions/tooltips, 24px collapse, re-expansion, detach/reattach and queued teardown passed\n");gtk_main_quit();return G_SOURCE_REMOVE;}
 return G_SOURCE_CONTINUE;
}
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;gtk_init(&argc,&argv);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);
 window=gtk_window_new(GTK_WINDOW_TOPLEVEL);book=gtk_notebook_new();GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_EXTERNAL,GTK_POLICY_EXTERNAL);gtk_container_add(GTK_CONTAINER(window),scroll);gtk_container_add(GTK_CONTAINER(scroll),book);
 for(guint n=0;n<3;n++) {GtkWidget *page=gtk_label_new("Panel contents remain visible and unchanged");gtk_label_set_ellipsize(GTK_LABEL(page),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(page),1);gtk_notebook_append_page(GTK_NOTEBOOK(book),page,gtk_label_new(names[n]));}
 help=gtk_button_new_with_label("?");g_signal_connect(help,"clicked",G_CALLBACK(help_clicked),NULL);gtk_notebook_set_action_widget(GTK_NOTEBOOK(book),help,GTK_PACK_END);wrapper=insight_side_tabs_wrap(GTK_NOTEBOOK(book),translate);g_assert_nonnull(wrapper);gtk_window_set_default_size(GTK_WINDOW(window),900,300);gtk_widget_show_all(window);g_timeout_add(150,step,NULL);gtk_main();return 0;
}
