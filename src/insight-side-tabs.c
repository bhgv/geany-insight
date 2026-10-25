/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-side-tabs.h"
#include <gdk/gdkkeysyms.h>
typedef struct {
 GtkWidget *wrapper,*row,*tabs,*previous,*next;
 GtkNotebook *book;
 GPtrArray *buttons;
 GtkCssProvider *css;
 const gchar *(*translate)(const gchar *);
 guint idle;
 gboolean alive,rebuild,single,focus_active;
 gdouble consumed;
} SideTabs;
static void queue_update(SideTabs *s);
static const gchar *caption(GtkNotebook *book,GtkWidget *page) {
 GtkWidget *label=gtk_notebook_get_tab_label(book,page);
 return GTK_IS_LABEL(label) ? gtk_label_get_text(GTK_LABEL(label)):"";
}
static const gchar *translated(SideTabs *s,const gchar *text) {return s->translate ? s->translate(text):text;}
static void weak_free(gpointer data) {GWeakRef *ref=data;g_weak_ref_clear(ref);g_free(ref);}
static GtkWidget *button_page(GtkWidget *button) {return g_weak_ref_get(g_object_get_data(G_OBJECT(button),"insight-side-tab-page"));}
static void changed(GObject *object,GParamSpec *spec,gpointer data) {
 (void)object;(void)spec;GtkWidget *button=data;SideTabs *s=g_object_get_data(G_OBJECT(button),"insight-side-tab-owner");if(s->alive) queue_update(s);
}
static void label_replaced(GObject *object,GParamSpec *spec,gpointer data) {
 (void)object;(void)spec;GtkWidget *button=data;SideTabs *s=g_object_get_data(G_OBJECT(button),"insight-side-tab-owner");s->rebuild=TRUE;if(s->alive) queue_update(s);
}
static void select_button(GtkButton *button,gpointer data) {
 SideTabs *s=data;GtkWidget *page=button_page(GTK_WIDGET(button));
 if(page) {gint at=gtk_notebook_page_num(s->book,page);if(at>=0) gtk_notebook_set_current_page(s->book,at);g_object_unref(page);}queue_update(s);
}
static void turn(SideTabs *s,gint direction) {
 gint count=gtk_notebook_get_n_pages(s->book),current=gtk_notebook_get_current_page(s->book);
 for(gint n=1;n<=count;n++) {gint at=(MAX(0,current)+direction*n+count*n)%count;GtkWidget *page=gtk_notebook_get_nth_page(s->book,at);if(gtk_widget_get_visible(page)) {gtk_notebook_set_current_page(s->book,at);queue_update(s);return;}}
}
static void previous_clicked(GtkButton *button,gpointer data) {(void)button;turn(data,-1);}
static void next_clicked(GtkButton *button,gpointer data) {(void)button;turn(data,1);}
static void drag_begin(GtkGestureDrag *gesture,gdouble x,gdouble y,gpointer data) {(void)gesture;(void)x;(void)y;((SideTabs *)data)->consumed=0;}
static void drag_update(GtkGestureDrag *gesture,gdouble x,gdouble y,gpointer data) {
 (void)y;SideTabs *s=data;if(!s->single || ABS(x-s->consumed)<40) return;
 if(gtk_gesture_is_active(GTK_GESTURE(gesture))) gtk_gesture_set_state(GTK_GESTURE(gesture),GTK_EVENT_SEQUENCE_CLAIMED);turn(s,x<s->consumed ? 1:-1);s->consumed=x;
}
static gboolean key_press(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;if(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK)) return FALSE;
 if(event->keyval==GDK_KEY_Left || event->keyval==GDK_KEY_Right) {SideTabs *s=data;s->focus_active=TRUE;turn(s,event->keyval==GDK_KEY_Left ? -1:1);return TRUE;}return FALSE;
}
static void rebuild(SideTabs *s) {
 for(guint n=0;n<s->buttons->len;n++) gtk_widget_destroy(g_ptr_array_index(s->buttons,n));g_ptr_array_set_size(s->buttons,0);
 for(gint n=0;n<gtk_notebook_get_n_pages(s->book);n++) {
  GtkWidget *page=gtk_notebook_get_nth_page(s->book,n),*button=gtk_toggle_button_new_with_label(caption(s->book,page));
  gchar *name=g_strdup_printf("insight-side-tab-%d",n);gtk_widget_set_name(button,name);g_free(name);
  gtk_widget_set_no_show_all(button,TRUE);gtk_style_context_add_class(gtk_widget_get_style_context(button),"insight-side-tab");gtk_button_set_focus_on_click(GTK_BUTTON(button),FALSE);
  GtkWidget *label=gtk_bin_get_child(GTK_BIN(button));gtk_label_set_ellipsize(GTK_LABEL(label),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(label),1);
  g_object_set_data(G_OBJECT(button),"insight-no-i18n",GINT_TO_POINTER(1));g_object_set_data(G_OBJECT(label),"insight-no-i18n",GINT_TO_POINTER(1));
  GWeakRef *ref=g_new0(GWeakRef,1);g_weak_ref_init(ref,page);g_object_set_data_full(G_OBJECT(button),"insight-side-tab-page",ref,weak_free);g_object_set_data(G_OBJECT(button),"insight-side-tab-owner",s);
  gtk_style_context_add_provider(gtk_widget_get_style_context(button),GTK_STYLE_PROVIDER(s->css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+1);
  g_signal_connect(button,"clicked",G_CALLBACK(select_button),s);
  GtkWidget *native=gtk_notebook_get_tab_label(s->book,page);if(GTK_IS_LABEL(native)) g_signal_connect_object(native,"notify::label",G_CALLBACK(changed),button,0);
  g_signal_connect_object(page,"notify::visible",G_CALLBACK(changed),button,0);g_signal_connect_object(page,"child-notify::tab-label",G_CALLBACK(label_replaced),button,0);
  gtk_box_pack_start(GTK_BOX(s->tabs),button,TRUE,TRUE,0);g_ptr_array_add(s->buttons,button);
 }
 s->rebuild=FALSE;
}
static gboolean update(gpointer data) {
 SideTabs *s=data;s->idle=0;if(!s->alive) return G_SOURCE_REMOVE;if(s->rebuild) rebuild(s);
 gint current=gtk_notebook_get_current_page(s->book),visible=0,active_width=0;
 gint width=gtk_widget_get_allocated_width(s->wrapper);GtkWidget *parent=gtk_widget_get_parent(s->wrapper);if(parent) width=MIN(width,gtk_widget_get_allocated_width(parent));
 for(guint n=0;n<s->buttons->len;n++) {
  GtkWidget *button=g_ptr_array_index(s->buttons,n),*page=button_page(button);if(!page) continue;
  const gchar *text=caption(s->book,page);if(g_strcmp0(gtk_button_get_label(GTK_BUTTON(button)),text)) gtk_button_set_label(GTK_BUTTON(button),text);
  gchar *old=gtk_widget_get_tooltip_text(button);if(g_strcmp0(old,text)) gtk_widget_set_tooltip_text(button,text);g_free(old);
  if(gtk_widget_get_visible(page)) visible++;
  if((gint)n==current) {
   PangoLayout *layout=gtk_widget_create_pango_layout(gtk_bin_get_child(GTK_BIN(button)),text);pango_layout_get_pixel_size(layout,&active_width,NULL);g_object_unref(layout);
   GtkBorder padding,border;GtkStyleContext *style=gtk_widget_get_style_context(button);gtk_style_context_get_padding(style,GTK_STATE_FLAG_NORMAL,&padding);gtk_style_context_get_border(style,GTK_STATE_FLAG_NORMAL,&border);active_width+=padding.left+padding.right+border.left+border.right+4;
  }
  g_object_unref(page);
 }
 s->single=visible>1 && active_width>MAX(1,width/MAX(1,visible));
 g_object_set_data(G_OBJECT(s->wrapper),"insight-side-tabs-carousel",GINT_TO_POINTER(s->single));
 gtk_widget_set_visible(s->previous,s->single && width>=40);gtk_widget_set_visible(s->next,s->single && width>=40);
 gtk_widget_set_tooltip_text(s->previous,translated(s,"Previous panel tab"));gtk_widget_set_tooltip_text(s->next,translated(s,"Next panel tab"));
 for(guint n=0;n<s->buttons->len;n++) {
  GtkWidget *button=g_ptr_array_index(s->buttons,n),*page=button_page(button);gboolean selected=(gint)n==current;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(button),selected);gtk_widget_set_visible(button,page && gtk_widget_get_visible(page) && (!s->single || selected));if(selected && s->focus_active) gtk_widget_grab_focus(button);if(page) g_object_unref(page);
 }
 s->focus_active=FALSE;gtk_widget_set_visible(s->row,visible>0);return G_SOURCE_REMOVE;
}
static void queue_update(SideTabs *s) {if(s->alive && !s->idle) s->idle=g_idle_add(update,s);}
void insight_side_tabs_refresh(gpointer wrapper) {SideTabs *s=g_object_get_data(G_OBJECT(wrapper),"insight-side-tabs");if(s) queue_update(s);}
static void style_changed(GtkWidget *widget,gpointer data) {(void)widget;queue_update(data);}
static void allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)widget;SideTabs *s=data;gint old=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(s->wrapper),"insight-side-tabs-width"));if(old!=allocation->width) {g_object_set_data(G_OBJECT(s->wrapper),"insight-side-tabs-width",GINT_TO_POINTER(allocation->width));queue_update(s);}
}
static void switched(GtkNotebook *book,GtkWidget *page,guint index,gpointer data) {(void)book;(void)page;(void)index;insight_side_tabs_refresh(data);}
static void pages_changed(GtkNotebook *book,GtkWidget *page,guint index,gpointer data) {(void)book;(void)page;(void)index;SideTabs *s=g_object_get_data(G_OBJECT(data),"insight-side-tabs");s->rebuild=TRUE;queue_update(s);}
static void destroyed(GtkWidget *widget,gpointer data) {(void)widget;SideTabs *s=data;s->alive=FALSE;if(s->idle) {g_source_remove(s->idle);s->idle=0;}}
static void state_free(gpointer data) {SideTabs *s=data;g_ptr_array_unref(s->buttons);g_object_unref(s->css);g_free(s);}
GtkWidget *insight_side_tabs_wrap(GtkNotebook *book,const gchar *(*translate)(const gchar *)) {
 GtkWidget *parent=gtk_widget_get_parent(GTK_WIDGET(book));if(!GTK_IS_BIN(parent)) return NULL;
 SideTabs *s=g_new0(SideTabs,1);s->alive=s->rebuild=TRUE;s->book=book;s->translate=translate;s->buttons=g_ptr_array_new();
 s->wrapper=gtk_box_new(GTK_ORIENTATION_VERTICAL,0);gtk_widget_set_name(s->wrapper,"insight-side-tabs-wrapper");g_object_set_data_full(G_OBJECT(s->wrapper),"insight-side-tabs",s,state_free);
 s->row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_widget_set_name(s->row,"insight-side-tabs-row");gtk_style_context_add_class(gtk_widget_get_style_context(s->row),"insight-side-tabs");g_object_set_data(G_OBJECT(s->row),"insight-no-i18n",GINT_TO_POINTER(1));
 s->tabs=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);gtk_box_set_homogeneous(GTK_BOX(s->tabs),TRUE);
 s->previous=gtk_button_new_from_icon_name("go-previous-symbolic",GTK_ICON_SIZE_MENU);s->next=gtk_button_new_from_icon_name("go-next-symbolic",GTK_ICON_SIZE_MENU);
 gtk_widget_set_name(s->previous,"insight-side-tabs-previous");gtk_widget_set_name(s->next,"insight-side-tabs-next");GtkWidget *arrows[]={s->previous,s->next};
 for(guint n=0;n<2;n++) {gtk_widget_set_no_show_all(arrows[n],TRUE);gtk_button_set_always_show_image(GTK_BUTTON(arrows[n]),TRUE);gtk_style_context_add_class(gtk_widget_get_style_context(arrows[n]),"insight-side-tab-arrow");gtk_image_set_pixel_size(GTK_IMAGE(gtk_button_get_image(GTK_BUTTON(arrows[n]))),10);gtk_button_set_focus_on_click(GTK_BUTTON(arrows[n]),FALSE);}
 gtk_box_pack_start(GTK_BOX(s->row),s->previous,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(s->row),s->tabs,TRUE,TRUE,0);gtk_box_pack_end(GTK_BOX(s->row),s->next,FALSE,FALSE,0);
 gtk_box_pack_start(GTK_BOX(s->wrapper),s->row,FALSE,FALSE,0);
 g_object_ref(book);gtk_container_remove(GTK_CONTAINER(parent),GTK_WIDGET(book));gtk_box_pack_start(GTK_BOX(s->wrapper),GTK_WIDGET(book),TRUE,TRUE,0);gtk_container_add(GTK_CONTAINER(parent),s->wrapper);g_object_unref(book);gtk_notebook_set_show_tabs(book,FALSE);
 s->css=gtk_css_provider_new();gtk_css_provider_load_from_data(s->css,".insight-side-tabs button { min-width: 0; min-height: 0; padding: 1px 4px; margin: 0; } .insight-side-tabs button.insight-side-tab-arrow { padding: 0; }",-1,NULL);for(guint n=0;n<2;n++) gtk_style_context_add_provider(gtk_widget_get_style_context(arrows[n]),GTK_STYLE_PROVIDER(s->css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION+1);
 GtkGesture *drag=gtk_gesture_drag_new(s->row);gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag),GTK_PHASE_CAPTURE);g_signal_connect(drag,"drag-begin",G_CALLBACK(drag_begin),s);g_signal_connect(drag,"drag-update",G_CALLBACK(drag_update),s);g_object_set_data_full(G_OBJECT(s->row),"insight-side-tabs-drag",drag,g_object_unref);
 g_signal_connect(s->previous,"clicked",G_CALLBACK(previous_clicked),s);g_signal_connect(s->next,"clicked",G_CALLBACK(next_clicked),s);g_signal_connect(s->row,"key-press-event",G_CALLBACK(key_press),s);
 g_signal_connect(s->row,"style-updated",G_CALLBACK(style_changed),s);g_signal_connect(s->wrapper,"size-allocate",G_CALLBACK(allocated),s);g_signal_connect(s->wrapper,"destroy",G_CALLBACK(destroyed),s);
 g_signal_connect_object(book,"switch-page",G_CALLBACK(switched),s->wrapper,0);g_signal_connect_object(book,"page-added",G_CALLBACK(pages_changed),s->wrapper,0);g_signal_connect_object(book,"page-removed",G_CALLBACK(pages_changed),s->wrapper,0);g_signal_connect_object(book,"page-reordered",G_CALLBACK(pages_changed),s->wrapper,0);
 gtk_widget_show(s->tabs);gtk_widget_show(s->row);gtk_widget_show(s->wrapper);queue_update(s);return s->wrapper;
}
