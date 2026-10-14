#define INSIGHT_LOCALIZE_FORMATS
#include "insight-i18n.h"
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
#include "navqueue.h"
#include "tagmanager/tm_tag.h"
#include "../insight/desktop/source_mask.h"
#include "../insight/core/structure_select.h"
#include <gio/gio.h>
#include <string.h>
typedef struct { gsize begin,end; gint first,last; gchar *name; } FunctionRange;
typedef struct {
 guint id,timer; GtkWidget *pane,*host,*shade; ScintillaObject *map;
 gboolean dirty,style_dirty,dragging,positioned; GeanyFiletype *filetype; gint last_top,last_line,last_scale,drag_offset; gdouble drag_y;
 guint64 functions_generation; gboolean functions_dirty; GCancellable *functions_cancel; GPtrArray *functions;
} Overview;
static GList *overviews;
static gboolean enabled=TRUE,show_selection=TRUE,show_viewport=TRUE;
static gboolean show_function=TRUE,show_all_functions=FALSE;
static gboolean show_bookmarks=TRUE,show_changes=TRUE,show_saved=TRUE,show_tips=TRUE;
#define MODIFIED_MARKS ((1u<<SC_MARKNUM_HISTORY_MODIFIED)|(1u<<SC_MARKNUM_HISTORY_REVERTED_TO_MODIFIED))
#define SAVED_MARKS (1u<<SC_MARKNUM_HISTORY_SAVED)
static gint scale=10,width=100;
static GeanyDocument *owner(Overview *o) { return document_find_by_id(o->id); }
static gint first_document_line(ScintillaObject *sci) { return (gint)SSM(sci,SCI_DOCLINEFROMVISIBLE,SSM(sci,SCI_GETFIRSTVISIBLELINE,0,0),0); }
static gint last_document_line(ScintillaObject *sci) { gint first=(gint)SSM(sci,SCI_GETFIRSTVISIBLELINE,0,0),last=(gint)SSM(sci,SCI_VISIBLEFROMDOCLINE,MAX(0,sci_get_line_count(sci)-1),0); return (gint)SSM(sci,SCI_DOCLINEFROMVISIBLE,MIN(last,first+MAX(1,SSM(sci,SCI_LINESONSCREEN,0,0))-1),0); }
static void queue_sync(Overview *);
static void function_range_free(gpointer data) { FunctionRange *range=data; g_free(range->name); g_free(range); }
static void functions_invalidate(Overview *o) {
 o->functions_generation++; if(o->functions_cancel) g_cancellable_cancel(o->functions_cancel); g_clear_object(&o->functions_cancel); g_clear_pointer(&o->functions,g_ptr_array_unref);
}
void insight_overview_tags_changed(GeanyDocument *doc) {
 if(!doc) return;
 for(GList *p=overviews;p;p=p->next) { Overview *o=p->data; if(o->id==doc->id) { functions_invalidate(o); o->functions_dirty=TRUE; queue_sync(o); } }
}
typedef struct { GWeakRef pane; guint id; guint64 generation; gchar *text; InsightCodeDialect dialect; GPtrArray *ranges; } FunctionJob;
static void function_job_free(gpointer data) { FunctionJob *j=data; g_weak_ref_clear(&j->pane); g_free(j->text); g_ptr_array_unref(j->ranges); g_free(j); }
static void functions_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; FunctionJob *j=data; gsize length=strlen(j->text); gchar *mask=insight_source_code_mask(j->text,length,j->dialect,NULL);
 if(mask) for(guint i=0;i<j->ranges->len && !g_cancellable_is_cancelled(cancel);i++) {
  FunctionRange *r=g_ptr_array_index(j->ranges,i); size_t finish=0;
  if(insight_function_extent(mask,length,r->begin,&finish)==1) { r->end=finish; r->last=r->first; for(gsize n=r->begin;n+1<finish;n++) if(j->text[n]=='\n') r->last++; }
 }
 g_free(mask); g_task_return_boolean(task,!g_cancellable_is_cancelled(cancel));
}
static void functions_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); FunctionJob *j=g_task_get_task_data(task); gboolean ok=g_task_propagate_boolean(task,NULL);
 GtkWidget *pane=g_weak_ref_get(&j->pane); if(!pane) return; Overview *o=g_object_get_data(G_OBJECT(pane),"insight-overview");
 if(o && documents_array && o->id==j->id && owner(o) && o->functions_generation==j->generation) {
  g_clear_object(&o->functions_cancel); if(ok) { o->functions=g_ptr_array_ref(j->ranges); gtk_widget_queue_draw(o->shade); }
 }
 g_object_unref(pane);
}
static void functions_start(Overview *o,GeanyDocument *doc) {
 if(!o->functions_dirty || o->functions_cancel) return;
 o->functions_dirty=FALSE;
 if(!show_function && !show_all_functions && !show_tips) { o->functions_dirty=TRUE; return; }
 const char *name=doc->file_type ? doc->file_type->name : ""; InsightCodeDialect dialect;
 if(!strcmp(name,"JavaScript") || !strcmp(name,"TypeScript")) dialect=INSIGHT_CODE_JS;
 else if(!strcmp(name,"C") || !strcmp(name,"C++") || !strcmp(name,"C#") || !strcmp(name,"Java")) dialect=INSIGHT_CODE_C;
 else return;
 if(!doc->tm_file || !doc->tm_file->tags_array) return;
 ScintillaObject *sci=doc->editor->sci; gint length=sci_get_length(sci); if(length<0 || length>2*1024*1024) return;
 FunctionJob *j=g_new0(FunctionJob,1); g_weak_ref_init(&j->pane,o->pane); j->id=o->id; j->generation=o->functions_generation; j->dialect=dialect; j->ranges=g_ptr_array_new_with_free_func(function_range_free); j->text=sci_get_contents(sci,length+1);
 gint lines=sci_get_line_count(sci);
 for(guint i=0;i<doc->tm_file->tags_array->len;i++) {
  TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i); if(!tag->line || tag->line>(gulong)lines || !(tag->type&(tm_tag_function_t|tm_tag_method_t))) continue;
  FunctionRange *r=g_new0(FunctionRange,1); r->first=(gint)tag->line-1; r->begin=(gsize)sci_get_position_from_line(sci,r->first); r->name=tag->scope && *tag->scope ? g_strconcat(tag->scope,"::",tag->name,NULL) : g_strdup(tag->name); g_ptr_array_add(j->ranges,r);
 }
 o->functions_cancel=g_cancellable_new(); GTask *task=g_task_new(NULL,o->functions_cancel,functions_done,NULL); g_task_set_task_data(task,j,function_job_free); g_task_run_in_thread(task,functions_run); g_object_unref(task);
}
static FunctionRange *function_at(Overview *o,gsize begin,gsize end) {
 FunctionRange *best=NULL; if(!o->functions) return NULL;
 for(guint i=0;i<o->functions->len;i++) { FunctionRange *r=g_ptr_array_index(o->functions,i); if(r->end && r->begin<=begin && begin<r->end && end<=r->end && (!best || r->end-r->begin<best->end-best->begin)) best=r; }
 return best;
}
static gboolean shade_draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Overview *o=data; GeanyDocument *doc=owner(o); if(!doc || !enabled) return FALSE; ScintillaObject *source=doc->editor->sci;
 gint length=sci_get_length(o->map); if(length<0 || length>2*1024*1024) return FALSE;
 gint first=first_document_line(source),last=last_document_line(source),map_first=first_document_line(o->map);
 gint row_height=MAX(1,SSM(o->map,SCI_TEXTHEIGHT,0,0)),w=gtk_widget_get_allocated_width(widget);
 if(show_viewport) { gdouble top=(first-map_first)*row_height,bottom=(last+1-map_first)*row_height; cairo_set_source_rgba(cr,0.2,0.45,0.9,0.18); cairo_rectangle(cr,0,top,w,MAX(row_height,bottom-top)); cairo_fill(cr); cairo_set_source_rgba(cr,0.15,0.3,0.65,0.85); cairo_set_line_width(cr,1); cairo_rectangle(cr,0.5,top+0.5,MAX(0,w-1),MAX(1,bottom-top-1)); cairo_stroke(cr); }
 if(show_selection) { gint begin=(gint)SSM(source,SCI_GETSELECTIONSTART,0,0),end=(gint)SSM(source,SCI_GETSELECTIONEND,0,0); if(end>begin && end<=length) { gint a=sci_get_line_from_position(o->map,begin),b=sci_get_line_from_position(o->map,end-1); gdouble top=(a-map_first)*row_height; cairo_set_source_rgba(cr,1,0.85,0,0.32); cairo_rectangle(cr,0,top,w,MAX(row_height,(b-a+1)*row_height)); cairo_fill(cr); } }
 if(o->functions && (show_function || show_all_functions)) {
  FunctionRange *current=function_at(o,(gsize)sci_get_selection_start(source),(gsize)sci_get_selection_end(source));
  gint map_last=map_first+gtk_widget_get_allocated_height(widget)/row_height+1;
  for(guint i=0;i<o->functions->len;i++) {
   FunctionRange *r=g_ptr_array_index(o->functions,i); if(!r->end || r->last<map_first || r->first>map_last || (!show_all_functions && (!show_function || r!=current))) continue;
   gboolean active=show_function && r==current; cairo_set_source_rgba(cr,active ? 0.55 : 0.4,active ? 0.2 : 0.45,active ? 0.85 : 0.5,active ? 0.95 : 0.5); cairo_set_line_width(cr,active ? 2 : 1);
   cairo_rectangle(cr,10.5,(r->first-map_first)*row_height+0.5,MAX(1,w-19),MAX(1,(r->last-r->first+1)*row_height-1)); cairo_stroke(cr);
  }
 }
 /* Query only visible rows. Marker positions follow edits, undo and redo in
  * the source document; the read-only miniature never owns editor markers. */
 gint map_last=MIN(sci_get_line_count(source)-1,map_first+gtk_widget_get_allocated_height(widget)/row_height+1);
 for(gint line=map_first;line<=map_last;line++) {
  guint marks=(guint)SSM(source,SCI_MARKERGET,line,0); gdouble y=(line-map_first)*row_height;
  if(show_changes && ((marks&MODIFIED_MARKS) || (show_saved && (marks&SAVED_MARKS)))) {
   if(marks&MODIFIED_MARKS) cairo_set_source_rgb(cr,0.95,0.58,0.08); else cairo_set_source_rgb(cr,0.2,0.65,0.3);
   cairo_rectangle(cr,MAX(0,w-5),y,5,MAX(2,row_height)); cairo_fill(cr);
  }
  if(show_bookmarks && (marks&2)) { cairo_set_source_rgb(cr,0.12,0.42,0.95); cairo_arc(cr,5,y+row_height/2.0,3,0,2*G_PI); cairo_fill(cr); }
 }
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
 functions_start(o,doc);
 if(o->style_dirty || o->last_scale!=scale) { copy_styles(o,source); o->style_dirty=FALSE; o->last_scale=scale; }
 gint top=first_document_line(source),line=sci_get_current_line(source);
 if(!o->dragging && (top!=o->last_top || line!=o->last_line)) { gint rows=MAX(1,SSM(o->map,SCI_LINESONSCREEN,0,0)),visible=MAX(1,last_document_line(source)-top+1); gint start=MAX(0,top-MAX(0,rows-visible)/2); SSM(o->map,SCI_SETFIRSTVISIBLELINE,start,0); }
 o->last_top=top; o->last_line=line; gtk_widget_queue_draw(o->shade); return G_SOURCE_REMOVE;
}
static void queue_sync(Overview *o) { if(!o->timer) o->timer=g_timeout_add(80,overview_sync,o); }
static void source_notify(ScintillaObject *source,gint message,SCNotification *n,GtkWidget *pane) {
 (void)source; (void)message; Overview *o=g_object_get_data(G_OBJECT(pane),"insight-overview"); if(!o) return;
 if(n->nmhdr.code==SCN_MODIFIED && (n->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) { o->dirty=TRUE; functions_invalidate(o); o->functions_dirty=FALSE; if(o->timer) { g_source_remove(o->timer); o->timer=0; } }
 if(n->nmhdr.code==SCN_MODIFIED && (n->modificationType&SC_MOD_CHANGESTYLE)) o->style_dirty=TRUE;
 if(n->nmhdr.code==SCN_ZOOM) o->last_scale=-1;
 if(n->nmhdr.code==SCN_UPDATEUI || n->nmhdr.code==SCN_MODIFIED || n->nmhdr.code==SCN_PAINTED || n->nmhdr.code==SCN_ZOOM || n->nmhdr.code==SCN_SAVEPOINTREACHED || n->nmhdr.code==SCN_SAVEPOINTLEFT) queue_sync(o);
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
 gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Text scaling %"),FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),percent,FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),gtk_label_new("Overview width"),FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),size,FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),selection,FALSE,FALSE,3); gtk_box_pack_start(GTK_BOX(box),viewport,FALSE,FALSE,3); GtkWidget *bookmarks=gtk_check_button_new_with_label("Show bookmarks"),*changes=gtk_check_button_new_with_label("Show change marks"),*saved=gtk_check_button_new_with_label("Show saved changes"),*tips=gtk_check_button_new_with_label("Enable hover messages");
 GtkWidget *function=gtk_check_button_new_with_label("Show current function boundary"),*all_functions=gtk_check_button_new_with_label("Show all function boundaries");
 GtkWidget *checks[]={bookmarks,changes,saved,tips,function,all_functions}; gboolean values[]={show_bookmarks,show_changes,show_saved,show_tips,show_function,show_all_functions}; const char *names[]={"insight-overview-bookmarks","insight-overview-changes","insight-overview-saved","insight-overview-tips","insight-overview-function","insight-overview-all-functions"};
 for(guint i=0;i<G_N_ELEMENTS(checks);i++) { gtk_widget_set_name(checks[i],names[i]); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checks[i]),values[i]); gtk_box_pack_start(GTK_BOX(box),checks[i],FALSE,FALSE,3); }
 gtk_widget_show_all(dialog);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_APPLY) { scale=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(percent)); width=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(size)); show_selection=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(selection)); show_viewport=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(viewport)); show_bookmarks=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bookmarks)); show_changes=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(changes)); show_saved=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(saved)); show_tips=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(tips)); show_function=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(function)); show_all_functions=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(all_functions)); for(GList *p=overviews;p;p=p->next) { Overview *view=p->data; view->last_scale=-1; gtk_paned_set_position(GTK_PANED(view->pane),MAX(1,gtk_widget_get_allocated_width(view->pane)-width)); queue_sync(view); } }
 gtk_widget_destroy(dialog);
}
static void change_navigation(GtkMenuItem *item,gpointer data) {
 (void)item; GeanyDocument *doc=document_get_current(); if(!doc) return; ScintillaObject *sci=doc->editor->sci;
 if(sci_get_length(sci)>2*1024*1024) { ui_set_statusbar(TRUE,"Change navigation supports files up to 2 MiB"); return; }
 gint direction=GPOINTER_TO_INT(data),count=sci_get_line_count(sci),start=sci_get_current_line(sci);
 /* Skip the current contiguous change block, then find the next block. */
 gint line=start; while(line>=0 && line<count && ((guint)SSM(sci,SCI_MARKERGET,line,0)&MODIFIED_MARKS)) line+=direction;
 if(line==start) line+=direction;
 for(;line>=0 && line<count;line+=direction) if((guint)SSM(sci,SCI_MARKERGET,line,0)&MODIFIED_MARKS) {
  while(line>0 && ((guint)SSM(sci,SCI_MARKERGET,line-1,0)&MODIFIED_MARKS)) line--;
  navqueue_goto_line(doc,doc,line+1); document_grab_focus(doc); return;
 }
 ui_set_statusbar(TRUE,direction>0 ? "No next unsaved change" : "No previous unsaved change");
}
static GtkWidget *change_item(const char *label,const char *name,const char *icon,gint direction) {
 GtkWidget *item=gtk_image_menu_item_new_with_label(label); gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),gtk_image_new_from_icon_name(icon,GTK_ICON_SIZE_MENU)); gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(item),TRUE); gtk_widget_set_name(item,name); g_signal_connect(item,"activate",G_CALLBACK(change_navigation),GINT_TO_POINTER(direction)); return item;
}
void insight_overview_install_commands(void) {
 GtkWidget *edit=ui_lookup_widget(main_widgets.window,"edit1_menu"); GtkAccelGroup *accels=gtk_accel_group_new(); gtk_window_add_accel_group(GTK_WINDOW(main_widgets.window),accels);
 GtkWidget *previous=change_item("Previous unsaved change","insight-previous-change","go-up",-1),*next=change_item("Next unsaved change","insight-next-change","go-down",1);
 gtk_menu_shell_append(GTK_MENU_SHELL(edit),gtk_separator_menu_item_new()); gtk_menu_shell_append(GTK_MENU_SHELL(edit),previous); gtk_menu_shell_append(GTK_MENU_SHELL(edit),next);
 gtk_widget_add_accelerator(previous,"activate",accels,GDK_KEY_KP_Subtract,GDK_MOD1_MASK,GTK_ACCEL_VISIBLE); gtk_widget_add_accelerator(next,"activate",accels,GDK_KEY_KP_Add,GDK_MOD1_MASK,GTK_ACCEL_VISIBLE); g_object_unref(accels); gtk_widget_show_all(edit);
}
static gboolean map_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 Overview *o=data;
 if(event->button==3) { GtkWidget *menu=gtk_menu_new(),*options=gtk_image_menu_item_new_with_label("Overview Options…"); gtk_menu_attach_to_widget(GTK_MENU(menu),widget,NULL); gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(options),gtk_image_new_from_icon_name("preferences-system",GTK_ICON_SIZE_MENU)); gtk_image_menu_item_set_always_show_image(GTK_IMAGE_MENU_ITEM(options),TRUE); gtk_widget_set_name(options,"insight-overview-options"); gtk_menu_shell_append(GTK_MENU_SHELL(menu),options);
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),change_item("Previous unsaved change","insight-overview-previous-change","go-up",-1)); gtk_menu_shell_append(GTK_MENU_SHELL(menu),change_item("Next unsaved change","insight-overview-next-change","go-down",1)); g_signal_connect_object(options,"activate",G_CALLBACK(overview_options),o->pane,0); g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu); gtk_widget_show_all(menu); gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event); return TRUE; }
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
 (void)widget; (void)x; if(keyboard || !show_tips) return FALSE; Overview *o=data; GeanyDocument *doc=owner(o); if(!doc) return FALSE; gint line=MIN(line_at(o,y),MAX(0,sci_get_line_count(doc->editor->sci)-1)); gchar *text=sci_get_line(doc->editor->sci,line);
 if(!text || !g_utf8_validate(text,-1,NULL)) { g_free(text); return FALSE; } gchar *shortened=g_utf8_substring(text,0,MIN(180,g_utf8_strlen(text,-1))),*label=g_strdup_printf("Line %d: %s",line+1,shortened); guint marks=(guint)SSM(doc->editor->sci,SCI_MARKERGET,line,0);
 const char *status=(marks&MODIFIED_MARKS) ? insight_tr("Unsaved change") : (marks&SAVED_MARKS) ? insight_tr("Saved change") : "";
 gchar *detail=g_strdup_printf("%s%s%s%s%s",label,*status ? "\n" : "",status,(marks&2) ? "\n" : "",(marks&2) ? insight_tr("Bookmark") : ""); FunctionRange *function=function_at(o,(gsize)sci_get_position_from_line(doc->editor->sci,line),(gsize)sci_get_position_from_line(doc->editor->sci,line));
 if(function) { gchar *function_label=g_strdup_printf("Function: %s",function->name),*with_function=g_strconcat(detail,"\n",function_label,NULL); g_free(function_label); g_free(detail); detail=with_function; } gtk_tooltip_set_text(tooltip,detail); g_free(detail); g_free(label); g_free(shortened); g_free(text); return TRUE;
}
static void pane_position(GObject *object,GParamSpec *spec,gpointer data) { (void)spec; Overview *o=data; gint total=gtk_widget_get_allocated_width(GTK_WIDGET(object)); if(o->positioned && enabled && gtk_widget_get_mapped(o->host) && total>400) width=CLAMP(total-gtk_paned_get_position(GTK_PANED(object)),40,400); }
static void pane_allocated(GtkWidget *widget,GtkAllocation *allocation,gpointer data) { Overview *o=data; if(!o->positioned && allocation->width>width+100) { gtk_paned_set_position(GTK_PANED(widget),allocation->width-width); o->positioned=TRUE; } }
static void overview_free(gpointer data) { Overview *o=data; if(o->timer) g_source_remove(o->timer); functions_invalidate(o); overviews=g_list_remove(overviews,o); g_free(o); }
static void host_mapped(GtkWidget *widget,gpointer data) { (void)widget; Overview *o=data; o->last_top=-1; queue_sync(o); }
GtkWidget *insight_overview_wrap(GeanyDocument *doc) {
 Overview *o=g_new0(Overview,1); o->id=doc->id; o->dirty=TRUE; o->functions_dirty=TRUE; o->last_top=o->last_line=o->last_scale=-1;
 o->pane=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL); o->host=gtk_overlay_new(); o->map=SCINTILLA(scintilla_new()); o->shade=gtk_drawing_area_new(); gtk_widget_set_name(o->shade,"insight-overview-markers");
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
 const char *keys[]={"overview-bookmarks","overview-changes","overview-saved","overview-tips","overview-function","overview-all-functions"}; gboolean *values[]={&show_bookmarks,&show_changes,&show_saved,&show_tips,&show_function,&show_all_functions};
 for(guint i=0;i<G_N_ELEMENTS(keys);i++) if(g_key_file_has_key(key,"workbench",keys[i],NULL)) *values[i]=g_key_file_get_boolean(key,"workbench",keys[i],NULL);
 for(GList *p=overviews;p;p=p->next) { Overview *o=p->data; gtk_widget_set_size_request(o->host,40,-1); o->last_scale=-1; }
 insight_overview_set_visible(enabled);
}
void insight_overview_write(GKeyFile *key) { g_key_file_set_boolean(key,"workbench","overview-visible",enabled); g_key_file_set_integer(key,"workbench","overview-scale",scale); g_key_file_set_integer(key,"workbench","overview-width",width); g_key_file_set_boolean(key,"workbench","overview-selection",show_selection); g_key_file_set_boolean(key,"workbench","overview-viewport",show_viewport); g_key_file_set_boolean(key,"workbench","overview-bookmarks",show_bookmarks); g_key_file_set_boolean(key,"workbench","overview-changes",show_changes); g_key_file_set_boolean(key,"workbench","overview-saved",show_saved); g_key_file_set_boolean(key,"workbench","overview-tips",show_tips); g_key_file_set_boolean(key,"workbench","overview-function",show_function); g_key_file_set_boolean(key,"workbench","overview-all-functions",show_all_functions); }
