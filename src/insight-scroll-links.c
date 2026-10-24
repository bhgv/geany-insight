/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-scroll-links.h"
#include "document.h"
#include "editor.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-help.h"
#include "insight-i18n.h"
#define VIEW_SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define PAIR_LIMIT 64
#define ALL_LIMIT 128

typedef struct { guint source,target;gboolean two_way; } ScrollPair;
typedef struct { gint64 row,x; } ViewSample;
static GArray *link_pairs;
static GHashTable *view_samples;
static gboolean link_all,vertical=TRUE,horizontal,propagating,ui_loading;
static GtkWidget *source_choice,*links_view,*remove_link;
static GtkWidget *all_menu,*link_dialog,*source_label,*target_choice,*two_way,*all_check,*vertical_check,*horizontal_check,*links_status;
static GtkListStore *links_model;
static guint dialog_source;
static gboolean target_closed;

static ViewSample sample_view(GeanyDocument *doc) {
 return (ViewSample){VIEW_SEND(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0),VIEW_SEND(doc->editor->sci,SCI_GETXOFFSET,0,0)};
}
static void cache_view(GeanyDocument *doc) {
 ViewSample *sample=g_hash_table_lookup(view_samples,GUINT_TO_POINTER(doc->id));
 if(!sample) { sample=g_new(ViewSample,1);g_hash_table_insert(view_samples,GUINT_TO_POINTER(doc->id),sample); }
 *sample=sample_view(doc);
}
static void reset_baselines(void) {
 g_hash_table_remove_all(view_samples);
 for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(DOC_VALID(doc)) cache_view(doc); }
}
static guint open_count(void) {
 guint count=0;for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(DOC_VALID(doc)) count++; }return count;
}
static guint selected_target(void) {
 const gchar *id=target_choice ? gtk_combo_box_get_active_id(GTK_COMBO_BOX(target_choice)):NULL;
 if(!id) return 0;gchar *end=NULL;guint64 value=g_ascii_strtoull(id,&end,10);return end && !*end && value<=G_MAXUINT ? (guint)value:0;
}
static gchar *document_label(guint id) {
 GeanyDocument *doc=document_find_by_id(id);if(!DOC_VALID(doc)) return g_strdup(insight_tr("Closed source"));
 return doc->file_name ? g_strdup(doc->file_name):g_strdup_printf("%s #%u",insight_tr("Untitled"),id);
}
static gboolean selected_pair(guint *source,guint *target) {
 if(!links_view) return FALSE;GtkTreeIter row;GtkTreeModel *model;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(links_view)),&model,&row)) return FALSE;gtk_tree_model_get(model,&row,3,source,4,target,-1);return TRUE;
}
static void refresh_controls(void) {
 gboolean loading=ui_loading;ui_loading=TRUE;gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(all_menu),link_all);
 if(link_dialog) {
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(all_check),link_all);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(vertical_check),vertical);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(horizontal_check),horizontal);
  if(g_object_get_data(G_OBJECT(link_dialog),"insight-links-rebuild")) {
   guint selected_source=0,selected_destination=0;selected_pair(&selected_source,&selected_destination);gboolean current=FALSE;for(guint i=0;i<link_pairs->len;i++) { ScrollPair *pair=&g_array_index(link_pairs,ScrollPair,i);if(pair->source==dialog_source && pair->target==selected_target()) current=TRUE; }gtk_list_store_clear(links_model);
   for(guint n=0;n<link_pairs->len;n++) { ScrollPair *pair=&g_array_index(link_pairs,ScrollPair,n);gchar *a=document_label(pair->source),*b=document_label(pair->target);GtkTreeIter row;gtk_list_store_append(links_model,&row);gtk_list_store_set(links_model,&row,0,a,1,pair->two_way ? "↔":"→",2,b,3,pair->source,4,pair->target,5,pair->two_way,-1);g_free(a);g_free(b);if(current ? (pair->source==dialog_source && pair->target==selected_target()) : (pair->source==selected_source && pair->target==selected_destination)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(links_view)),&row); }
   g_object_set_data(G_OBJECT(link_dialog),"insight-links-rebuild",NULL);
  }
  gchar *status=insight_i18n_printf(link_all ? "All source windows linked — individual links: %u / %u":"Individual source links: %u / %u",link_pairs->len,PAIR_LIMIT);if(!vertical && !horizontal) { gchar *paused=g_strconcat(insight_tr("Scrolling paused — enable a scrolling axis")," • ",status,NULL);g_free(status);status=paused; }gtk_label_set_text(GTK_LABEL(links_status),status);g_free(status);
  guint target=selected_target(),a,b;gboolean valid=DOC_VALID(document_find_by_id(dialog_source)) && !target_closed && (!target || (target!=dialog_source && DOC_VALID(document_find_by_id(target))));gtk_dialog_set_response_sensitive(GTK_DIALOG(link_dialog),GTK_RESPONSE_APPLY,valid);gtk_widget_set_sensitive(remove_link,selected_pair(&a,&b));
 }
 ui_loading=loading;
}
static void links_changed(void) { if(link_dialog) g_object_set_data(G_OBJECT(link_dialog),"insight-links-rebuild",GINT_TO_POINTER(1));refresh_controls(); }
static void set_all(gboolean active) {
 if(active && open_count()>ALL_LIMIT) { ui_set_statusbar(TRUE,"Link All supports at most 128 open source buffers");refresh_controls();return; }
 link_all=active;reset_baselines();refresh_controls();
}
static void clear_links(void) { g_array_set_size(link_pairs,0);link_all=FALSE;g_hash_table_remove_all(view_samples);links_changed(); }
static void add_reachable(GArray *ids,GHashTable *seen,guint id) {
 if(!g_hash_table_contains(seen,GUINT_TO_POINTER(id)) && DOC_VALID(document_find_by_id(id))) { g_hash_table_add(seen,GUINT_TO_POINTER(id));g_array_append_val(ids,id); }
}
static void propagate_scroll(guint source,gint64 rows,gint64 pixels) {
 GArray *ids=g_array_new(FALSE,FALSE,sizeof(guint));GHashTable *seen=g_hash_table_new(g_direct_hash,g_direct_equal);add_reachable(ids,seen,source);
 if(link_all) {
  if(open_count()>ALL_LIMIT) { link_all=FALSE;refresh_controls();ui_set_statusbar(TRUE,"Link All disabled: more than 128 source buffers are open"); }
  else for(guint n=0;n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(DOC_VALID(doc)) add_reachable(ids,seen,doc->id); }
 }
 if(!link_all) for(guint at=0;at<ids->len;at++) {
  guint id=g_array_index(ids,guint,at);
  for(guint n=0;n<link_pairs->len;n++) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n);if(pair.source==id) add_reachable(ids,seen,pair.target);if(pair.two_way && pair.target==id) add_reachable(ids,seen,pair.source); }
 }
 propagating=TRUE;
 for(guint n=1;n<ids->len;n++) {
  GeanyDocument *doc=document_find_by_id(g_array_index(ids,guint,n));if(!DOC_VALID(doc)) continue;ViewSample before=sample_view(doc);
  if(rows) VIEW_SEND(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,(uptr_t)CLAMP(before.row+rows,(gint64)0,(gint64)G_MAXINT),0);
  if(pixels) VIEW_SEND(doc->editor->sci,SCI_SETXOFFSET,(uptr_t)CLAMP(before.x+pixels,(gint64)0,(gint64)G_MAXINT),0);
  cache_view(doc);
 }
 propagating=FALSE;g_hash_table_unref(seen);g_array_unref(ids);
}
void insight_scroll_links_notify(GeanyEditor *editor,SCNotification *nt) {
 if(!view_samples || !link_pairs || (!link_all && !link_pairs->len) || !editor || !DOC_VALID(editor->document) || nt->nmhdr.code!=SCN_UPDATEUI) return;
 GeanyDocument *doc=editor->document;if(editor->sci!=doc->editor->sci) return;
 ViewSample now=sample_view(doc),*previous=g_hash_table_lookup(view_samples,GUINT_TO_POINTER(doc->id));
 if(!previous) { cache_view(doc);return; }
 ViewSample old=*previous;*previous=now;if(propagating) return;
 gint64 rows=vertical && (nt->updated&SC_UPDATE_V_SCROLL) ? now.row-old.row:0,pixels=horizontal && (nt->updated&SC_UPDATE_H_SCROLL) ? now.x-old.x:0;
 if((rows || pixels) && (link_all || link_pairs->len)) propagate_scroll(doc->id,rows,pixels);
}
static void target_changed(GtkComboBox *combo,gpointer data) { (void)combo;(void)data;if(!ui_loading) { target_closed=FALSE;refresh_controls(); } }
static void all_menu_changed(GtkCheckMenuItem *item,gpointer data) { (void)data;if(!ui_loading) set_all(gtk_check_menu_item_get_active(item)); }
static void all_dialog_changed(GtkToggleButton *item,gpointer data) { (void)data;if(!ui_loading) set_all(gtk_toggle_button_get_active(item)); }
static void axes_changed(GtkToggleButton *item,gpointer data) {
 (void)item;(void)data;if(ui_loading) return;vertical=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vertical_check));horizontal=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(horizontal_check));reset_baselines();refresh_controls();
}
static void populate_sources(guint skip) {
 gboolean loading=ui_loading;ui_loading=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(source_choice));
 for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(!DOC_VALID(doc) || doc->id==skip) continue;gchar *id=g_strdup_printf("%u",doc->id),*label=document_label(doc->id);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(source_choice),id,label);g_free(id);g_free(label); }
 gchar *id=g_strdup_printf("%u",dialog_source);gtk_combo_box_set_active_id(GTK_COMBO_BOX(source_choice),id);g_free(id);ui_loading=loading;
}
static void populate_target(gboolean preserve,guint skip) {
 guint selected=preserve ? selected_target():0;gboolean reverse=preserve && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(two_way));gboolean loading=ui_loading;ui_loading=TRUE;
 if(!preserve) for(guint n=0;n<link_pairs->len;n++) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n);if(pair.source==dialog_source) { selected=pair.target;reverse=pair.two_way;break; } }
 gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(target_choice));gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_choice),"0",insight_tr("None — unlink this source"));
 for(guint n=0;documents_array && n<documents_array->len;n++) { GeanyDocument *target=g_ptr_array_index(documents_array,n);if(!DOC_VALID(target) || target->id==dialog_source || target->id==skip) continue;gchar *id=g_strdup_printf("%u",target->id),*label=document_label(target->id);gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(target_choice),id,label);g_free(id);g_free(label); }
 gchar *id=g_strdup_printf("%u",selected),*label=dialog_source ? document_label(dialog_source):g_strdup(insight_tr("Source closed — Refresh to choose the current editor"));if(!gtk_combo_box_set_active_id(GTK_COMBO_BOX(target_choice),id)) gtk_combo_box_set_active_id(GTK_COMBO_BOX(target_choice),"0");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(two_way),reverse);gtk_label_set_text(GTK_LABEL(source_label),label);gtk_widget_set_tooltip_text(source_label,label);g_free(id);g_free(label);ui_loading=loading;
}
static void capture_current(void) { GeanyDocument *doc=document_get_current();dialog_source=DOC_VALID(doc) ? doc->id:0;target_closed=FALSE;populate_sources(0);populate_target(FALSE,0);links_changed(); }
static void source_changed(GtkComboBox *combo,gpointer data) {
 (void)data;if(ui_loading) return;const gchar *id=gtk_combo_box_get_active_id(combo);dialog_source=id ? (guint)g_ascii_strtoull(id,NULL,10):0;target_closed=FALSE;populate_target(FALSE,0);refresh_controls();
}
static void pair_selected(GtkTreeSelection *selection,gpointer data) {
 (void)selection;(void)data;if(ui_loading) return;guint source,target;if(selected_pair(&source,&target)) { dialog_source=source;target_closed=FALSE;populate_sources(0);populate_target(FALSE,0); }refresh_controls();
}
static void remove_selected(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;guint source,target;if(!selected_pair(&source,&target)) return;for(guint n=0;n<link_pairs->len;n++) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n);if(pair.source==source && pair.target==target) { g_array_remove_index(link_pairs,n);break; } }reset_baselines();populate_target(FALSE,0);links_changed();
}
static void locale_changed(gpointer data) { (void)data;if(!link_dialog) return;populate_sources(0);populate_target(TRUE,0);links_changed(); }
static void apply_pair(void) {
 guint target=selected_target();if(!DOC_VALID(document_find_by_id(dialog_source)) || target_closed || (target && (target==dialog_source || !DOC_VALID(document_find_by_id(target))))) { gtk_label_set_text(GTK_LABEL(links_status),insight_tr("Source or target closed; Refresh before linking"));gtk_dialog_set_response_sensitive(GTK_DIALOG(link_dialog),GTK_RESPONSE_APPLY,FALSE);return; }
 gint existing=-1;for(guint n=0;n<link_pairs->len;n++) if(g_array_index(link_pairs,ScrollPair,n).source==dialog_source) { existing=(gint)n;break; }
 if(target && existing<0 && link_pairs->len>=PAIR_LIMIT) { gtk_label_set_text(GTK_LABEL(links_status),insight_tr("At most 64 individual source links are supported"));return; }
 if(existing>=0) g_array_remove_index(link_pairs,(guint)existing);
 if(target) { ScrollPair pair={dialog_source,target,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(two_way))};g_array_append_val(link_pairs,pair); }
 reset_baselines();links_changed();
}
static void dialog_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;link_dialog=NULL;source_choice=links_view=remove_link=NULL;source_label=target_choice=two_way=all_check=vertical_check=horizontal_check=links_status=NULL;g_clear_object(&links_model); }
static void dialog_response(GtkDialog *dialog,gint response,gpointer data) {
 (void)data;if(response==GTK_RESPONSE_APPLY) apply_pair();else if(response==GTK_RESPONSE_HELP) capture_current();else if(response==1) clear_links();else gtk_widget_destroy(GTK_WIDGET(dialog));
}
static void show_dialog(void) {
 if(!link_dialog) {
  link_dialog=gtk_dialog_new_with_buttons("Link Source Windows",GTK_WINDOW(main_widgets.window),GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CLOSE,"Refresh",GTK_RESPONSE_HELP,"Unlink All",1,"Apply Link",GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(link_dialog,"insight-scroll-links-dialog");gtk_window_set_default_size(GTK_WINDOW(link_dialog),730,480);gtk_window_set_resizable(GTK_WINDOW(link_dialog),TRUE);
  GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(link_dialog)),*row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_box_set_spacing(GTK_BOX(box),5);
  GtkWidget *description=gtk_label_new(insight_tr("Linked sources scroll together; their text, caret and selection remain independent."));gtk_label_set_line_wrap(GTK_LABEL(description),TRUE);gtk_label_set_xalign(GTK_LABEL(description),0);gtk_box_pack_start(GTK_BOX(box),insight_panel_help_overlay(description,"scroll-links.md"),FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(insight_tr("Source")),FALSE,FALSE,0);source_choice=gtk_combo_box_text_new();gtk_widget_set_name(source_choice,"insight-scroll-links-source-choice");g_object_set_data(G_OBJECT(source_choice),"insight-no-i18n",GINT_TO_POINTER(1));insight_i18n_key(G_OBJECT(source_choice),"tooltip-text","Choose the source buffer without changing the active editor.");GList *source_cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(source_choice));for(GList *c=source_cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"width-chars",1,NULL);g_list_free(source_cells);gtk_box_pack_start(GTK_BOX(row),source_choice,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
  source_label=gtk_label_new("");gtk_widget_set_name(source_label,"insight-scroll-links-source");g_object_set_data(G_OBJECT(source_label),"insight-no-i18n",GINT_TO_POINTER(1));gtk_label_set_selectable(GTK_LABEL(source_label),TRUE);gtk_label_set_ellipsize(GTK_LABEL(source_label),PANGO_ELLIPSIZE_MIDDLE);gtk_box_pack_start(GTK_BOX(box),source_label,FALSE,FALSE,0);
  gtk_box_pack_start(GTK_BOX(row),gtk_label_new(insight_tr("Target")),FALSE,FALSE,0);target_choice=gtk_combo_box_text_new();g_object_set_data(G_OBJECT(target_choice),"insight-no-i18n",GINT_TO_POINTER(1));insight_i18n_key(G_OBJECT(target_choice),"tooltip-text","Choose the target buffer; None removes this source's outgoing link.");gtk_widget_set_name(target_choice,"insight-scroll-links-target");GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(target_choice));for(GList *c=cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,"width-chars",1,NULL);g_list_free(cells);two_way=gtk_check_button_new_with_label("Two Way");gtk_widget_set_name(two_way,"insight-scroll-links-two-way");gtk_widget_set_tooltip_text(two_way,insight_tr("Two Way lets either file drive scrolling; otherwise only the source drives the target."));gtk_box_pack_start(GTK_BOX(row),target_choice,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(row),two_way,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
  row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);all_check=gtk_check_button_new_with_label("Link All Source Windows");vertical_check=gtk_check_button_new_with_label("Vertical scrolling");horizontal_check=gtk_check_button_new_with_label("Horizontal scrolling");GtkWidget *options[]={all_check,vertical_check,horizontal_check};const gchar *names[]={"insight-scroll-links-all","insight-scroll-links-vertical","insight-scroll-links-horizontal"};for(guint n=0;n<3;n++) { gtk_widget_set_name(options[n],names[n]);gtk_box_pack_start(GTK_BOX(row),options[n],FALSE,FALSE,0); }gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);gtk_widget_set_tooltip_text(gtk_dialog_get_widget_for_response(GTK_DIALOG(link_dialog),GTK_RESPONSE_HELP),insight_tr("Use the current editor as source and refresh both file lists."));
  links_model=gtk_list_store_new(6,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_BOOLEAN);GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(links_model)),*scroll=gtk_scrolled_window_new(NULL,NULL);links_view=view;gtk_widget_set_name(view,"insight-scroll-links-list");gtk_widget_set_tooltip_text(view,insight_tr("Select a link to edit its source, target and direction, then Apply Link."));g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),"changed",G_CALLBACK(pair_selected),NULL);const gchar *titles[]={"Source","Direction","Target"};for(guint n=0;n<3;n++) { GtkCellRenderer *cell=gtk_cell_renderer_text_new();g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,NULL);GtkTreeViewColumn *col=gtk_tree_view_column_new_with_attributes(titles[n],cell,"text",n,NULL);gtk_tree_view_column_set_resizable(col,TRUE);gtk_tree_view_column_set_expand(col,n!=1);gtk_tree_view_append_column(GTK_TREE_VIEW(view),col); }gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_AUTOMATIC,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
  remove_link=gtk_button_new_with_label(insight_tr("Remove selected link"));gtk_widget_set_name(remove_link,"insight-scroll-links-remove-selected");gtk_widget_set_tooltip_text(remove_link,insight_tr("Remove only the selected pair; other links remain active."));gtk_box_pack_start(GTK_BOX(box),remove_link,FALSE,FALSE,0);g_signal_connect(remove_link,"clicked",G_CALLBACK(remove_selected),NULL);
  links_status=gtk_label_new("");gtk_widget_set_name(links_status,"insight-scroll-links-status");gtk_label_set_line_wrap(GTK_LABEL(links_status),TRUE);gtk_box_pack_start(GTK_BOX(box),links_status,FALSE,FALSE,0);insight_i18n_on_refresh(G_OBJECT(link_dialog),locale_changed,NULL);
  g_signal_connect(source_choice,"changed",G_CALLBACK(source_changed),NULL);g_signal_connect(target_choice,"changed",G_CALLBACK(target_changed),NULL);g_signal_connect(all_check,"toggled",G_CALLBACK(all_dialog_changed),NULL);g_signal_connect(vertical_check,"toggled",G_CALLBACK(axes_changed),NULL);g_signal_connect(horizontal_check,"toggled",G_CALLBACK(axes_changed),NULL);g_signal_connect(link_dialog,"response",G_CALLBACK(dialog_response),NULL);g_signal_connect(link_dialog,"destroy",G_CALLBACK(dialog_destroyed),NULL);
 }
 capture_current();gtk_widget_show_all(link_dialog);gtk_window_present(GTK_WINDOW(link_dialog));
}
void insight_scroll_links_command(guint action) { if(!link_pairs) return;if(action==0) show_dialog();else if(action==1) set_all(!link_all);else if(action==2) clear_links(); }
static void menu_command(GtkWidget *widget,gpointer data) { (void)widget;insight_scroll_links_command(GPOINTER_TO_UINT(data)); }
static void document_opened(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(!view_samples || !DOC_VALID(doc)) return;g_hash_table_remove(view_samples,GUINT_TO_POINTER(doc->id));
 if(link_all && open_count()>ALL_LIMIT) { set_all(FALSE);ui_set_statusbar(TRUE,"Link All disabled: more than 128 source buffers are open"); }
 if(link_dialog) { populate_sources(0);populate_target(TRUE,0);refresh_controls(); }
}
static void document_saved(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(!view_samples || !DOC_VALID(doc)) return;cache_view(doc);if(link_dialog) { populate_sources(0);populate_target(TRUE,0);links_changed(); }
}
static void document_closed(GObject *object,GeanyDocument *doc,gpointer data) {
 (void)object;(void)data;if(!view_samples) return;g_hash_table_remove(view_samples,GUINT_TO_POINTER(doc->id));
 for(guint n=link_pairs->len;n>0;n--) { ScrollPair pair=g_array_index(link_pairs,ScrollPair,n-1);if(pair.source==doc->id || pair.target==doc->id) g_array_remove_index(link_pairs,n-1); }
 if(link_dialog) { if(dialog_source==doc->id) { dialog_source=0;gtk_label_set_text(GTK_LABEL(source_label),insight_tr("Source closed — Refresh to choose the current editor")); }if(selected_target()==doc->id) target_closed=TRUE; }
 if(link_dialog) { populate_sources(doc->id);populate_target(TRUE,doc->id); }links_changed();
}
static void menu_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;ui_loading=TRUE;if(link_dialog) gtk_widget_destroy(link_dialog);link_all=FALSE;g_clear_pointer(&link_pairs,g_array_unref);g_clear_pointer(&view_samples,g_hash_table_unref);all_menu=NULL;ui_loading=FALSE; }
void insight_scroll_links_init(GtkWidget *menu) {
 link_pairs=g_array_new(FALSE,FALSE,sizeof(ScrollPair));view_samples=g_hash_table_new_full(g_direct_hash,g_direct_equal,NULL,g_free);
 GtkWidget *item=gtk_menu_item_new_with_label("Link Source Windows…");gtk_widget_set_name(item,"insight-scroll-links-command");g_object_set_data(G_OBJECT(item),"insight-window-persistent",GINT_TO_POINTER(1));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_LINKWINDOW,item);g_signal_connect(item,"activate",G_CALLBACK(menu_command),GUINT_TO_POINTER(0));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 all_menu=gtk_check_menu_item_new_with_label("Link All Source Windows");gtk_widget_set_name(all_menu,"insight-scroll-links-all-command");g_object_set_data(G_OBJECT(all_menu),"insight-window-persistent",GINT_TO_POINTER(1));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_LINKALLWINDOWS,all_menu);g_signal_connect(all_menu,"toggled",G_CALLBACK(all_menu_changed),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),all_menu);gtk_widget_show(all_menu);
 item=gtk_menu_item_new_with_label("Unlink All Source Windows");gtk_widget_set_name(item,"insight-scroll-links-clear-command");g_object_set_data(G_OBJECT(item),"insight-window-persistent",GINT_TO_POINTER(1));keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_UNLINKWINDOWS,item);g_signal_connect(item,"activate",G_CALLBACK(menu_command),GUINT_TO_POINTER(2));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
 GtkWidget *separator=gtk_separator_menu_item_new();g_object_set_data(G_OBJECT(separator),"insight-window-persistent",GINT_TO_POINTER(1));gtk_menu_shell_append(GTK_MENU_SHELL(menu),separator);gtk_widget_show(separator);
 g_signal_connect_object(geany_object,"document-new",G_CALLBACK(document_opened),all_menu,G_CONNECT_AFTER);g_signal_connect_object(geany_object,"document-open",G_CALLBACK(document_opened),all_menu,G_CONNECT_AFTER);g_signal_connect_object(geany_object,"document-close",G_CALLBACK(document_closed),all_menu,0);g_signal_connect_object(geany_object,"document-save",G_CALLBACK(document_saved),all_menu,G_CONNECT_AFTER);g_signal_connect(all_menu,"destroy",G_CALLBACK(menu_destroyed),NULL);
}
