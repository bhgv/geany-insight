/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-source-routes.h"
#include "insight-compare-ui.h"
#include "insight-ai-ui.h"
#include "insight-i18n.h"
#include "insight-project-report.h"
#include "encodingsprivate.h"
#include "../insight/desktop/call_paths.h"
#include "document.h"
#include "editor.h"
#include "sciwrappers.h"
#include "navqueue.h"
#include "project.h"
#include "geanyobject.h"
#include "ui_utils.h"
#include "utils.h"
#include "../insight/core/control_flow.h"
#include "../insight/core/structure_select.h"
#include "../insight/desktop/source_mask.h"
#include "../insight/desktop/glib_compat.h"
#include <pango/pangocairo.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define ROUTE_GROUP "insight-source-routes"
#define ROUTE_LIMIT 256

typedef struct RoutePoint { gchar *path,*text,*label,*digest;guint line,column,lane;gsize offset;InsightFlowKind kind;struct RoutePoint *site;gboolean uncertain; } RoutePoint;
typedef struct { GPtrArray *points;GArray *edges;gchar *digest;guint document; } RouteGraph;
typedef struct {
 GtkWidget *root,*canvas,*scroll,*mode,*zoom,*status,*link,*back,*call_from,*call_to,*call_possible,*call_variants;RouteGraph routes,flow,calls;
 GCancellable *call_cancel;InsightCallPaths *call_result;guint64 call_serial;gchar *endpoint_path[2];guint endpoint_line[2];gboolean closed,setting_endpoint;gint selected,from;gboolean loading;gdouble scale;
} Routes;
typedef struct { gdouble x,y,w,h; } Rect;
static void cancel_calls(Routes *r);
static void point_free(gpointer data) { RoutePoint *p=data;g_free(p->path);g_free(p->text);g_free(p->label);g_free(p->digest);if(p->site) point_free(p->site);g_free(p); }
static void graph_clear(RouteGraph *g) {g_ptr_array_set_size(g->points,0);g_array_set_size(g->edges,0);g_clear_pointer(&g->digest,g_free);g->document=0;}
static RouteGraph *shown(Routes *r) {gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(r->mode));return mode==1 ? &r->flow:mode==2 ? &r->calls:&r->routes;}
static void note(Routes *r,const gchar *text) {insight_i18n_key(G_OBJECT(r->status),"label",text);}
static Rect rect(RouteGraph *g,guint i) {
 RoutePoint *p=g_ptr_array_index(g->points,i);guint row=g->digest ? i==1 ? g->points->len-1:i>1 ? i-1:0:i;
 return (Rect){32+MIN(p->lane,7)*76,24+row*92,280,58};
}
static void update(Routes *r) {
 RouteGraph *g=shown(r);gdouble width=340,height=100;for(guint i=0;i<g->points->len;i++) {Rect a=rect(g,i);width=MAX(width,a.x+a.w+80);height=MAX(height,a.y+a.h+32);}
 gtk_widget_set_size_request(r->canvas,(gint)(width*r->scale),(gint)(height*r->scale));
 g_object_set_data(G_OBJECT(r->root),"insight-route-node-count",GUINT_TO_POINTER(g->points->len));g_object_set_data(G_OBJECT(r->root),"insight-route-edge-count",GUINT_TO_POINTER(g->edges->len));
 gtk_widget_queue_draw(r->canvas);
}
static void save(Routes *r) {if(!r->loading && app->project) project_write_config();}
static gchar *line_text(GeanyDocument *doc,guint line) {
 gint begin=sci_get_position_from_line(doc->editor->sci,line),end=(gint)SEND(doc->editor->sci,SCI_GETLINEENDPOSITION,line,0);if(begin<0 || end<begin || end-begin>4096) return NULL;
 if(begin==end) return g_strdup("");
 gchar *text=sci_get_contents_range(doc->editor->sci,begin,end);if(memchr(text,0,(gsize)(end-begin)) || !g_utf8_validate(text,end-begin,NULL)) {g_free(text);return NULL;}return text;
}
static RoutePoint *capture(GeanyDocument *doc,gsize position,InsightFlowKind kind,guint lane) {
 RoutePoint *p=g_new0(RoutePoint,1);p->path=g_strdup(doc->file_name);p->line=(guint)sci_get_line_from_position(doc->editor->sci,(gint)position);p->column=(guint)position-(guint)sci_get_position_from_line(doc->editor->sci,(gint)p->line);p->offset=position;p->kind=kind;p->lane=lane;p->text=line_text(doc,p->line);
 if(!p->text) {point_free(p);return NULL;}return p;
}
static void navigate_point(Routes *r,RouteGraph *g,RoutePoint *p) {
 GeanyDocument *old=document_get_current(),*doc=g->digest ? document_find_by_id(g->document):document_find_by_filename(p->path);
 if(!DOC_VALID(doc) && !g->digest) {gchar *native=utils_get_locale_from_utf8(p->path);doc=document_open_file(native,FALSE,NULL,NULL);g_free(native);}
 if(!DOC_VALID(doc)) {note(r,"Source is closed or unavailable; rebuild the graph");return;}
 if(g->digest) {
  gint length=sci_get_length(doc->editor->sci);if(length>2*1024*1024) {note(r,"Source changed; rebuild the flow graph");return;}
  gchar *text=sci_get_contents(doc->editor->sci,length+1),*digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,length);gboolean equal=!strcmp(digest,g->digest);g_free(text);g_free(digest);if(!equal) {note(r,"Source changed; rebuild the flow graph");return;}
 } else if(p->digest) {
  gint length=sci_get_length(doc->editor->sci);if(length<0 || length>2*1024*1024) {note(r,"Source changed; search the call paths again");return;}
  gchar *text=sci_get_contents(doc->editor->sci,length+1),*digest=insight_call_digest(text);gboolean equal=!strcmp(digest,p->digest);g_free(text);g_free(digest);if(!equal || p->line>=(guint)sci_get_line_count(doc->editor->sci)) {note(r,"Source changed; search the call paths again");return;}
  gint begin=sci_get_position_from_line(doc->editor->sci,p->line),end=(gint)SEND(doc->editor->sci,SCI_GETLINEENDPOSITION,p->line,0);p->offset=MIN((gsize)end,(gsize)begin+p->column);
 } else {
  guint found=0,line=p->line;gchar *actual=p->line<(guint)sci_get_line_count(doc->editor->sci) ? line_text(doc,p->line):NULL;gboolean same=!g_strcmp0(actual,p->text);g_free(actual);
  if(!same) {
   if(sci_get_length(doc->editor->sci)>2*1024*1024 || !*p->text) {note(r,"Source location changed or is ambiguous; capture it again");return;}
   for(gint i=0;i<sci_get_line_count(doc->editor->sci);i++) {actual=line_text(doc,(guint)i);if(actual && !strcmp(actual,p->text)) {line=(guint)i;found++;}g_free(actual);if(found>1) break;}
   if(found!=1) {note(r,"Source location changed or is ambiguous; capture it again");return;}p->line=line;
  }
  gint begin=sci_get_position_from_line(doc->editor->sci,p->line),end=(gint)SEND(doc->editor->sci,SCI_GETLINEENDPOSITION,p->line,0);p->offset=MIN((gsize)end,(gsize)begin+p->column);
  if((gsize)SEND(doc->editor->sci,SCI_POSITIONBEFORE,SEND(doc->editor->sci,SCI_POSITIONAFTER,p->offset,0),0)!=p->offset && p->offset<(gsize)end) {note(r,"Source location changed or is ambiguous; capture it again");return;}
 }
 navqueue_goto_line(old,doc,(gint)p->line+1);editor_goto_pos(doc->editor,(gint)p->offset,TRUE);document_grab_focus(doc);
}
static void navigate(Routes *r,guint index) {RouteGraph *g=shown(r);if(index<g->points->len) navigate_point(r,g,g_ptr_array_index(g->points,index));}
static void edge_add(Routes *r,guint from,guint to) {
 if(from==to) return;for(guint i=0;i<r->routes.edges->len;i++) {InsightFlowEdge e=g_array_index(r->routes.edges,InsightFlowEdge,i);if(e.from==from && e.to==to) return;}
 if(r->routes.edges->len>=INSIGHT_FLOW_EDGE_LIMIT) {note(r,"Source route graph is full");return;}
 InsightFlowEdge e={from,to,INSIGHT_FLOW_NEXT};g_array_append_val(r->routes.edges,e);save(r);
}
static gboolean draw(GtkWidget *widget,cairo_t *cr,gpointer data) {
 Routes *r=data;RouteGraph *g=shown(r);GtkStyleContext *style=gtk_widget_get_style_context(widget);gtk_render_background(style,cr,0,0,gtk_widget_get_allocated_width(widget),gtk_widget_get_allocated_height(widget));GdkRGBA fg;gtk_style_context_get_color(style,GTK_STATE_FLAG_NORMAL,&fg);cairo_scale(cr,r->scale,r->scale);
 gdouble outside=320;for(guint i=0;i<g->points->len;i++) {Rect a=rect(g,i);outside=MAX(outside,a.x+a.w+24);}
 PangoLayout *layout=gtk_widget_create_pango_layout(widget,NULL);pango_layout_set_ellipsize(layout,PANGO_ELLIPSIZE_END);
 for(guint i=0;i<g->edges->len;i++) {
  InsightFlowEdge e=g_array_index(g->edges,InsightFlowEdge,i);if(e.from>=g->points->len || e.to>=g->points->len) continue;Rect a=rect(g,e.from),b=rect(g,e.to);gboolean back=b.y<=a.y,skip=b.y>a.y+100;double x=a.x+a.w/2,y=a.y+a.h,tx=b.x+b.w/2,ty=b.y,lx=x+5,ly=y+2;guint direction=0;
  cairo_set_source_rgba(cr,e.kind==INSIGHT_FLOW_NO ? .8:.15,e.kind==INSIGHT_FLOW_YES ? .55:.35,.55,.85);cairo_set_line_width(cr,1.5);RoutePoint *destination=g_ptr_array_index(g->points,e.to);gdouble dash[]={5,4};cairo_set_dash(cr,dash,destination->uncertain ? 2:0,0);cairo_move_to(cr,x,y);
  if(back) {double side=outside+(e.from%4)*7;tx=b.x+b.w;ty=b.y+b.h/2;direction=1;cairo_curve_to(cr,side,y+25,side,ty,tx,ty);}
  else if(skip) {double side=8+(e.from%3)*5;tx=b.x;ty=b.y+b.h/2;direction=2;lx=side+3;ly=a.y+a.h/2+3;cairo_move_to(cr,a.x,a.y+a.h/2);cairo_curve_to(cr,side,a.y+a.h/2,side,ty,tx,ty);}
  else cairo_curve_to(cr,x,y+18,tx,ty-18,tx,ty);cairo_stroke(cr);cairo_set_dash(cr,NULL,0,0);
  cairo_move_to(cr,tx,ty);if(direction) {double dx=direction==1 ? 7:-7;cairo_line_to(cr,tx+dx,ty-4);cairo_line_to(cr,tx+dx,ty+4);}else {cairo_line_to(cr,tx-4,ty-7);cairo_line_to(cr,tx+4,ty-7);}cairo_close_path(cr);cairo_fill(cr);
  if(e.kind==INSIGHT_FLOW_YES || e.kind==INSIGHT_FLOW_NO) {pango_layout_set_width(layout,-1);pango_layout_set_text(layout,insight_tr(e.kind==INSIGHT_FLOW_YES ? "Yes":"No"),-1);cairo_move_to(cr,lx,ly);pango_cairo_show_layout(cr,layout);}

 }
 for(guint i=0;i<g->points->len;i++) {
  RoutePoint *p=g_ptr_array_index(g->points,i);Rect a=rect(g,i);gboolean branch=p->kind==INSIGHT_FLOW_IF || p->kind==INSIGHT_FLOW_LOOP;
  if(branch) {cairo_move_to(cr,a.x+12,a.y);cairo_line_to(cr,a.x+a.w-12,a.y);cairo_line_to(cr,a.x+a.w,a.y+a.h/2);cairo_line_to(cr,a.x+a.w-12,a.y+a.h);cairo_line_to(cr,a.x+12,a.y+a.h);cairo_line_to(cr,a.x,a.y+a.h/2);cairo_close_path(cr);}else cairo_rectangle(cr,a.x,a.y,a.w,a.h);
  cairo_set_source_rgba(cr,.28,.55,.85,(gint)i==r->selected ? .38:.13);cairo_fill_preserve(cr);gdk_cairo_set_source_rgba(cr,&fg);cairo_set_line_width(cr,(gint)i==r->selected ? 2:1);cairo_stroke(cr);
  const gchar *special=p->kind==INSIGHT_FLOW_ENTRY ? insight_tr("Entry"):p->kind==INSIGHT_FLOW_EXIT ? insight_tr("Exit"):NULL;gchar *base=p->path ? g_path_get_basename(p->path):g_strdup("untitled"),*title=g_strdup_printf("%u · %s:%u",i+1,base,p->line+1);pango_layout_set_width(layout,(gint)((a.w-24)*PANGO_SCALE));pango_layout_set_text(layout,title,-1);cairo_move_to(cr,a.x+12,a.y+5);pango_cairo_show_layout(cr,layout);pango_layout_set_text(layout,special ? special:p->label ? p->label:p->text,-1);cairo_move_to(cr,a.x+12,a.y+29);pango_cairo_show_layout(cr,layout);g_free(title);g_free(base);
 }
 if(!g->points->len) {pango_layout_set_width(layout,-1);pango_layout_set_text(layout,insight_tr("Add source locations or build a function flow graph"),-1);gdk_cairo_set_source_rgba(cr,&fg);cairo_move_to(cr,20,20);pango_cairo_show_layout(cr,layout);}
 g_object_unref(layout);return FALSE;
}
static gint hit(Routes *r,gdouble x,gdouble y) {RouteGraph *g=shown(r);x/=r->scale;y/=r->scale;for(guint i=0;i<g->points->len;i++) {Rect a=rect(g,i);if(x>=a.x && x<a.x+a.w && y>=a.y && y<a.y+a.h) return (gint)i;}return -1;}
static gboolean clicked(GtkWidget *w,GdkEventButton *event,gpointer data) {
 Routes *r=data;if(event->button!=1 || event->type!=GDK_BUTTON_PRESS) return FALSE;gint index=hit(r,event->x,event->y);if(index<0) return FALSE;gtk_widget_grab_focus(w);r->selected=index;
 if(shown(r)==&r->routes && r->from>=0) {edge_add(r,(guint)r->from,(guint)index);r->from=-1;note(r,"Source route added");}else navigate(r,(guint)index);update(r);return TRUE;
}
static gboolean key(GtkWidget *w,GdkEventKey *event,gpointer data) {
 (void)w;Routes *r=data;guint n=shown(r)->points->len;if(!n) return FALSE;
 if(event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down) {r->selected=CLAMP(r->selected+(event->keyval==GDK_KEY_Up ? -1:1),0,(gint)n-1);update(r);Rect a=rect(shown(r),(guint)r->selected);GtkAdjustment *v=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(r->scroll));gtk_adjustment_clamp_page(v,a.y*r->scale,(a.y+a.h)*r->scale);return TRUE;}
 if(event->keyval==GDK_KEY_Return && r->selected>=0) {navigate(r,(guint)r->selected);return TRUE;}if(event->keyval==GDK_KEY_Escape) {r->from=-1;return TRUE;}return FALSE;
}
static gboolean tooltip(GtkWidget *w,gint x,gint y,gboolean keyboard,GtkTooltip *tip,gpointer data) {
 (void)w;Routes *r=data;gint i=keyboard ? r->selected:hit(r,x,y);RouteGraph *g=shown(r);if(i<0 || (guint)i>=g->points->len) return FALSE;RoutePoint *p=g_ptr_array_index(g->points,i);gchar *text=g_strdup_printf("%s:%u:%u\n%s",p->path ? p->path:"untitled",p->line+1,p->column+1,p->text);gtk_tooltip_set_text(tip,text);g_free(text);return TRUE;
}
static void changed(GtkWidget *w,gpointer data) {(void)w;Routes *r=data;if(r->call_cancel && shown(r)!=&r->calls) cancel_calls(r);r->selected=r->from=-1;note(r,shown(r)==&r->routes ? "Manual links: use Call paths to find intermediate functions":shown(r)==&r->calls ? "Lexical call paths: click a function; dashed arrows are uncertain":"Flow snapshot: click a node; rebuild after source edits");update(r);}
static void zoomed(GtkSpinButton *spin,gpointer data) {Routes *r=data;r->scale=gtk_spin_button_get_value(spin)/100.;update(r);}
static gboolean wheel(GtkWidget *w,GdkEventScroll *event,gpointer data) {(void)w;Routes *r=data;if(!(event->state&GDK_CONTROL_MASK)) return FALSE;double dx=0,dy=0;if(!gdk_event_get_scroll_deltas((GdkEvent *)event,&dx,&dy)) dy=event->direction==GDK_SCROLL_UP ? -1:1;gtk_spin_button_set_value(GTK_SPIN_BUTTON(r->zoom),r->scale*100-dy*10);return TRUE;}
void insight_source_routes_capture(GtkWidget *widget) {
 Routes *r=g_object_get_data(G_OBJECT(widget),"insight-source-routes-state");if(!r) return;GeanyDocument *doc=document_get_current();if(!DOC_VALID(doc) || !doc->file_name) {note(r,"Save the source file before adding a route location");return;}
 gtk_combo_box_set_active(GTK_COMBO_BOX(r->mode),0);gsize offset=(gsize)sci_get_current_position(doc->editor->sci);RoutePoint *p=capture(doc,offset,INSIGHT_FLOW_STEP,0);if(!p) {note(r,"Source line is too long to capture");return;}
 for(guint i=0;i<r->routes.points->len;i++) {RoutePoint *old=g_ptr_array_index(r->routes.points,i);if(!g_strcmp0(old->path,p->path) && old->line==p->line && old->column==p->column) {point_free(p);r->selected=(gint)i;update(r);note(r,"Source location already exists");return;}}
 if(r->routes.points->len==ROUTE_LIMIT) {point_free(p);note(r,"Source route graph is full");return;}g_ptr_array_add(r->routes.points,p);r->selected=(gint)r->routes.points->len-1;note(r,"Source location added; select Link from selected to connect it");update(r);save(r);
}
static void capture_clicked(GtkWidget *w,gpointer data) {(void)w;Routes *r=data;insight_source_routes_capture(r->root);}
static void link_clicked(GtkWidget *w,gpointer data) {(void)w;Routes *r=data;if(shown(r)!=&r->routes || r->selected<0) {note(r,"Select a source route node first");return;}r->from=r->selected;note(r,"Click the destination node; Escape cancels linking");}
static void remove_clicked(GtkWidget *w,gpointer data) {
 (void)w;Routes *r=data;if(shown(r)!=&r->routes || r->selected<0) return;guint index=(guint)r->selected;
 for(guint i=r->routes.edges->len;i>0;i--) {InsightFlowEdge *e=&g_array_index(r->routes.edges,InsightFlowEdge,i-1);if(e->from==index || e->to==index) g_array_remove_index(r->routes.edges,i-1);else {if(e->from>index) e->from--;if(e->to>index) e->to--;}}
 g_ptr_array_remove_index(r->routes.points,index);r->from=r->selected=-1;update(r);save(r);
}
static gboolean dialect(GeanyDocument *doc,InsightCodeDialect *out) {
 if(!doc->file_type) return FALSE;const gchar *name=doc->file_type->name;
 if(!g_strcmp0(name,"JavaScript") || !g_strcmp0(name,"TypeScript")) {*out=INSIGHT_CODE_JS;return TRUE;}
 *out=INSIGHT_CODE_C;return !g_strcmp0(name,"C") || !g_strcmp0(name,"C++") || !g_strcmp0(name,"C#") || !g_strcmp0(name,"Java");
}
static void build_flow(GtkWidget *button,gpointer data) {
 Routes *r=data;gtk_combo_box_set_active(GTK_COMBO_BOX(r->mode),1);GeanyDocument *doc=document_get_current();InsightCodeDialect code;gint length=DOC_VALID(doc) ? sci_get_length(doc->editor->sci):0;
 if(!DOC_VALID(doc) || length>2*1024*1024 || !dialect(doc,&code)) {note(r,"Flow graphs require a C-like source buffer up to 2 MiB");return;}
 gchar *text=sci_get_contents(doc->editor->sci,length+1);if(memchr(text,0,(gsize)length) || !g_utf8_validate(text,length,NULL)) {g_free(text);note(r,"Source must be valid UTF-8 without NUL");return;}
 gchar *mask=insight_source_code_mask(text,length,code,NULL);if(!mask) {g_free(text);note(r,"Source could not be masked");return;}
 gsize body=0,end=0,base=0;gchar *selected=NULL;gboolean selection=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"selection"));
 if(selection) {
  if(sci_get_selection_mode(doc->editor->sci)!=SC_SEL_STREAM || SEND(doc->editor->sci,SCI_GETSELECTIONS,0,0)!=1 || SEND(doc->editor->sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) || SEND(doc->editor->sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0)) {note(r,"Select one normal range of complete statements");goto done;}
  base=(gsize)sci_get_selection_start(doc->editor->sci);end=(gsize)sci_get_selection_end(doc->editor->sci);if(end<=base) {note(r,"Select one normal range of complete statements");goto done;}
  selected=g_malloc(end-base+3);selected[0]='{';memcpy(selected+1,mask+base,end-base);selected[end-base+1]='}';selected[end-base+2]=0;end=end-base+2;
 } else {
  document_update_tags(doc);gsize caret=(gsize)sci_get_current_position(doc->editor->sci),best=G_MAXSIZE;
  for(guint i=0;doc->tm_file && doc->tm_file->tags_array && i<doc->tm_file->tags_array->len;i++) {TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,i);if(!tag || !(tag->type&(tm_tag_function_t|tm_tag_method_t)) || !tag->line || tag->line>(gulong)sci_get_line_count(doc->editor->sci)) continue;gsize a=(gsize)sci_get_position_from_line(doc->editor->sci,(gint)tag->line-1),b=0,z=0;
   if(a<=caret && insight_function_range(text,mask,(gsize)length,a,INSIGHT_FUNCTION_BRACES,&b,&z)==1 && caret<z && z-a<best && b) {body=b-1;end=z;best=z-a;}}
  if(!end) {note(r,"Place the caret inside a complete brace-bodied function");goto done;}
 }
 InsightFlow *flow=g_new0(InsightFlow,1);if(!insight_control_flow(selected ? selected:mask,selected ? end:(gsize)length,body,end,flow)) {insight_i18n_text(G_OBJECT(r->status),"label","Flow graph unavailable: {value}",insight_tr(flow->error));g_free(flow);goto done;}
 RouteGraph next={g_ptr_array_new_with_free_func(point_free),g_array_new(FALSE,FALSE,sizeof(InsightFlowEdge)),NULL,doc->id};
 for(guint i=0;i<flow->count;i++) {InsightFlowNode n=flow->nodes[i];gsize at=selection ? base+(n.begin ? n.begin-1:0):n.begin;if(at>(gsize)length) at=(gsize)length;RoutePoint *p=capture(doc,at,n.kind,n.lane);if(!p) {graph_clear(&next);g_ptr_array_unref(next.points);g_array_unref(next.edges);g_free(flow);note(r,"Source line is too long to capture");goto done;}gsize bytes=MIN(n.end-n.begin,(gsize)4096);gchar *raw=g_strndup(text+at,MIN(bytes,(gsize)length-at)),*valid=g_utf8_make_valid(raw,-1);g_free(raw);for(gchar *c=valid;*c;c++) if(*c=='\n' || *c=='\r' || *c=='\t') *c=' ';p->label=g_utf8_substring(valid,0,MIN(g_utf8_strlen(valid,-1),180));g_free(valid);g_ptr_array_add(next.points,p);}
 g_array_append_vals(next.edges,flow->edges,flow->edge_count);next.digest=g_compute_checksum_for_data(G_CHECKSUM_SHA256,(const guchar *)text,length);graph_clear(&r->flow);g_ptr_array_unref(r->flow.points);g_array_unref(r->flow.edges);r->flow=next;g_free(flow);gtk_combo_box_set_active(GTK_COMBO_BOX(r->mode),1);r->selected=0;update(r);note(r,"Flow snapshot: click a node; rebuild after source edits");
 done:g_free(selected);g_free(mask);g_free(text);
}
static void cancel_calls(Routes *r) {r->call_serial++;if(!r->closed) g_object_set_data(G_OBJECT(r->root),"insight-call-search-running",NULL);if(r->call_cancel) g_cancellable_cancel(r->call_cancel);g_clear_object(&r->call_cancel);}
static void reset_calls(Routes *r) {cancel_calls(r);graph_clear(&r->calls);g_clear_pointer(&r->call_result,insight_call_paths_free);g_clear_pointer(&r->endpoint_path[0],g_free);g_clear_pointer(&r->endpoint_path[1],g_free);}
typedef struct {
 InsightReportSnapshot *snapshot;GHashTable *buffers;gchar *names[2],*paths[2];guint lines[2];gint encoding;guint64 serial;gboolean possible;InsightCallPaths *result;
} CallJob;
static void call_job_free(gpointer data) {CallJob *j=data;insight_report_snapshot_unref(j->snapshot);g_hash_table_unref(j->buffers);for(guint i=0;i<2;i++) {g_free(j->names[i]);g_free(j->paths[i]);}insight_call_paths_free(j->result);g_free(j);}
static gchar *load_call_source(const gchar *path,GCancellable *cancel,gpointer data) {
 CallJob *j=data;if(g_hash_table_contains(j->buffers,path)) return g_strdup(g_hash_table_lookup(j->buffers,path));
 gchar *native=utils_get_locale_from_utf8(path);GFile *file=g_file_new_for_path(native);g_free(native);GFileInputStream *stream=g_file_read(file,cancel,NULL);g_object_unref(file);if(!stream) return NULL;
 GString *text=g_string_sized_new(16384);gchar chunk[16384];gboolean ok=TRUE;for(;;) {gssize n=g_input_stream_read(G_INPUT_STREAM(stream),chunk,sizeof(chunk),cancel,NULL);if(n<0 || (n>0 && text->len+(gsize)n>2u*1024u*1024u)) {ok=FALSE;break;}if(!n) break;g_string_append_len(text,chunk,(gsize)n);}g_object_unref(stream);
 if(!ok) {g_string_free(text,TRUE);return NULL;}gsize length=text->len;gchar *bytes=g_string_free(text,FALSE);gboolean nul=FALSE;
 if(!encodings_convert_to_utf8_worker(&bytes,&length,j->encoding,&nul) || nul || length>2u*1024u*1024u || memchr(bytes,0,length) || !g_utf8_validate(bytes,length,NULL)) {g_free(bytes);return NULL;}return bytes;
}
static void call_job_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source;CallJob *j=data;InsightCallEndpoint endpoints[2]={{j->names[0],j->paths[0],j->lines[0]},{j->names[1],j->paths[1],j->lines[1]}};GError *error=NULL;
 j->result=insight_call_paths_find(j->snapshot->index,j->snapshot->paths,&endpoints[0],&endpoints[1],j->possible,load_call_source,j,cancel,&error);
 if(error) g_task_return_error(task,error);else g_task_return_boolean(task,TRUE);
}
static RoutePoint *call_point(const gchar *path,InsightCallSource *source,guint line,guint column,const gchar *label) {
 if(!source || !line || line>source->lines->len) return NULL;gsize begin=g_array_index(source->lines,gsize,line-1),end=begin;while(source->text[end] && source->text[end]!='\n') end++;if(end-begin>4096) end=begin+4096;
 RoutePoint *p=g_new0(RoutePoint,1);p->path=g_strdup(path);p->line=line-1;p->column=column;p->kind=INSIGHT_FLOW_STEP;p->text=g_utf8_make_valid(source->text+begin,(gssize)(end-begin));p->label=g_strdup(label);p->digest=g_strdup(source->digest);return p;
}
static void call_result_status(Routes *r) {
 if(!r->call_result) return;InsightCallPaths *result=r->call_result;gchar *message=g_strdup_printf(insight_tr("%u shortest lexical paths • %u unresolved • %u ambiguous • %u skipped%s"),result->paths->len,result->unresolved,result->ambiguous,result->skipped,result->limited ? insight_tr(" • work limit reached"):"");gtk_label_set_text(GTK_LABEL(r->status),message);g_free(message);
 if(!result->paths->len) note(r,result->limited || result->skipped ? "No path found in the inspected sources; search is incomplete":"No lexical call path found; callbacks may need uncertain name matching");
}
static void show_call_variant(GtkComboBox *combo,gpointer data) {
 Routes *r=data;if(r->setting_endpoint || !r->call_result) return;gint variant=gtk_combo_box_get_active(combo);InsightCallPaths *result=r->call_result;graph_clear(&r->calls);r->selected=r->from=-1;
 if(variant<0 || (guint)variant>=result->paths->len) {update(r);return;}GArray *path=g_ptr_array_index(result->paths,(guint)variant);guint function=result->start;
 for(guint i=0;i<=path->len;i++) {
  InsightCallFunction *f=g_ptr_array_index(result->functions,function);InsightCallSource *source=g_hash_table_lookup(result->sources,f->path);gchar *name=*f->scope ? g_strconcat(f->scope,"::",f->name,NULL):g_strdup(f->name);RoutePoint *p=call_point(f->path,source,f->line,0,name);g_free(name);if(!p) break;
  if(i) {guint edge_id=g_array_index(path,guint,i-1);InsightCallEdge edge=g_array_index(result->edges,InsightCallEdge,edge_id);InsightCallFunction *caller=g_ptr_array_index(result->functions,edge.from);InsightCallSource *s=g_hash_table_lookup(result->sources,caller->path);gchar *label=g_strdup_printf("%s → %s",caller->name,f->name);p->site=call_point(caller->path,s,edge.line,edge.column,label);g_free(label);p->uncertain=edge.uncertain;InsightFlowEdge e={i-1,i,INSIGHT_FLOW_NEXT};g_array_append_val(r->calls.edges,e);}
  g_ptr_array_add(r->calls.points,p);if(i<path->len) {guint edge_id=g_array_index(path,guint,i);function=g_array_index(result->edges,InsightCallEdge,edge_id).to;}
 }
 gtk_combo_box_set_active(GTK_COMBO_BOX(r->mode),2);r->selected=r->calls.points->len ? 0:-1;update(r);call_result_status(r);
}
static void call_job_done(GObject *root,GAsyncResult *result,gpointer unused) {
 (void)unused;Routes *r=g_object_get_data(root,"insight-source-routes-state");GTask *task=G_TASK(result);CallJob *j=g_task_get_task_data(task);GError *error=NULL;gboolean ok=g_task_propagate_boolean(task,&error);
 if(r->closed || r->call_serial!=j->serial) {g_clear_error(&error);return;}g_clear_object(&r->call_cancel);g_object_set_data(G_OBJECT(r->root),"insight-call-search-running",NULL);g_object_set_data(G_OBJECT(r->root),"insight-call-search-completed",GINT_TO_POINTER(1));
 if(!app->project || g_strcmp0(app->project->file_name,j->snapshot->project_file)) {g_clear_error(&error);return;}
 if(!ok || !j->result) {insight_i18n_text(G_OBJECT(r->status),"label","Call path search: {value}",insight_tr(error ? error->message:"No result"));g_clear_error(&error);return;}
 g_clear_pointer(&r->call_result,insight_call_paths_free);r->call_result=j->result;j->result=NULL;if(j->snapshot->incomplete) r->call_result->limited=TRUE;
 r->setting_endpoint=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(r->call_variants));for(guint i=0;i<r->call_result->paths->len;i++) {GArray *path=g_ptr_array_index(r->call_result->paths,i);gchar *label=g_strdup_printf(insight_tr("Path %u — %u calls"),i+1,path->len);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->call_variants),label);g_free(label);}r->setting_endpoint=FALSE;
 gtk_combo_box_set_active(GTK_COMBO_BOX(r->call_variants),r->call_result->paths->len ? 0:-1);show_call_variant(GTK_COMBO_BOX(r->call_variants),r);call_result_status(r);g_object_set_data(G_OBJECT(r->root),"insight-call-search-completed",GINT_TO_POINTER(1));
}
static void find_calls(GtkWidget *w,gpointer data) {
 (void)w;Routes *r=data;cancel_calls(r);g_object_set_data(G_OBJECT(r->root),"insight-call-search-completed",NULL);GError *error=NULL;InsightReportSnapshot *snapshot=insight_project_report_capture(&error);
 if(!snapshot) {insight_i18n_text(G_OBJECT(r->status),"label","Call path search: {value}",insight_tr(error ? error->message:"Project membership is not ready."));g_clear_error(&error);return;}
 CallJob *j=g_new0(CallJob,1);j->snapshot=snapshot;j->buffers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);j->encoding=file_prefs.default_open_encoding;j->serial=r->call_serial;j->possible=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(r->call_possible));
 for(guint i=0;i<2;i++) {j->names[i]=g_strdup(gtk_entry_get_text(GTK_ENTRY(i ? r->call_to:r->call_from)));g_strstrip(j->names[i]);j->paths[i]=g_strdup(r->endpoint_path[i]);j->lines[i]=r->endpoint_line[i];if(!*j->names[i] && !j->paths[i]) {call_job_free(j);note(r,"Enter start and target functions, or set them from selected nodes");return;}}
 gsize total=0;for(guint i=0;documents_array && i<documents_array->len;i++) {GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(!DOC_VALID(doc) || !doc->file_name) continue;gint length=sci_get_length(doc->editor->sci);gchar *text=NULL;if(length>=0 && length<=2*1024*1024 && total+(gsize)length<=32u*1024u*1024u) {text=sci_get_contents(doc->editor->sci,length+1);if(memchr(text,0,(gsize)length) || !g_utf8_validate(text,length,NULL)) g_clear_pointer(&text,g_free);else total+=(gsize)length;}g_hash_table_replace(j->buffers,g_strdup(doc->file_name),text);}
 graph_clear(&r->calls);g_clear_pointer(&r->call_result,insight_call_paths_free);r->setting_endpoint=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(r->call_variants));r->setting_endpoint=FALSE;gtk_combo_box_set_active(GTK_COMBO_BOX(r->mode),2);update(r);note(r,"Searching call paths in the indexed project…");r->call_cancel=g_cancellable_new();g_object_set_data(G_OBJECT(r->root),"insight-call-search-running",GINT_TO_POINTER(1));GTask *task=g_task_new(r->root,r->call_cancel,call_job_done,NULL);g_task_set_task_data(task,j,call_job_free);g_task_run_in_thread(task,call_job_run);g_object_unref(task);
}
static void stop_calls(GtkWidget *w,gpointer data) {(void)w;Routes *r=data;cancel_calls(r);note(r,"Call path search cancelled");}
static void endpoint_changed(GtkEditable *entry,gpointer data) {Routes *r=data;if(r->setting_endpoint) return;cancel_calls(r);guint side=GTK_WIDGET(entry)==r->call_to;g_clear_pointer(&r->endpoint_path[side],g_free);r->endpoint_line[side]=0;}
static void endpoint_from_node(GtkWidget *button,gpointer data) {
 Routes *r=data;RouteGraph *g=shown(r);if(r->selected<0 || (guint)r->selected>=g->points->len) {note(r,"Select a source node first");return;}RoutePoint *p=g_ptr_array_index(g->points,(guint)r->selected);if(!p->path) {note(r,"Save the source file before adding a route location");return;}guint side=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(button),"endpoint-side"));g_free(r->endpoint_path[side]);r->endpoint_path[side]=g_strdup(p->path);cancel_calls(r);r->endpoint_line[side]=p->line+1;gchar *base=g_path_get_basename(p->path),*text=g_strdup_printf("%s:%u",base,p->line+1);r->setting_endpoint=TRUE;gtk_entry_set_text(GTK_ENTRY(side ? r->call_to:r->call_from),text);r->setting_endpoint=FALSE;g_free(base);g_free(text);note(r,"Endpoint selected; set the other endpoint and find call paths");
}
static void call_site_clicked(GtkWidget *w,gpointer data) {(void)w;Routes *r=data;RouteGraph *g=shown(r);if(g!=&r->calls || r->selected<0 || (guint)r->selected>=g->points->len) return;RoutePoint *p=g_ptr_array_index(g->points,(guint)r->selected);if(p->site) navigate_point(r,g,p->site);else note(r,"The first function has no incoming call in this path");}
static void possible_changed(GtkToggleButton *w,gpointer data) {(void)w;Routes *r=data;if(r->call_cancel) {cancel_calls(r);note(r,"Search options changed; find call paths again");}}
static void routes_destroyed(GtkWidget *w,gpointer data) {(void)w;Routes *r=data;r->closed=TRUE;cancel_calls(r);}

static void write_project(GObject *o,GKeyFile *key,gpointer data) {
 (void)o;Routes *r=data;g_key_file_remove_group(key,ROUTE_GROUP,NULL);g_key_file_set_integer(key,ROUTE_GROUP,"version",1);g_key_file_set_integer(key,ROUTE_GROUP,"nodes",(gint)r->routes.points->len);
 gchar *base=project_get_base_path();GFile *root=base ? g_file_new_for_path(base):NULL;
 for(guint i=0;i<r->routes.points->len;i++) {RoutePoint *p=g_ptr_array_index(r->routes.points,i);GFile *file=g_file_new_for_path(p->path);gchar *relative=root ? g_file_get_relative_path(root,file):NULL,*name=g_strdup_printf("node-%u",i),*line=g_strdup_printf("%u",p->line),*column=g_strdup_printf("%u",p->column);const gchar *values[]={relative ? relative:p->path,line,column,p->text};g_key_file_set_string_list(key,ROUTE_GROUP,name,values,4);g_free(relative);g_free(name);g_free(line);g_free(column);g_object_unref(file);}
 g_key_file_set_integer(key,ROUTE_GROUP,"edges",(gint)r->routes.edges->len);
 for(guint i=0;i<r->routes.edges->len;i++) {InsightFlowEdge e=g_array_index(r->routes.edges,InsightFlowEdge,i);gint values[]={(gint)e.from,(gint)e.to};gchar *name=g_strdup_printf("edge-%u",i);g_key_file_set_integer_list(key,ROUTE_GROUP,name,values,2);g_free(name);}
 if(root) g_object_unref(root);g_free(base);
}
static gboolean number(const gchar *s,guint *value,guint max) {if(!s || !*s || !g_ascii_isdigit(*s)) return FALSE;gchar *end=NULL;guint64 n=g_ascii_strtoull(s,&end,10);if(!end || *end || n>max) return FALSE;*value=(guint)n;return TRUE;}
static void read_project(GObject *o,GKeyFile *key,gpointer data) {
 (void)o;Routes *r=data;reset_calls(r);r->loading=TRUE;graph_clear(&r->routes);graph_clear(&r->flow);r->selected=r->from=-1;
 gint count=g_key_file_get_integer(key,ROUTE_GROUP,"nodes",NULL),edges=g_key_file_get_integer(key,ROUTE_GROUP,"edges",NULL);gboolean valid=g_key_file_get_integer(key,ROUTE_GROUP,"version",NULL)==1 && count>=0 && count<=ROUTE_LIMIT && edges>=0 && edges<=INSIGHT_FLOW_EDGE_LIMIT;gchar *base=project_get_base_path();
 for(gint i=0;valid && i<count;i++) {gchar *name=g_strdup_printf("node-%d",i);gsize n=0;gchar **v=g_key_file_get_string_list(key,ROUTE_GROUP,name,&n,NULL);g_free(name);guint line=0,column=0;
  valid=n==4 && v && *v[0] && strlen(v[0])<=8192 && g_utf8_validate(v[0],-1,NULL) && strlen(v[3])<=4096 && g_utf8_validate(v[3],-1,NULL) && number(v[1],&line,10000000) && number(v[2],&column,4096);
  if(valid) {RoutePoint *p=g_new0(RoutePoint,1);p->path=insight_canonicalize_filename(v[0],base);p->line=line;p->column=column;p->text=g_strdup(v[3]);p->kind=INSIGHT_FLOW_STEP;g_ptr_array_add(r->routes.points,p);}g_strfreev(v);
 }
 for(gint i=0;valid && i<edges;i++) {gchar *name=g_strdup_printf("edge-%d",i);gsize n=0;gint *v=g_key_file_get_integer_list(key,ROUTE_GROUP,name,&n,NULL);g_free(name);valid=n==2 && v && v[0]>=0 && v[1]>=0 && v[0]<count && v[1]<count && v[0]!=v[1];if(valid) {InsightFlowEdge e={(guint)v[0],(guint)v[1],INSIGHT_FLOW_NEXT};gboolean exists=FALSE;for(guint k=0;k<r->routes.edges->len;k++) {InsightFlowEdge old=g_array_index(r->routes.edges,InsightFlowEdge,k);if(old.from==e.from && old.to==e.to) exists=TRUE;}if(!exists) g_array_append_val(r->routes.edges,e);}g_free(v);}
 if(!valid) graph_clear(&r->routes);g_free(base);r->loading=FALSE;gtk_combo_box_set_active(GTK_COMBO_BOX(r->mode),0);update(r);note(r,valid || !g_key_file_has_group(key,ROUTE_GROUP) ? "Source routes: click a node to open its source":"Invalid source route data was ignored");
}
static void close_project(GObject *o,gpointer data) {(void)o;Routes *r=data;reset_calls(r);graph_clear(&r->routes);graph_clear(&r->flow);r->selected=r->from=-1;update(r);note(r,"Source routes: click a node to open its source");}
static void project_save_signal(GObject *o,GKeyFile *key,GtkWidget *root) {write_project(o,key,g_object_get_data(G_OBJECT(root),"insight-source-routes-state"));}
static void project_open_signal(GObject *o,GKeyFile *key,GtkWidget *root) {read_project(o,key,g_object_get_data(G_OBJECT(root),"insight-source-routes-state"));}
static void project_close_signal(GObject *o,GtkWidget *root) {close_project(o,g_object_get_data(G_OBJECT(root),"insight-source-routes-state"));}
static void freed(gpointer data) {Routes *r=data;reset_calls(r);g_ptr_array_unref(r->calls.points);g_array_unref(r->calls.edges);graph_clear(&r->routes);graph_clear(&r->flow);g_ptr_array_unref(r->routes.points);g_ptr_array_unref(r->flow.points);g_array_unref(r->routes.edges);g_array_unref(r->flow.edges);g_free(r);}
static GtkWidget *action(GtkWidget *box,const gchar *label,const gchar *name,const gchar *tip,GCallback callback,Routes *r) {
 GtkWidget *button=gtk_button_new_with_label(insight_tr(label));gtk_widget_set_name(button,name);insight_i18n_key(G_OBJECT(button),"label",label);insight_i18n_key(G_OBJECT(button),"tooltip-text",tip);gtk_widget_set_focus_on_click(button,FALSE);gtk_container_add(GTK_CONTAINER(box),button);g_signal_connect(button,"clicked",callback,r);return button;
}
static GtkWidget *controls(GtkWidget *box) {GtkWidget *flow=gtk_flow_box_new();gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(flow),GTK_SELECTION_NONE);gtk_flow_box_set_min_children_per_line(GTK_FLOW_BOX(flow),1);gtk_flow_box_set_max_children_per_line(GTK_FLOW_BOX(flow),4);gtk_box_pack_start(GTK_BOX(box),flow,FALSE,FALSE,0);return flow;}
GtkWidget *insight_source_routes_new(void) {
 Routes *r=g_new0(Routes,1);r->selected=r->from=-1;r->scale=1;r->routes.points=g_ptr_array_new_with_free_func(point_free);r->flow.points=g_ptr_array_new_with_free_func(point_free);r->calls.points=g_ptr_array_new_with_free_func(point_free);r->routes.edges=g_array_new(FALSE,FALSE,sizeof(InsightFlowEdge));r->flow.edges=g_array_new(FALSE,FALSE,sizeof(InsightFlowEdge));r->calls.edges=g_array_new(FALSE,FALSE,sizeof(InsightFlowEdge));
 r->root=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_widget_set_name(r->root,"insight-source-routes");g_object_set_data_full(G_OBJECT(r->root),"insight-source-routes-state",r,freed);
 GtkWidget *options=insight_compare_ui_tabs(r->root,"insight-source-routes-options"),*locations=insight_compare_ui_page(options,"Source routes"),*flow=insight_compare_ui_page(options,"Control flow"),*view=insight_compare_ui_page(options,"View");
 GtkWidget *row=controls(locations);action(row,"Add caret location","insight-route-add","Add the current source position to the project route graph",G_CALLBACK(capture_clicked),r);r->link=action(row,"Link from selected","insight-route-link","Select a node, then click this button and the destination node",G_CALLBACK(link_clicked),r);action(row,"Remove node","insight-route-remove","Remove the selected route node and its arrows",G_CALLBACK(remove_clicked),r);
 row=controls(flow);action(row,"Function flow","insight-route-function","Build a lexical control-flow snapshot of the function at the caret",G_CALLBACK(build_flow),r);GtkWidget *selection=action(row,"Selection flow","insight-route-selection","Build a flow graph from selected complete C-like statements",G_CALLBACK(build_flow),r);g_object_set_data(G_OBJECT(selection),"selection",GINT_TO_POINTER(1));
 GtkWidget *paths_page=insight_compare_ui_page(options,"Call paths");
 GtkWidget *explain=gtk_label_new(insight_tr("Find intermediate functions here; the symbol/text query above is separate"));gtk_label_set_line_wrap(GTK_LABEL(explain),TRUE);gtk_box_pack_start(GTK_BOX(paths_page),explain,FALSE,FALSE,0);
 for(guint i=0;i<2;i++) {GtkWidget *entry=gtk_entry_new();gtk_entry_set_width_chars(GTK_ENTRY(entry),12);gtk_widget_set_hexpand(entry,TRUE);gtk_entry_set_max_length(GTK_ENTRY(entry),1024);gtk_widget_set_name(entry,i ? "insight-call-to":"insight-call-from");gtk_entry_set_placeholder_text(GTK_ENTRY(entry),insight_tr(i ? "To function, e.g. create_rule":"From function, e.g. main"));insight_i18n_key(G_OBJECT(entry),"tooltip-text",i ? "Target function name; use To selected for ambiguous definitions":"Start function name; use From selected for ambiguous definitions");gtk_box_pack_start(GTK_BOX(paths_page),entry,FALSE,FALSE,0);if(i) r->call_to=entry;else r->call_from=entry;g_signal_connect(entry,"changed",G_CALLBACK(endpoint_changed),r);g_signal_connect(entry,"activate",G_CALLBACK(find_calls),r);}
 gtk_entry_set_text(GTK_ENTRY(r->call_from),"main");
 row=controls(paths_page);for(guint i=0;i<2;i++) {GtkWidget *button=action(row,i ? "To selected":"From selected",i ? "insight-call-set-to":"insight-call-set-from","Use the enclosing function of the selected graph node as an endpoint",G_CALLBACK(endpoint_from_node),r);g_object_set_data(G_OBJECT(button),"endpoint-side",GUINT_TO_POINTER(i));}
 action(row,"Find call paths","insight-call-find","Find shortest lexical call chains between the two functions",G_CALLBACK(find_calls),r);action(row,"Stop","insight-call-stop","Cancel the current call-path search",G_CALLBACK(stop_calls),r);
 r->call_possible=gtk_check_button_new_with_label(insight_tr("Include uncertain name matches"));insight_i18n_key(G_OBJECT(r->call_possible),"tooltip-text","Allow ambiguous and member-name matches; dashed edges are hypotheses, not proven calls");gtk_box_pack_start(GTK_BOX(paths_page),r->call_possible,FALSE,FALSE,0);g_signal_connect(r->call_possible,"toggled",G_CALLBACK(possible_changed),r);
 r->call_variants=gtk_combo_box_text_new();gtk_widget_set_name(r->call_variants,"insight-call-variants");insight_i18n_key(G_OBJECT(r->call_variants),"tooltip-text","Choose an alternative shortest call path");gtk_box_pack_start(GTK_BOX(paths_page),r->call_variants,FALSE,FALSE,0);g_signal_connect(r->call_variants,"changed",G_CALLBACK(show_call_variant),r);row=controls(paths_page);action(row,"Go to call site","insight-call-site","Open the call site leading to the selected function",G_CALLBACK(call_site_clicked),r);
 r->back=gtk_button_new_with_label(insight_tr("Symbol relations"));gtk_widget_set_name(r->back,"insight-route-back");insight_i18n_key(G_OBJECT(r->back),"tooltip-text","Return to symbol references, calls and relation graphs");gtk_box_pack_start(GTK_BOX(r->root),r->back,FALSE,FALSE,0);
 r->mode=gtk_combo_box_text_new();gtk_widget_set_name(r->mode,"insight-route-mode");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->mode),insight_tr("Source routes"));gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->mode),insight_tr("Control flow"));gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(r->mode),insight_tr("Call paths"));gtk_combo_box_set_active(GTK_COMBO_BOX(r->mode),0);gtk_widget_set_tooltip_text(r->mode,insight_tr("Choose saved source routes or the generated flow snapshot"));gtk_box_pack_start(GTK_BOX(view),r->mode,FALSE,FALSE,0);
 r->zoom=gtk_spin_button_new_with_range(40,200,10);gtk_spin_button_set_value(GTK_SPIN_BUTTON(r->zoom),100);gtk_widget_set_name(r->zoom,"insight-route-zoom");insight_i18n_key(G_OBJECT(r->zoom),"tooltip-text","Graph zoom (%) — Ctrl+mouse wheel");gtk_box_pack_start(GTK_BOX(view),r->zoom,FALSE,FALSE,0);insight_ai_ui_fold(options,"Collapse");
 r->status=gtk_label_new(NULL);gtk_widget_set_name(r->status,"insight-route-status");gtk_label_set_xalign(GTK_LABEL(r->status),0);gtk_label_set_line_wrap(GTK_LABEL(r->status),TRUE);gtk_label_set_selectable(GTK_LABEL(r->status),TRUE);gtk_box_pack_start(GTK_BOX(r->root),r->status,FALSE,FALSE,0);
 r->scroll=gtk_scrolled_window_new(NULL,NULL);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(r->scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);r->canvas=gtk_drawing_area_new();gtk_widget_set_name(r->canvas,"insight-route-canvas");gtk_widget_set_can_focus(r->canvas,TRUE);gtk_widget_set_has_tooltip(r->canvas,TRUE);gtk_widget_add_events(r->canvas,GDK_BUTTON_PRESS_MASK|GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK|GDK_KEY_PRESS_MASK);gtk_container_add(GTK_CONTAINER(r->scroll),r->canvas);gtk_box_pack_start(GTK_BOX(r->root),r->scroll,TRUE,TRUE,0);
 g_signal_connect(r->root,"destroy",G_CALLBACK(routes_destroyed),r);
 g_signal_connect(r->canvas,"draw",G_CALLBACK(draw),r);g_signal_connect(r->canvas,"button-press-event",G_CALLBACK(clicked),r);g_signal_connect(r->canvas,"key-press-event",G_CALLBACK(key),r);g_signal_connect(r->canvas,"query-tooltip",G_CALLBACK(tooltip),r);g_signal_connect(r->canvas,"scroll-event",G_CALLBACK(wheel),r);g_signal_connect(r->mode,"changed",G_CALLBACK(changed),r);g_signal_connect(r->zoom,"value-changed",G_CALLBACK(zoomed),r);
 g_signal_connect_object(geany_object,"project-save",G_CALLBACK(project_save_signal),r->root,0);g_signal_connect_object(geany_object,"project-open",G_CALLBACK(project_open_signal),r->root,0);g_signal_connect_object(geany_object,"project-close",G_CALLBACK(project_close_signal),r->root,0);
 /* Signal lifetime follows root; wrappers below recover the state from it. */
 note(r,"Source routes: click a node to open its source");update(r);
 if(app->project && app->project->file_name) {GKeyFile *key=g_key_file_new();gchar *path=utils_get_locale_from_utf8(app->project->file_name);if(g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL)) read_project(NULL,key,r);g_free(path);g_key_file_unref(key);}
 return r->root;
}

GtkWidget *insight_source_routes_back_button(GtkWidget *root) {Routes *r=g_object_get_data(G_OBJECT(root),"insight-source-routes-state");return r ? r->back:NULL;}
