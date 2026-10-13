/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-overview.h"
#include "editor.h"
#include "highlighting.h"
#include "filetypes.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <string.h>
typedef struct {
 guint id,timer; GtkWidget *pane,*host,*shade; ScintillaObject *map;
 gboolean dirty,style_dirty,dragging,positioned; GeanyFiletype *filetype; gint last_top,last_line,last_scale,drag_offset; gdouble drag_y;
} Overview;
static GList *overviews;
static gboolean enabled=TRUE,show_selection=TRUE,show_viewport=TRUE;
static gint scale=10,width=100;
static GeanyDocument *owner(Overview *o) { return document_find_by_id(o->id); }
static gint first_document_line(ScintillaObject *sci) { return (gint)SSM(sci,SCI_DOCLINEFROMVISIBLE,SSM(sci,SCI_GETFIRSTVISIBLELINE,0,0),0); }
static gint last_document_line(ScintillaObject *sci) { gint first=(gint)SSM(sci,SCI_GETFIRSTVISIBLELINE,0,0),last=(gint)SSM(sci,SCI_VISIBLEFROMDOCLINE,MAX(0,sci_get_line_count(sci)-1),0); return (gint)SSM(sci,SCI_DOCLINEFROMVISIBLE,MIN(last,first+MAX(1,SSM(sci,SCI_LINESONSCREEN,0,0))-1),0); }
static gboolean shade_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Overview *o=data; GeanyDocument *doc=owner(o); if(!doc || !enabled) return FALSE; ScintillaObject *source=doc->editor->sci;
 gint length=sci_get_length(o->map); if(length<0 || length>2*1024*1024) return FALSE;
 gint first=first_document_line(source),last=last_document_line(source),map_first=first_document_line(o->map);
 gint row_height=MAX(1,SSM(o->map,SCI_TEXTHEIGHT,0,0)),w=gtk_widget_get_allocated_width(widget);
 if(show_viewport) { gdouble top=(first-map_first)*row_height,bottom=(last+1-map_first)*row_height; cairo_set_source_rgba(cr,0.2,0.45,0.9,0.18); cairo_rectangle(cr,0,top,w,MAX(row_height,bottom-top)); cairo_fill(cr); cairo_set_source_rgba(cr,0.15,0.3,0.65,0.85); cairo_set_line_width(cr,1); cairo_rectangle(cr,0.5,top+0.5,MAX(0,w-1),MAX(1,bottom-top-1)); cairo_stroke(cr); }
 if(show_selection) { gint begin=(gint)SSM(source,SCI_GETSELECTIONSTART,0,0),end=(gint)SSM(source,SCI_GETSELECTIONEND,0,0); if(end>begin && end<=length) { gint a=sci_get_line_from_position(o->map,begin),b=sci_get_line_from_position(o->map,end-1); gdouble top=(a-map_first)*row_height; cairo_set_source_rgba(cr,1,0.85,0,0.32); cairo_rectangle(cr,0,top,w,MAX(row_height,(b-a+1)*row_height)); cairo_fill(cr); } }
 return FALSE;
}
static void copy_styles(Overview *o,ScintillaObject *source) {
 for(gint style=0;style<=STYLE_MAX;style++) {
  SSM(o->map,SCI_STYLESETFORE,style,SSM(source,SCI_STYLEGETFORE,style,0)); SSM(o->map,SCI_STYLESETBACK,style,SSM(source,SCI_STYLEGETBACK,style,0));
  SSM(o->map,SCI_STYLESETBOLD,style,SSM(source,SCI_STYLEGETBOLD,style,0)); SSM(o->map,SCI_STYLESETITALIC,style,SSM(source,SCI_STYLEGETITALIC,style,0));
  SSM(o->map,SCI_STYLESETSIZEFRACTIONAL,style,MAX(100,SSM(source,SCI_STYLEGETSIZEFRACTIONAL,style,0)*scale/100));
  gint size=(gint)SSM(source,SCI_STYLEGETFONT,style,0); if(size>=0 && size<1024) { gchar *font=g_malloc0((gsize)size+1); SSM(source,SCI_STYLEGETFONT,style,(sptr_t)font); SSM(o->map,SCI_STYLESETFONT,style,(sptr_t)font); g_free(font); }
 }
 SSM(o->map,SCI_SETTABWIDTH,SSM(source,SCI_GETTABWIDTH,0,0),0);
}
static gboolean overview_sync(gpointer data) {
 Overview *o=data; o->timer=0; GeanyDocument *doc=owner(o); if(!doc || !enabled || !gtk_widget_get_mapped(o->host)) return G_SOURCE_REMOVE;
 ScintillaObject *source=doc->editor->sci; gint length=sci_get_length(source);
 if(length<0 || length>2*1024*1024) { gtk_widget_set_tooltip_text(o->host,"Overview supports files up to 2 MiB"); sci_set_readonly(o->map,FALSE); sci_set_text(o->map,""); sci_set_readonly(o->map,TRUE); return G_SOURCE_REMOVE; }
 if(o->dirty || o->filetype!=doc->file_type) {
  gint old_top=first_document_line(o->map); gchar *text=sci_get_contents(source,length+1);
  /* Independent text and lexer: setting read-only on a shared Scintilla
   * document would also make the user's main editor read-only. */
  if(o->filetype!=doc->file_type) { o->filetype=doc->file_type; highlighting_set_styles(o->map,doc->file_type ? doc->file_type : filetypes[GEANY_FILETYPES_NONE]); }
  sci_set_readonly(o->map,FALSE); SSM(o->map,SCI_SETUNDOCOLLECTION,FALSE,0); sci_set_text(o->map,text); SSM(o->map,SCI_EMPTYUNDOBUFFER,0,0); sci_set_readonly(o->map,TRUE);
  SSM(o->map,SCI_SETFIRSTVISIBLELINE,old_top,0); g_free(text); o->dirty=FALSE; o->style_dirty=TRUE;
 }
 if(o->style_dirty || o->last_scale!=scale) { copy_styles(o,source); o->style_dirty=FALSE; o->last_scale=scale; }
 gint top=first_document_line(source),line=sci_get_current_line(source);
 if(!o->dragging && (top!=o->last_top || line!=o->last_line)) { gint rows=MAX(1,SSM(o->map,SCI_LINESONSCREEN,0,0)),visible=MAX(1,last_document_line(source)-top+1); gint start=MAX(0,top-MAX(0,rows-visible)/2); SSM(o->map,SCI_SETFIRSTVISIBLELINE,start,0); }
 o->last_top=top; o->last_line=line; gtk_widget_queue_draw(o->shade); return G_SOURCE_REMOVE;
}
static void queue_sync(Overview *o) { if(!o->timer) o->timer=g_timeout_add(80,overview_sync,o); }
static void source_notify(ScintillaObject *source,gint message,SCNotification *n,GtkWidget *pane) {
 (void)source; (void)message; Overview *o=g_object_get_data(G_OBJECT(pane),"insight-overview"); if(!o) return;
 if(n->nmhdr.code==SCN_MODIFIED && (n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) { o->dirty=TRUE; if(o->timer) { g_source_remove(o->timer); o->timer=0; } }
 if(n->nmhdr.code==SCN_MODIFIED && (n->modificationType&SC_MOD_CHANGESTYLE)) o->style_dirty=TRUE;
 if(n->nmhdr.code==SCN_ZOOM) o->last_scale=-1;
 if(n->nmhdr.code==SCN_UPDATEUI || n->nmhdr.code==SCN_MODIFIED || n->nmhdr.code==SCN_PAINTED || n->nmhdr.code==SCN_ZOOM) queue_sync(o);
}
static gint line_at(Overview *o,gdouble y) { return MAX(0,first_document_line(o->map)+(gint)(MAX(0,y)/MAX(1,SSM(o->map,SCI_TEXTHEIGHT,0,0)))); }
static void drag_scroll(Overview *o,gdouble y) {
 GeanyDocument *doc=owner(o); if(!doc) return;
 gint line=CLAMP(line_at(o,y)-o->drag_offset,0,MAX(0,sci_get_line_count(doc->editor->sci)-1));
 SSM(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,SSM(doc->editor->sci,SCI_VISIBLEFROMDOCLINE,line,0),0); gtk_widget_queue_draw(o->shade);
}
static void overview_options(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; GtkWidget *dialog=gtk_dialog_new_with_buttons("Overview Options",GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,NULL),*box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
 GtkWidget *percent=gtk_spin_button_new_with_range(5,100,5),*size=gtk_spin_button_new_with_range(40,400,10),*selection=gtk_check_button_new_with_label("Show active selection"),*viewport=gtk_check_button_new_with_label("Show visible area");
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(percent),scale); gtk_spin_button_set_value(GTK_SPIN_BUTTON(size),width); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(selection),show_selection); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(viewport),show_viewport);
 gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Text scaling %"),FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),percent,FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Overview width"),FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),size,FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),selection,FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),viewport,FALSE,FALSE,3); gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY) { scale=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(percent)); width=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(size)); show_selection=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(selection)); show_viewport=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(viewport)); for(GList *p=overviews;p;p=p->next) { Overview *view=p->data; view->last_scale=-1; gtk_paned_set_position(GTK_PANED(view->pane),MAX(1,gtk_widget_get_allocated_width(view->pane)-width)); queue_sync(view); } }
 gtk_widget_destroy(dialog);
}
static gboolean map_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 Overview *o=data;
 if(event->button==3) { GtkWidget *menu=gtk_menu_new(),*options=gtk_menu_item_new_with_label("Overview Options…"); gtk_container_add(GTK_CONTAINER(menu),options); g_signal_connect_object(options,"activate",G_CALLBACK(overview_options),o->pane,0); g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu); gtk_widget_show_all(menu); gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event); return TRUE; }
 if(event->button==1) { GeanyDocument *doc=owner(o); if(doc) { gint first=first_document_line(doc->editor->sci),last=last_document_line(doc->editor->sci),line=line_at(o,event->y); o->drag_offset=line>=first && line<=last ? line-first : MAX(0,(last-first)/2); o->dragging=TRUE; o->drag_y=event->y; gtk_grab_add(widget); drag_scroll(o,event->y); } }
 return TRUE;
}
static gboolean map_release(GtkWidget *widget,GdkEventButton *event,gpointer data) { Overview *o=data; if(event->button==1 && o->dragging) { o->dragging=FALSE; gtk_grab_remove(widget); queue_sync(o); } return TRUE; }
static gboolean map_motion(GtkWidget *widget,GdkEventMotion *event,gpointer data) { (void)widget; Overview *o=data; if(o->dragging) { o->drag_y=event->y; gint h=gtk_widget_get_allocated_height(GTK_WIDGET(o->map)); if(event->y<12 || event->y>h-12) SSM(o->map,SCI_LINESCROLL,0,event->y<12 ? -8 : 8); drag_scroll(o,event->y); } return TRUE; }
static gboolean map_scroll(GtkWidget *widget,GdkEventScroll *event,gpointer data) {
 (void)widget; Overview *o=data; gdouble dx=0,dy=0;
 if(!gdk_event_get_scroll_deltas((GdkEvent *)event,&dx,&dy)) dy=event->direction==GDK_SCROLL_UP || event->direction==GDK_SCROLL_LEFT ? -1 : 1;
 if(event->state&GDK_CONTROL_MASK) { scale=CLAMP(scale+(dy<0 ? 5 : -5),5,100); o->last_scale=-1; queue_sync(o); }
 else { SSM(o->map,SCI_LINESCROLL,0,(sptr_t)(dy*(event->state&GDK_SHIFT_MASK ? 100 : 10))); gtk_widget_queue_draw(o->shade); }
 return TRUE;
}
static gboolean map_tooltip(GtkWidget *widget,gint x,gint y,gboolean keyboard,GtkTooltip *tooltip,gpointer data) {
 (void)widget; (void)x; if(keyboard) return FALSE; Overview *o=data; GeanyDocument *doc=owner(o); if(!doc) return FALSE; gint line=MIN(line_at(o,y),MAX(0,sci_get_line_count(doc->editor->sci)-1)); gchar *text=sci_get_line(doc->editor->sci,line);
 if(!text || !g_utf8_validate(text,-1,NULL)) { g_free(text); return FALSE; } gchar *shortened=g_utf8_substring(text,0,MIN(180,g_utf8_strlen(text,-1))),*label=g_strdup_printf("Line %d: %s",line+1,shortened); gtk_tooltip_set_text(tooltip,label); g_free(label); g_free(shortened); g_free(text); return TRUE;
}
static void pane_position(GObject *object,GParamSpec *spec,gpointer data) { (void)spec; Overview *o=data; gint total=gtk_widget_get_allocated_width(GTK_WIDGET(object)); if(o->positioned && enabled && gtk_widget_get_mapped(o->host) && total>400) width=CLAMP(total-gtk_paned_get_position(GTK_PANED(object)),40,400); }
static void pane_allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) { Overview *o=data; if(!o->positioned && allocation->width>width+100) { gtk_paned_set_position(GTK_PANED(widget),allocation->width-width); o->positioned=TRUE; } }
static void overview_free(gpointer data) { Overview *o=data; if(o->timer) g_source_remove(o->timer); overviews=g_list_remove(overviews,o); g_free(o); }
static void host_mapped(GtkWidget *widget,gpointer data) { (void)widget; Overview *o=data; o->last_top=-1; queue_sync(o); }
GtkWidget *insight_overview_wrap(GeanyDocument *doc) {
 Overview *o=g_new0(Overview,1); o->id=doc->id; o->dirty=TRUE; o->last_top=o->last_line=o->last_scale=-1;
 o->pane=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); o->host=gtk_overlay_new(); o->map=SCINTILLA(scintilla_new()); o->shade=gtk_drawing_area_new();
 gtk_widget_set_direction(GTK_WIDGET(o->map),GTK_TEXT_DIR_LTR); gtk_widget_set_can_focus(GTK_WIDGET(o->map),FALSE); sci_set_codepage(o->map,SC_CP_UTF8); sci_use_popup(o->map,FALSE); sci_set_readonly(o->map,TRUE); SSM(o->map,SCI_SETHSCROLLBAR,FALSE,0); SSM(o->map,SCI_SETVSCROLLBAR,FALSE,0); SSM(o->map,SCI_SETWRAPMODE,SC_WRAP_NONE,0); SSM(o->map,SCI_SETCARETWIDTH,0,0); SSM(o->map,SCI_SETCURSOR,SC_CURSORARROW,0);
 for(guint i=0;i<5;i++) SSM(o->map,SCI_SETMARGINWIDTHN,i,0);
 gtk_drag_dest_unset(GTK_WIDGET(o->map)); gtk_container_add(GTK_CONTAINER(o->host),GTK_WIDGET(o->map)); gtk_overlay_add_overlay(GTK_OVERLAY(o->host),o->shade); gtk_overlay_set_overlay_pass_through(GTK_OVERLAY(o->host),o->shade,TRUE); gtk_widget_set_size_request(o->host,40,-1);
 gtk_paned_pack1(GTK_PANED(o->pane),GTK_WIDGET(doc->editor->sci),TRUE,FALSE); gtk_paned_pack2(GTK_PANED(o->pane),o->host,FALSE,FALSE); gtk_widget_set_no_show_all(o->host,TRUE);
 g_object_set_data_full(G_OBJECT(o->pane),"insight-overview",o,overview_free); g_signal_connect_object(doc->editor->sci,"sci-notify",G_CALLBACK(source_notify),o->pane,0); g_signal_connect(o->shade,"draw",G_CALLBACK(shade_draw),o); g_signal_connect(o->host,"map",G_CALLBACK(host_mapped),o); g_signal_connect(o->pane,"notify::position",G_CALLBACK(pane_position),o); g_signal_connect(o->pane,"size-allocate",G_CALLBACK(pane_allocated),o);
 gtk_widget_add_events(GTK_WIDGET(o->map),GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK|GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK); g_signal_connect(o->map,"button-press-event",G_CALLBACK(map_button),o); g_signal_connect(o->map,"button-release-event",G_CALLBACK(map_release),o); g_signal_connect(o->map,"motion-notify-event",G_CALLBACK(map_motion),o); g_signal_connect(o->map,"scroll-event",G_CALLBACK(map_scroll),o); gtk_widget_set_has_tooltip(GTK_WIDGET(o->map),TRUE); g_signal_connect(o->map,"query-tooltip",G_CALLBACK(map_tooltip),o);
 overviews=g_list_prepend(overviews,o); gtk_widget_show(GTK_WIDGET(o->map)); gtk_widget_show(o->shade); gtk_widget_set_visible(o->host,enabled); gtk_widget_show(o->pane); return o->pane;
}
void insight_overview_set_visible(gboolean visible) { enabled=visible; for(GList *p=overviews;p;p=p->next) { Overview *o=p->data; gtk_widget_set_visible(o->host,visible); if(visible) { o->dirty=TRUE; queue_sync(o); } } }
gboolean insight_overview_get_visible(void) { return enabled; }
void insight_overview_read(GKeyFile *key) {
 if(g_key_file_has_key(key,"workbench","overview-visible",NULL)) enabled=g_key_file_get_boolean(key,"workbench","overview-visible",NULL);
 gint value=g_key_file_get_integer(key,"workbench","overview-scale",NULL); if(value>=5 && value<=100) scale=value;
 value=g_key_file_get_integer(key,"workbench","overview-width",NULL); if(value>=40 && value<=400) width=value;
 if(g_key_file_has_key(key,"workbench","overview-selection",NULL)) show_selection=g_key_file_get_boolean(key,"workbench","overview-selection",NULL);
 if(g_key_file_has_key(key,"workbench","overview-viewport",NULL)) show_viewport=g_key_file_get_boolean(key,"workbench","overview-viewport",NULL);
 for(GList *p=overviews;p;p=p->next) { Overview *o=p->data; gtk_widget_set_size_request(o->host,40,-1); o->last_scale=-1; }
 insight_overview_set_visible(enabled);
}
void insight_overview_write(GKeyFile *key) { g_key_file_set_boolean(key,"workbench","overview-visible",enabled); g_key_file_set_integer(key,"workbench","overview-scale",scale); g_key_file_set_integer(key,"workbench","overview-width",width); g_key_file_set_boolean(key,"workbench","overview-selection",show_selection); g_key_file_set_boolean(key,"workbench","overview-viewport",show_viewport); }
