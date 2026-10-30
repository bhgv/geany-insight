/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-completion.h"
#include "insight-languages.h"
#include "document.h"
#include "insight-clang.h"
#include "insight-workbench.h"
#include "insight-template-fields.h"
#include "project.h"
#include "utils.h"
#include "insight-i18n.h"
#include "sciwrappers.h"
#include "symbols.h"
#include "main.h"
#include "ui_utils.h"
#include "tm_tag.h"
#include "../insight/desktop/completion_preferences.h"
#include "../insight/desktop/filetype_pairs.h"
#include <gdk/gdkkeysyms.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define KEY "insight-detailed-completion"
enum { NAME,MARKUP,ICON,KIND,SCOPE,ORIGIN,IDENTITY,CALLABLE,PARAMETERS,SNIPPET,COLUMNS };
typedef struct { ScintillaObject *sci;GtkWidget *window,*view,*footer,*scroll;GtkListStore *store;guint document,idle,focus_idle;gint start,end;gchar *prefix;gboolean active,accepting,limited,bypass;GCancellable *pending;guint semantic_idle,semantic_serial;gsize semantic_root;guint64 snippet_generation;gint popup_width,popup_height; } Completion;
static gboolean loaded;
static guint semantic_workers;
static InsightCompletionSettings settings;
static GtkWidget *detail_item;
static gboolean refresh(gpointer);
static gchar *config_path(void) { return g_build_filename(app->configdir,"insight-completion.conf",NULL); }
static void load(void) { if(loaded) return;loaded=TRUE;insight_completion_settings_default(&settings);GKeyFile *k=g_key_file_new();gchar *path=config_path();if(g_key_file_load_from_file(k,path,0,NULL)) { GError *error=NULL;if(!insight_completion_settings_read(&settings,k,&error)) g_warning("Invalid completion settings: %s",error->message);g_clear_error(&error); }g_key_file_unref(k);g_free(path); }
gboolean insight_completion_case_sensitive(void) { load();return settings.case_sensitive; }
gint insight_completion_min_chars(void) { load();return settings.min_chars ? settings.min_chars : editor_prefs.symbolcompletion_min_chars; }
gboolean insight_completion_inside_comments(void) { load();return settings.inside_comments; }
static Completion *state(GeanyEditor *editor) { return editor ? g_object_get_data(G_OBJECT(editor->sci),KEY) : NULL; }
static void semantic_cancel(Completion *c) {if(!c) return;c->semantic_serial++;if(c->semantic_idle) {g_source_remove(c->semantic_idle);c->semantic_idle=0;}if(c->pending) {g_cancellable_cancel(c->pending);g_clear_object(&c->pending);}}
static void hide_at(Completion *c,const gchar *reason) { if(!c) return;semantic_cancel(c);if(c->active && g_getenv("GEANY_COMPLETION_DEBUG")) g_printerr("completion hide %s end=%d caret=%d idle=%u\n",reason,c->end,sci_get_current_position(c->sci),c->idle);c->active=FALSE;if(c->idle) { g_source_remove(c->idle);c->idle=0; }gtk_widget_hide(c->window); }


#define hide(c) hide_at(c,__func__)
void insight_completion_cancel(GeanyEditor *editor) { hide(state(editor)); }
void insight_completion_native_fallback(GeanyEditor *editor) {Completion *c=state(editor);if(!c) return;c->active=FALSE;if(c->idle) {g_source_remove(c->idle);c->idle=0;}gtk_widget_hide(c->window);}
static void destroy(gpointer data) { Completion *c=data;semantic_cancel(c);if(c->focus_idle) g_source_remove(c->focus_idle);if(c->idle) g_source_remove(c->idle);gtk_widget_destroy(c->window);g_object_unref(c->store);g_free(c->prefix);g_free(c); }
static const gchar *kind(TMTagType type) {
 if(type & (tm_tag_function_t|tm_tag_prototype_t|tm_tag_method_t)) return "function";
 if(type & (tm_tag_class_t|tm_tag_struct_t|tm_tag_interface_t|tm_tag_union_t|tm_tag_typedef_t)) return "type";
 if(type & (tm_tag_namespace_t|tm_tag_package_t)) return "namespace";
 if(type & (tm_tag_macro_t|tm_tag_macro_with_arg_t)) return "macro";
 if(type & (tm_tag_enum_t|tm_tag_enumerator_t)) return "enum";
 if(type & (tm_tag_field_t|tm_tag_member_t)) return "member";
 if(type & tm_tag_local_var_t) return "local";
 return "variable";
}
static gchar *selected(Completion *c,gint column) { GtkTreeIter iter;gchar *value=NULL;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(c->view)),NULL,&iter)) gtk_tree_model_get(GTK_TREE_MODEL(c->store),&iter,column,&value,-1);return value; }
static void selection(GtkTreeSelection *unused,gpointer data) { (void)unused;Completion *c=data;gchar *origin=selected(c,ORIGIN);gchar *text=g_strdup_printf("%s%s",origin ? origin : "",c->limited ? " · Case-insensitive search limit reached" : "");gtk_label_set_text(GTK_LABEL(c->footer),text);g_free(text);g_free(origin); }
void insight_completion_search_limited(GeanyEditor *editor,gboolean limited) { Completion *c=state(editor);if(c) { c->limited=limited;selection(NULL,c); } }
static void position(Completion *c) {
 gint x=SEND(c->sci,SCI_POINTXFROMPOSITION,0,c->end),y=SEND(c->sci,SCI_POINTYFROMPOSITION,0,c->end),height=SEND(c->sci,SCI_TEXTHEIGHT,SEND(c->sci,SCI_LINEFROMPOSITION,c->end,0),0),ox,oy;
 GdkWindow *window=gtk_widget_get_window(GTK_WIDGET(c->sci));if(!window) return;gdk_window_get_origin(window,&ox,&oy);
 GdkDisplay *display=gtk_widget_get_display(GTK_WIDGET(c->sci));GdkMonitor *monitor=gdk_display_get_monitor_at_point(display,ox+x,oy+y);GdkRectangle area={ox,oy,800,600};if(monitor) gdk_monitor_get_workarea(monitor,&area);
 gint width,height_popup;gtk_window_get_size(GTK_WINDOW(c->window),&width,&height_popup);if(c->popup_width>0) width=c->popup_width;if(c->popup_height>0) height_popup=c->popup_height;x+=ox;y+=oy;
 if(y+height+height_popup>area.y+area.height) y-=height_popup;else y+=height;
 gtk_window_move(GTK_WINDOW(c->window),CLAMP(x,area.x,MAX(area.x,area.x+area.width-width)),CLAMP(y,area.y,MAX(area.y,area.y+area.height-height_popup)));
}
static gboolean valid(Completion *c,GeanyDocument **document) {
 GeanyDocument *doc=document_find_by_id(c->document);if(!c->active || !doc || doc!=document_get_current() || doc->editor->sci!=c->sci || SEND(c->sci,SCI_GETREADONLY,0,0) || SEND(c->sci,SCI_GETSELECTIONS,0,0)!=1 || sci_has_selection(c->sci) || sci_get_current_position(c->sci)!=c->end || c->start<0 || c->end<c->start) return FALSE;
 gchar *now=c->start==c->end ? g_strdup("") : sci_get_contents_range(c->sci,c->start,c->end);gboolean same=now && !g_strcmp0(now,c->prefix);g_free(now);if(document) *document=doc;return same;
}
static void accept(Completion *c) {
 GeanyDocument *doc=NULL;if(!valid(c,&doc)) { hide(c);return; }gchar *name=selected(c,NAME);if(!name) return;
 gint word_end=SEND(c->sci,SCI_WORDENDPOSITION,c->end,TRUE),end=editor_prefs.completion_drops_rest_of_word ? word_end : c->end;
 GtkTreeIter iter;gboolean callable=FALSE,snippet=FALSE;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(c->view)),NULL,&iter)) gtk_tree_model_get(GTK_TREE_MODEL(c->store),&iter,CALLABLE,&callable,SNIPPET,&snippet,-1);
 if(snippet) {guint64 generation=c->snippet_generation;hide(c);insight_workbench_completion_template_insert(doc->editor,name,c->start,c->end,generation);g_free(name);return;}
 gchar *right=end==sci_get_length(c->sci) ? g_strdup("") : sci_get_contents_range(c->sci,end,MIN((gint64)end+128,sci_get_length(c->sci)));if(strspn(right," \t")==128) callable=FALSE;gchar *suffix=insight_completion_settings_suffix(&settings,callable && end>=word_end,right),*text=g_strconcat(name,suffix,NULL);gint name_end=c->start+strlen(name),caret=c->start+strlen(text);if(*suffix) caret--;
 GArray *fields=g_array_new(FALSE,FALSE,sizeof(InsightSnippetField));gchar **parameters=NULL;if(settings.insert_parameters && *suffix && callable) gtk_tree_model_get(GTK_TREE_MODEL(c->store),&iter,PARAMETERS,&parameters,-1);
 if(parameters && parameters[0]) {GString *insert=g_string_new(settings.space_after_name ? " (":"(");for(guint n=0;parameters[n] && n<64;n++) {if(n) g_string_append(insert,settings.space_between_parameters ? ", ":",");InsightSnippetField f={strlen(name)+insert->len,0};g_string_append(insert,parameters[n]);f.end=strlen(name)+insert->len;g_array_append_val(fields,f);}g_string_append_c(insert,')');g_free(suffix);suffix=g_string_free(insert,FALSE);g_free(text);text=g_strconcat(name,suffix,NULL);caret=c->start+strlen(text);}
 g_strfreev(parameters);
 c->accepting=TRUE;hide(c);SEND(c->sci,SCI_BEGINUNDOACTION,0,0);SEND(c->sci,SCI_SETTARGETSTART,c->start,0);SEND(c->sci,SCI_SETTARGETEND,end,0);SEND(c->sci,SCI_REPLACETARGET,strlen(text),(sptr_t)text);sci_set_current_position(c->sci,caret,FALSE);if(*suffix && !fields->len) insight_filetype_pairs_remember(c->sci,name_end+strlen(suffix)-2,name_end+strlen(suffix)-1);SEND(c->sci,SCI_ENDUNDOACTION,0,0);c->accepting=FALSE;if(fields->len) insight_template_fields_begin(c->sci,fields,c->start,strlen(text));g_array_unref(fields);g_free(right);g_free(suffix);g_free(text);g_free(name);
}
static gboolean button(GtkWidget *view,GdkEventButton *event,gpointer data) {
 Completion *c=data;if(event->button!=1) return TRUE;GtkTreePath *path=NULL;if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),event->x,event->y,&path,NULL,NULL,NULL)) { gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),path);gtk_tree_path_free(path);accept(c); }return TRUE;
}
static gboolean motion(GtkWidget *view,GdkEventMotion *event,gpointer data) { (void)data;GtkTreePath *path=NULL;if(gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(view),event->x,event->y,&path,NULL,NULL,NULL)) { gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),path);gtk_tree_path_free(path); }return FALSE; }
static gboolean check_focus(gpointer data) {
 Completion *c=data;c->focus_idle=0;GtkWidget *owner=gtk_widget_get_toplevel(GTK_WIDGET(c->sci));
 if(!GTK_IS_WINDOW(owner) || gtk_window_get_focus(GTK_WINDOW(owner))!=GTK_WIDGET(c->sci)) hide(c);
 return G_SOURCE_REMOVE;
}
static gboolean focus_out(GtkWidget *widget,GdkEventFocus *event,gpointer data) { (void)widget;(void)event;Completion *c=data;if(!c->focus_idle) c->focus_idle=g_idle_add(check_focus,c);return FALSE; }
static void render(GtkTreeViewColumn *column,GtkCellRenderer *renderer,GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 (void)column;GtkWidget *view=data;gchar *markup=NULL;gtk_tree_model_get(model,iter,MARKUP,&markup,-1);
 GtkTreePath *path=gtk_tree_model_get_path(model,iter);gboolean chosen=gtk_tree_selection_path_is_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),path);gtk_tree_path_free(path);
 if(chosen) { gchar *name,*scope;gtk_tree_model_get(model,iter,NAME,&name,SCOPE,&scope,-1);g_free(markup);markup=g_markup_printf_escaped("<b>%s</b>  %s",name,scope ? scope : "");g_free(name);g_free(scope); }
 g_object_set(renderer,"markup",markup,NULL);g_free(markup);
}
GtkWidget *insight_completion_view_new(GtkListStore *store) {
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_widget_set_name(view,"insight-completion-list");gtk_widget_set_can_focus(view,FALSE);gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(view),FALSE);gtk_tree_view_set_enable_search(GTK_TREE_VIEW(view),FALSE);gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),ORIGIN);
 GtkCellRenderer *icon=gtk_cell_renderer_pixbuf_new(),*text=gtk_cell_renderer_text_new(),*type=gtk_cell_renderer_text_new();g_object_set(text,"ellipsize",PANGO_ELLIPSIZE_END,"max-width-chars",75,NULL);GtkTreeViewColumn *main=gtk_tree_view_column_new();gtk_tree_view_column_pack_start(main,icon,FALSE);gtk_tree_view_column_add_attribute(main,icon,"icon-name",ICON);gtk_tree_view_column_pack_start(main,text,TRUE);gtk_tree_view_column_set_cell_data_func(main,text,render,view,NULL);gtk_tree_view_column_set_expand(main,TRUE);gtk_tree_view_append_column(GTK_TREE_VIEW(view),main);gtk_tree_view_append_column(GTK_TREE_VIEW(view),gtk_tree_view_column_new_with_attributes("Kind",type,"text",KIND,NULL));
 return view;
}
static Completion *create(GeanyEditor *editor) {
 Completion *c=state(editor);if(c) return c;c=g_new0(Completion,1);c->sci=editor->sci;c->document=editor->document->id;
 c->window=gtk_window_new(GTK_WINDOW_POPUP);gtk_widget_set_name(c->window,"insight-completion-popup");gtk_window_set_accept_focus(GTK_WINDOW(c->window),FALSE);gtk_window_set_focus_on_map(GTK_WINDOW(c->window),FALSE);gtk_window_set_resizable(GTK_WINDOW(c->window),FALSE);gtk_window_set_type_hint(GTK_WINDOW(c->window),GDK_WINDOW_TYPE_HINT_COMBO);gtk_window_set_transient_for(GTK_WINDOW(c->window),GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(c->sci))));
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(c->window),box);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 c->scroll=scroll;
 c->store=gtk_list_store_new(COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_BOOLEAN,G_TYPE_STRV,G_TYPE_BOOLEAN);c->view=insight_completion_view_new(c->store);gtk_container_add(GTK_CONTAINER(scroll),c->view);
 c->footer=gtk_label_new("");gtk_widget_set_name(c->footer,"insight-completion-details");gtk_label_set_xalign(GTK_LABEL(c->footer),0);gtk_label_set_ellipsize(GTK_LABEL(c->footer),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),c->footer,FALSE,FALSE,3);gtk_widget_set_margin_start(box,4);gtk_widget_set_margin_end(box,4);
 g_signal_connect(c->view,"button-press-event",G_CALLBACK(button),c);g_signal_connect(c->view,"motion-notify-event",G_CALLBACK(motion),c);gtk_widget_add_events(c->view,GDK_POINTER_MOTION_MASK);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(c->view)),"changed",G_CALLBACK(selection),c);g_signal_connect(c->sci,"focus-out-event",G_CALLBACK(focus_out),c);
 g_object_set_data_full(G_OBJECT(c->sci),KEY,c,destroy);return c;
}
gboolean insight_completion_show(GeanyEditor *editor,const GPtrArray *tags,gsize rootlen) {
 load();if(!settings.detailed) return FALSE;Completion *c=create(editor);gchar *previous=c->active ? selected(c,IDENTITY) : NULL;
 c->end=sci_get_current_position(c->sci);c->start=c->end-(gint)rootlen;g_free(c->prefix);c->prefix=c->start==c->end ? g_strdup("") : sci_get_contents_range(c->sci,c->start,c->end);gtk_list_store_clear(c->store);
 GdkRGBA foreground;gtk_style_context_get_color(gtk_widget_get_style_context(c->view),GTK_STATE_FLAG_NORMAL,&foreground);gboolean dark=(foreground.red+foreground.green+foreground.blue)>1.8;const gchar *blue=dark ? "#93c5fd" : "#1d4ed8",*green=dark ? "#86efac" : "#166534",*muted=dark ? "#d1d5db" : "#475569";GtkTreePath *chosen=NULL,*exact=NULL;gboolean chosen_exact=FALSE;guint count=0;
 for(guint i=0;i<tags->len && count<editor_prefs.autocompletion_max_entries;i++) {
  TMTag *tag=g_ptr_array_index((GPtrArray *)tags,i);if(!tag->name || !g_utf8_validate(tag->name,-1,NULL) || strpbrk(tag->name,"\n\r\t")) continue;
  const gchar *args=tag->arglist ? tag->arglist : "",*scope=tag->scope ? tag->scope : "",*vartype=tag->var_type ? tag->var_type : "";gchar *identity=g_strdup_printf("%s|%s|%s|%s|%lu",tag->name,args,scope,tag->file ? tag->file->file_name : "",tag->line);
  gchar *markup=g_markup_printf_escaped("<span foreground='%s'>%s</span> <span foreground='%s'><b>%s</b></span><span foreground='%s'>%s</span> <span foreground='%s'>%s</span>",muted,vartype,blue,tag->name,green,args,muted,scope);
  gchar *description=g_strdup_printf("%s %s %s",vartype,args,scope),*origin=g_strdup_printf("%s · %s%s%s:%lu",kind(tag->type),*scope ? scope : "global",tag->file ? " · " : "",tag->file ? tag->file->file_name : "",tag->line);guint icon_id=0;gint group=tm_parser_get_sidebar_group(tag->lang,tag->type);const gchar *icon_name="text-x-generic";if(group>=0 && tm_parser_get_sidebar_info(tag->lang,group,&icon_id)) icon_name=symbols_get_icon_name(icon_id);
  GtkTreeIter iter;gtk_list_store_append(c->store,&iter);gtk_list_store_set(c->store,&iter,NAME,tag->name,MARKUP,markup,ICON,icon_name,KIND,kind(tag->type),SCOPE,description,ORIGIN,origin,IDENTITY,identity,CALLABLE,(tag->type & (tm_tag_function_t|tm_tag_prototype_t|tm_tag_method_t))!=0,-1);if(!exact && g_str_has_prefix(tag->name,c->prefix)) exact=gtk_tree_model_get_path(GTK_TREE_MODEL(c->store),&iter);if(previous && !strcmp(previous,identity)) { chosen=gtk_tree_model_get_path(GTK_TREE_MODEL(c->store),&iter);chosen_exact=g_str_has_prefix(tag->name,c->prefix); }count++;g_free(markup);g_free(description);g_free(origin);g_free(identity);
 }
 gint before=c->start;gboolean member=before>0 && sci_get_char_at(c->sci,before-1)=='.';if(before>=2) member=member || (sci_get_char_at(c->sci,before-2)=='-' && sci_get_char_at(c->sci,before-1)=='>') || (sci_get_char_at(c->sci,before-2)==':' && sci_get_char_at(c->sci,before-1)==':');
 if(settings.propose_snippets && !member) {GPtrArray *snippets=insight_workbench_completion_templates(editor,c->prefix,settings.case_sensitive,&c->snippet_generation);for(guint n=0;n<snippets->len && count<editor_prefs.autocompletion_max_entries;n++) {const gchar *name=g_ptr_array_index(snippets,n);gchar *markup=g_markup_printf_escaped("<b>%s</b>  %s",name,insight_tr("Clip template")),*identity=g_strdup_printf("snippet:%" G_GUINT64_FORMAT ":%s",c->snippet_generation,name);GtkTreeIter row;gtk_list_store_append(c->store,&row);gtk_list_store_set(c->store,&row,NAME,name,MARKUP,markup,ICON,"insert-text",KIND,insight_tr("snippet"),SCOPE,insight_tr("Clip template"),ORIGIN,insight_tr("Insert the Clips template using its parameter dialog and Undo"),IDENTITY,identity,SNIPPET,TRUE,-1);if(!exact && g_str_has_prefix(name,c->prefix)) exact=gtk_tree_model_get_path(GTK_TREE_MODEL(c->store),&row);if(previous && !strcmp(previous,identity)) {if(chosen) gtk_tree_path_free(chosen);chosen=gtk_tree_model_get_path(GTK_TREE_MODEL(c->store),&row);chosen_exact=g_str_has_prefix(name,c->prefix);}count++;g_free(markup);g_free(identity);}g_ptr_array_unref(snippets);}
 g_free(previous);if(!settings.case_sensitive && exact && !chosen_exact) { if(chosen) gtk_tree_path_free(chosen);chosen=gtk_tree_path_copy(exact); }if(exact) gtk_tree_path_free(exact);c->limited=FALSE;if(!count) {if(c->pending || c->semantic_idle) insight_completion_native_fallback(editor);else hide(c);return tags->len>0; }c->active=TRUE;if(!chosen) chosen=gtk_tree_path_new_first();gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(c->view)),chosen);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(c->view),chosen,NULL,FALSE,0,0);gtk_tree_path_free(chosen);SEND(c->sci,SCI_AUTOCCANCEL,0,0);SEND(c->sci,SCI_CALLTIPCANCEL,0,0);
 /* A GtkScrollable child's height request does not reserve viewport space.
  * Constrain the scroller itself, otherwise a non-resizable popup can shrink
  * to its footer while its model still reports every completion row. */
 GdkMonitor *monitor=gdk_display_get_monitor_at_window(gtk_widget_get_display(GTK_WIDGET(c->sci)),gtk_widget_get_window(GTK_WIDGET(c->sci)));GdkRectangle area={0,0,1000,700};if(monitor) gdk_monitor_get_workarea(monitor,&area);
 PangoContext *context=gtk_widget_get_pango_context(c->view);PangoFontMetrics *metrics=pango_context_get_metrics(context,pango_context_get_font_description(context),pango_context_get_language(context));gint row_height=MAX(26,(pango_font_metrics_get_ascent(metrics)+pango_font_metrics_get_descent(metrics)+PANGO_SCALE-1)/PANGO_SCALE+10);pango_font_metrics_unref(metrics);
 gint width=MIN(620,MAX(1,area.width-24)),height=MIN(MIN(count,(guint)settings.max_rows)*(guint)row_height,(guint)MAX(1,area.height-row_height-40));
 gtk_widget_set_size_request(c->view,-1,-1);gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(c->scroll),-1);gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(c->scroll),-1);gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(c->scroll),MAX(1,width-8));gtk_scrolled_window_set_max_content_width(GTK_SCROLLED_WINDOW(c->scroll),MAX(1,width-8));gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(c->scroll),height);gtk_scrolled_window_set_max_content_height(GTK_SCROLLED_WINDOW(c->scroll),height);
 c->popup_width=width;c->popup_height=height+row_height+12;gtk_widget_show_all(c->window);gtk_window_resize(GTK_WINDOW(c->window),width,c->popup_height);position(c);return TRUE;
}
gboolean insight_completion_key(GeanyEditor *editor,GdkEventKey *event) {
 Completion *c=state(editor);if(!c) return FALSE;if(event->keyval==GDK_KEY_Escape && (c->pending || c->semantic_idle)) {semantic_cancel(c);hide(c);SEND(c->sci,SCI_AUTOCCANCEL,0,0);return TRUE;}if(!c->active) return FALSE;if(event->state & (GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK)) { hide(c);return FALSE; }
 if(event->keyval==GDK_KEY_Escape) { hide(c);return TRUE; }
 if(event->keyval==GDK_KEY_Tab && !settings.accept_tab) { hide(c);return FALSE; }
 if(c->idle) { g_source_remove(c->idle);c->idle=0;refresh(c);if(!c->active) return FALSE; }
 if(event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter || event->keyval==GDK_KEY_Tab) { accept(c);return TRUE; }
 gint delta=0;if(event->keyval==GDK_KEY_Up) delta=-1;else if(event->keyval==GDK_KEY_Down) delta=1;else if(event->keyval==GDK_KEY_Page_Up) delta=-8;else if(event->keyval==GDK_KEY_Page_Down) delta=8;
 if(delta) { GtkTreeIter iter;GtkTreeModel *model=GTK_TREE_MODEL(c->store);gint current=0;if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(c->view)),NULL,&iter)) { GtkTreePath *p=gtk_tree_model_get_path(model,&iter);current=gtk_tree_path_get_indices(p)[0];gtk_tree_path_free(p); }gint total=gtk_tree_model_iter_n_children(model,NULL);GtkTreePath *p=gtk_tree_path_new_from_indices(CLAMP(current+delta,0,total-1),-1);gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(c->view)),p);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(c->view),p,NULL,FALSE,0,0);gtk_tree_path_free(p);return TRUE; }
 if(event->keyval==GDK_KEY_Left || event->keyval==GDK_KEY_Right || event->keyval==GDK_KEY_Home || event->keyval==GDK_KEY_End) hide(c);
 return FALSE;
}
static gboolean refresh(gpointer data) {
 Completion *c=data;c->idle=0;GeanyDocument *doc=document_find_by_id(c->document);if(!c->active || !doc || doc!=document_get_current() || doc->editor->sci!=c->sci) { hide(c);return G_SOURCE_REMOVE; }
 const InsightFiletypeOptions *options=insight_languages_editing_options(doc->editor);if(!settings.detailed || (options && !options->allow_completion) || !editor_prefs.auto_complete_symbols || sci_has_selection(c->sci) || sci_get_current_position(c->sci)!=c->end) { hide(c);return G_SOURCE_REMOVE; }
 if(!editor_start_auto_complete(doc->editor,c->end,FALSE)) hide(c);
 return G_SOURCE_REMOVE;
}
void insight_completion_notify(GeanyEditor *editor,SCNotification *nt) {
 Completion *c=state(editor);if(!c || c->accepting) return;if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType & (SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) semantic_cancel(c);if(!c->active) return;
 if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType & (SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) {
  if((nt->modificationType & (SC_PERFORMED_UNDO|SC_PERFORMED_REDO)) || nt->position<c->start || nt->position>c->end || nt->length>4096) { hide(c);return; }
  c->end+= (nt->modificationType & SC_MOD_INSERTTEXT) ? nt->length : -nt->length;if(!c->idle) c->idle=g_idle_add(refresh,c);
 } else if(nt->nmhdr.code==SCN_UPDATEUI && !c->idle && (nt->updated & SC_UPDATE_SELECTION) && sci_get_current_position(c->sci)!=c->end) hide(c);
 else if(nt->nmhdr.code==SCN_FOCUSOUT) focus_out(NULL,NULL,c);
 else if(nt->nmhdr.code==SCN_UPDATEUI && (nt->updated & (SC_UPDATE_V_SCROLL|SC_UPDATE_H_SCROLL))) hide(c);
}
typedef struct {GWeakRef sci;guint document,serial,offset,rootlen;gchar *path,*text,*root,*resources,*project;GHashTable *buffers;GPtrArray *results;} SemanticJob;
static void semantic_job_free(gpointer data) {SemanticJob *j=data;g_weak_ref_clear(&j->sci);g_free(j->path);g_free(j->text);g_free(j->root);g_free(j->resources);g_free(j->project);g_hash_table_unref(j->buffers);if(j->results) g_ptr_array_unref(j->results);g_free(j);}
static void semantic_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {(void)source;SemanticJob *j=data;j->results=insight_clang_complete(j->path,j->text,j->offset,j->root,j->resources,j->buffers,cancel,NULL);g_task_return_boolean(task,j->results!=NULL);}
static void semantic_done(GObject *source,GAsyncResult *result,gpointer unused) {
 (void)source;(void)unused;SemanticJob *j=g_task_get_task_data(G_TASK(result));if(semantic_workers) semantic_workers--;gboolean ok=g_task_propagate_boolean(G_TASK(result),NULL);ScintillaObject *sci=g_weak_ref_get(&j->sci);if(!sci) return;Completion *c=g_object_get_data(G_OBJECT(sci),KEY);GeanyDocument *doc=document_find_by_id(j->document);
 if(!c || c->semantic_serial!=j->serial || !DOC_VALID(doc) || doc!=document_get_current() || doc->editor->sci!=sci || g_strcmp0(doc->file_name,j->path) || g_strcmp0(j->project,app->project ? app->project->file_name:NULL) || sci_get_current_position(sci)!=(gint)j->offset || sci_has_selection(sci) || SEND(sci,SCI_GETREADONLY,0,0) || SEND(sci,SCI_GETSELECTIONS,0,0)!=1) {g_object_unref(sci);return;}
 g_clear_object(&c->pending);gchar *now=sci_get_contents(sci,sci_get_length(sci)+1);gboolean same=!strcmp(now,j->text);g_free(now);
 GHashTableIter overlays;gpointer path,text;g_hash_table_iter_init(&overlays,j->buffers);while(same && g_hash_table_iter_next(&overlays,&path,&text)) {GeanyDocument *d=document_find_by_filename(path);if(!DOC_VALID(d)) {same=FALSE;break;}gchar *contents=sci_get_contents(d->editor->sci,sci_get_length(d->editor->sci)+1);same=!strcmp(contents,text);g_free(contents);}
 if(ok && same && settings.detailed && settings.semantic) {
  GPtrArray *tags=g_ptr_array_new_with_free_func((GDestroyNotify)tm_tag_unref),*chosen=g_ptr_array_new();gchar *prefix=g_strndup(j->text+j->offset-j->rootlen,j->rootlen),*folded=g_utf8_casefold(prefix,-1);
  for(guint n=0;n<j->results->len && tags->len<editor_prefs.autocompletion_max_entries;n++) {InsightClangCompletion *item=g_ptr_array_index(j->results,n);gchar *candidate=g_utf8_casefold(item->name,-1);gboolean match=g_str_has_prefix(settings.case_sensitive ? item->name:candidate,settings.case_sensitive ? prefix:folded);g_free(candidate);if(!match) continue;
   TMTag *tag=tm_tag_new();tag->name=g_strdup(item->name);tag->arglist=g_strdup(g_str_has_prefix(item->signature,item->name) ? item->signature+strlen(item->name):item->signature);tag->var_type=g_strdup(item->type);tag->lang=doc->tm_file ? doc->tm_file->lang:0;tag->type=item->callable ? tm_tag_function_t:tm_tag_member_t;tag->line=n+1;g_ptr_array_add(tags,tag);g_ptr_array_add(chosen,item);
  }g_free(prefix);g_free(folded);
  insight_completion_show(doc->editor,tags,j->rootlen);GtkTreeIter row;gboolean valid=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(c->store),&row);
  for(guint n=0;valid && n<chosen->len;n++,valid=gtk_tree_model_iter_next(GTK_TREE_MODEL(c->store),&row)) {InsightClangCompletion *item=g_ptr_array_index(chosen,n);gchar **parameters=g_new0(gchar *,item->parameters->len+1);for(guint k=0;k<item->parameters->len;k++) parameters[k]=g_ptr_array_index(item->parameters,k);gchar *origin=g_strdup_printf("Clang • %s%s%s",item->signature,*item->comment ? " • ":"",item->comment);gtk_list_store_set(c->store,&row,ORIGIN,origin,PARAMETERS,parameters,-1);g_free(origin);g_free(parameters);}
  selection(NULL,c);g_object_set_data(G_OBJECT(sci),"insight-semantic-completion-count",GUINT_TO_POINTER(tags->len));g_ptr_array_unref(tags);g_ptr_array_unref(chosen);
 }else if(same && !ok) {c->bypass=TRUE;editor_start_auto_complete(doc->editor,j->offset,FALSE);c->bypass=FALSE;}g_object_unref(sci);
}
static gboolean semantic_start(gpointer data) {
 Completion *c=data;if(semantic_workers>=2) return G_SOURCE_CONTINUE;c->semantic_idle=0;GeanyDocument *doc=document_find_by_id(c->document);if(!DOC_VALID(doc) || doc!=document_get_current() || !settings.semantic || !settings.detailed) return G_SOURCE_REMOVE;gint length=sci_get_length(c->sci),offset=sci_get_current_position(c->sci);if(length<0 || length>2*1024*1024 || offset<(gint)c->semantic_root || sci_has_selection(c->sci)) return G_SOURCE_REMOVE;
 SemanticJob *j=g_new0(SemanticJob,1);g_weak_ref_init(&j->sci,c->sci);j->serial=c->semantic_serial;j->document=doc->id;j->offset=offset;j->rootlen=c->semantic_root;j->path=g_strdup(doc->file_name);j->text=sci_get_contents(c->sci,length+1);j->project=g_strdup(app->project ? app->project->file_name:NULL);j->resources=g_build_filename(app->datadir,"insight","clang","resources",NULL);gchar *native=app->project ? project_get_base_path():NULL;j->root=native ? utils_get_utf8_from_locale(native):g_path_get_dirname(doc->file_name);g_free(native);j->buffers=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
 gsize total=0;for(guint i=0;i<documents_array->len;i++) {GeanyDocument *d=g_ptr_array_index(documents_array,i);if(!DOC_VALID(d) || !d->file_name) continue;gint n=sci_get_length(d->editor->sci);if(n<0 || n>2*1024*1024 || total+(gsize)n>32u*1024u*1024u || g_hash_table_size(j->buffers)>=256) continue;gchar *text=sci_get_contents(d->editor->sci,n+1);if(!memchr(text,0,n) && g_utf8_validate(text,n,NULL)) {g_hash_table_insert(j->buffers,g_strdup(d->file_name),text);total+=n;}else g_free(text);}
 if(memchr(j->text,0,length) || !g_utf8_validate(j->text,length,NULL)) {semantic_job_free(j);return G_SOURCE_REMOVE;}
 semantic_workers++;c->pending=g_cancellable_new();GTask *task=g_task_new(NULL,c->pending,semantic_done,NULL);g_task_set_task_data(task,j,semantic_job_free);g_task_run_in_thread(task,semantic_run);g_object_unref(task);return G_SOURCE_REMOVE;
}
gboolean insight_completion_semantic(GeanyEditor *editor,gsize rootlen,gboolean force) {
 load();GeanyDocument *doc=editor ? editor->document:NULL;Completion *existing=state(editor);if((existing && existing->bypass) || !settings.semantic || !settings.detailed || !insight_clang_available() || !DOC_VALID(doc) || !doc->file_name || !insight_clang_source(doc->file_name) || SEND(editor->sci,SCI_GETREADONLY,0,0) || SEND(editor->sci,SCI_GETSELECTIONS,0,0)!=1 || sci_has_selection(editor->sci)) return FALSE;
 gint pos=sci_get_current_position(editor->sci)-(gint)rootlen;gboolean member=pos>0 && sci_get_char_at(editor->sci,pos-1)=='.';if(pos>=2) member=member || (sci_get_char_at(editor->sci,pos-2)=='-' && sci_get_char_at(editor->sci,pos-1)=='>') || (sci_get_char_at(editor->sci,pos-2)==':' && sci_get_char_at(editor->sci,pos-1)==':');if(!member && !force) return FALSE;
 Completion *c=create(editor);semantic_cancel(c);c->semantic_root=rootlen;c->semantic_idle=g_timeout_add(150,semantic_start,c);return member;
}
static void close_all(void) { for(guint i=0;i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(doc->is_valid) insight_completion_cancel(doc->editor); } }
static gboolean save(const InsightCompletionSettings *next,GError **error) {
 GKeyFile *key=g_key_file_new();gchar *path=config_path();g_key_file_load_from_file(key,path,0,NULL);gboolean ok=insight_completion_settings_write(next,key,error);gsize length=0;gchar *text=ok ? g_key_file_to_data(key,&length,NULL) : NULL;if(ok) ok=g_file_set_contents(path,text,length,error);if(ok) { settings=*next;close_all(); }g_free(text);g_free(path);g_key_file_unref(key);return ok;
}
static void toggle(GtkCheckMenuItem *item,gpointer data) {
 (void)data;InsightCompletionSettings next=settings;next.detailed=gtk_check_menu_item_get_active(item);GError *error=NULL;if(!save(&next,&error)) { g_signal_handlers_block_by_func(item,toggle,NULL);gtk_check_menu_item_set_active(item,settings.detailed);g_signal_handlers_unblock_by_func(item,toggle,NULL);g_warning("Cannot save completion settings: %s",error->message);g_clear_error(&error); }
}
static void preferences(GtkWidget *unused,gpointer data) {
 (void)unused;(void)data;load();close_all();GtkWidget *dialog=gtk_dialog_new_with_buttons("Completion options",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel",GTK_RESPONSE_CANCEL,"Apply",GTK_RESPONSE_APPLY,"OK",GTK_RESPONSE_OK,NULL);gtk_widget_set_name(dialog,"insight-completion-options-dialog");GtkWidget *page=insight_completion_preferences_new(&settings),*status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),page,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),status,FALSE,FALSE,4);gtk_widget_show_all(dialog);
 gint response;while((response=gtk_dialog_run(GTK_DIALOG(dialog)))==GTK_RESPONSE_OK || response==GTK_RESPONSE_APPLY) { InsightCompletionSettings next;insight_completion_preferences_get(page,&next);GError *error=NULL;if(save(&next,&error)) { if(detail_item) { g_signal_handlers_block_by_func(detail_item,toggle,NULL);gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(detail_item),settings.detailed);g_signal_handlers_unblock_by_func(detail_item,toggle,NULL); }gtk_label_set_text(GTK_LABEL(status),"Completion settings applied.");if(response==GTK_RESPONSE_OK) break; }else { gtk_label_set_text(GTK_LABEL(status),error ? error->message : "Cannot save completion settings");g_clear_error(&error); } }gtk_widget_destroy(dialog);
}
void insight_completion_menu(GtkWidget *menu) { load();GtkWidget *item=gtk_check_menu_item_new_with_label("Detailed completion window");detail_item=item;gtk_widget_set_name(item,"insight-detailed-completion-option");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),settings.detailed);g_signal_connect(item,"toggled",G_CALLBACK(toggle),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);GtkWidget *options=gtk_menu_item_new_with_label("Completion options…");gtk_widget_set_name(options,"insight-completion-options");g_signal_connect(options,"activate",G_CALLBACK(preferences),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),options); }
