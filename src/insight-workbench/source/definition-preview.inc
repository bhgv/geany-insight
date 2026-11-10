/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Definition previews and editor host operations.
 * RU: Просмотр определений и операции хоста редактора. */

static void queue_definition(gboolean force) {
 if(!wb.alive || !wb.preview || !wb.follow || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return;
 GeanyDocument *doc=document_get_current(); if(!doc) return; gchar *query=cursor_query(doc); gboolean same=wb.follow_doc==doc->id && wb.follow_line==(gulong)sci_get_current_line(doc->editor->sci)+1 && !g_strcmp0(wb.follow_word,query); g_free(query);
 if(!force && (same || wb.definition_timer || wb.definition_worker)) return;
 stop_definition(); wb.definition_timer=g_timeout_add(500,definition_timeout,NULL);
}
static gboolean refresh_now(gpointer data) {
 (void)data; wb.refresh=0;
 GeanyDocument *active=document_get_current(); gboolean dirty=wb.index_dirty;
 if(wb.clips_store) { gchar *language=clip_current_language(); if(g_strcmp0(language,wb.clips_language)) notes_filter(NULL,GINT_TO_POINTER(INSIGHT_CLIP)); g_free(language); }
 if(active) { decorate_selection(active->editor->sci); gsize a=(gsize)SSM(active->editor->sci,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(active->editor->sci,SCI_GETSELECTIONEND,0,0); if(b>a) decorate_match(active->editor->sci,a,b); else decorate_match(active->editor->sci,0,0); }
 if(wb.preview) decorate_selection(wb.preview);
 /* Project models are refreshed when a completed snapshot is published,
  * rather than repeatedly while the same snapshot is still being built. */
 if(wb.local_doc_id!=(active ? active->id : 0)) local_filter(NULL,NULL);
 relation_update_from_editor(dirty);
 if(!wb.preview || !wb.context_visible || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.follow))) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current();
 if(!doc) { render_source("",NULL,1,NULL); g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; preview_links();navigation_state(); gtk_label_set_text(GTK_LABEL(wb.title),"Context — no active source"); return G_SOURCE_REMOVE; }
 queue_definition(dirty);
 return G_SOURCE_REMOVE;
}
static void history_move(GtkButton *button,gpointer data) {
 (void)button; gint next=wb.history_position+GPOINTER_TO_INT(data);
 if(!wb.history || next<0 || next>=(gint)wb.history->len) {ui_set_statusbar(TRUE,"%s",insight_tr("No context location in this direction"));return;}
 focus_widget_panel(GTK_WIDGET(wb.preview));
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 Location *location=g_ptr_array_index(wb.history,(guint)next);
 if(show_location(location->path,location->id,location->line,FALSE)) wb.history_position=next;
 navigation_state();
}
static void open_context(GtkButton *button,gpointer data) {
 (void)button; (void)data;
 if(!wb.source_line) { ui_set_statusbar(TRUE,"Context has no valid source location"); return; }
 guint id=wb.source_id; gchar *path=g_strdup(wb.source_path);
 gulong line=wb.source_line ? (gulong)sci_get_current_line(wb.preview)+1 : 0;
 GeanyDocument *old=document_get_current(),*doc=id ? document_find_by_id(id) : NULL;
 if(!doc && path) { gchar *native=utils_get_locale_from_utf8(path); doc=document_open_file(native,FALSE,NULL,NULL); g_free(native); }
 if(doc && line && line<=(gulong)sci_get_line_count(doc->editor->sci)) {
  gint length=sci_get_length(doc->editor->sci); gchar *current=length>=0 && length<=2*1024*1024 ? sci_get_contents(doc->editor->sci,length+1) : NULL;
  gchar *digest=current && g_utf8_validate(current,length,NULL) && !memchr(current,0,(gsize)length) ? source_digest(current) : NULL;
  gboolean unchanged=wb.source_digest && !g_strcmp0(digest,wb.source_digest); g_free(current); g_free(digest);
  if(!unchanged) { ui_set_statusbar(TRUE,"Context source changed; refresh the preview before opening"); g_free(path); return; }
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
  gsize a=(gsize)SSM(wb.preview,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(wb.preview,SCI_GETSELECTIONEND,0,0),mapped_a=0,mapped_b=0;gboolean selected=FALSE;
  if(b>a) {
   gint first=(gint)SSM(wb.preview,SCI_LINEFROMPOSITION,a,0),last=(gint)SSM(wb.preview,SCI_LINEFROMPOSITION,b,0);
   mapped_a=(gsize)sci_get_position_from_line(doc->editor->sci,first)+a-(gsize)sci_get_position_from_line(wb.preview,first);
   mapped_b=(gsize)sci_get_position_from_line(doc->editor->sci,last)+b-(gsize)sci_get_position_from_line(wb.preview,last);
   selected=mapped_a<=mapped_b && mapped_b<=(gsize)sci_get_length(doc->editor->sci);
  }
  /* Record the final selection caret, not an intermediate line start.
   * Otherwise Back first returns to that line start instead of the caller. */
  navqueue_goto_position(old,doc,selected ? (gint)mapped_b:sci_get_position_from_line(doc->editor->sci,(gint)line-1));
  if(selected) { SSM(doc->editor->sci,SCI_SETSEL,mapped_a,mapped_b);decorate_match(doc->editor->sci,mapped_a,mapped_b); }
  SSM(doc->editor->sci,SCI_VERTICALCENTRECARET,0,0); document_grab_focus(doc);
 } else ui_set_statusbar(TRUE,"Context source is unavailable or changed; refresh the index");
 g_free(path);
}
static gboolean preview_definition(gint position) {
 if(position<0) return FALSE;
 gint start=(gint)SSM(wb.preview,SCI_WORDSTARTPOSITION,position,TRUE),end=(gint)SSM(wb.preview,SCI_WORDENDPOSITION,position,TRUE);
 if(end<=start || end-start>1024) return TRUE;
 char word[1025]; for(gint i=start;i<end;i++) word[i-start]=(char)sci_get_char_at(wb.preview,i); word[end-start]=0;
 GeanyDocument *source=wb.source_id ? document_find_by_id(wb.source_id) : document_get_current();
 const InsightSymbol *definition=word_definition(source,word,wb.source_path,(gulong)sci_get_current_line(wb.preview)+1,wb.preview,position);
 if(definition) {
  gchar *path=g_strdup(definition->path),*resolved_name=g_strdup(definition->name); gulong line=definition->line;
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
  if(show_location(path,0,line,TRUE)) preview_mark_symbol(resolved_name);
  g_free(path);g_free(resolved_name);
 } else ui_set_statusbar(TRUE,"Definition not found: %s",word);
 return TRUE;
}
static void context_find(GtkWidget *widget,gpointer data) {
 (void)widget; gboolean forward=GPOINTER_TO_INT(data)!=-1; gchar *text=sci_get_contents(wb.preview,sci_get_length(wb.preview)+1); gsize size=strlen(text),begin,end; gboolean wrapped;
 const char *query=gtk_entry_get_text(GTK_ENTRY(wb.find_entry)); gchar *digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
 gsize a=(gsize)SSM(wb.preview,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(wb.preview,SCI_GETSELECTIONEND,0,0),from=forward ? b : a;
 guint flags=(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.find_case)) ? INSIGHT_SEARCH_CASE : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.find_word)) ? INSIGHT_SEARCH_WORD : 0)|(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(wb.find_regex)) ? INSIGHT_SEARCH_REGEX : 0);
 gboolean repeat=wb.find_last_flags==flags && !g_strcmp0(query,wb.find_last_query) && !g_strcmp0(digest,wb.find_last_digest) && a==wb.find_last_begin && b==wb.find_last_end;
 gboolean edge_wrap=FALSE; if(repeat && a==b) { edge_wrap=forward ? b==size : a==0; from=forward ? (b<size ? b+1 : 0) : (a ? a-1 : size); }

 GError *error=NULL; if(insight_context_find(text,size,query,from,forward,flags,&begin,&end,&wrapped,&error)) {
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE); SSM(wb.preview,SCI_SETSEL,begin,end); decorate_match(wb.preview,begin,end); SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
  gtk_label_set_text(GTK_LABEL(wb.find_status),(wrapped || edge_wrap) ? "Wrapped" : "Match"); g_free(wb.find_last_query); wb.find_last_query=g_strdup(query); g_free(wb.find_last_digest); wb.find_last_digest=g_strdup(digest); wb.find_last_begin=begin; wb.find_last_end=end; wb.find_last_flags=flags;
 } else gtk_label_set_text(GTK_LABEL(wb.find_status),error ? error->message : "No match");
 g_clear_error(&error); g_free(digest); g_free(text);
}
static void create_context_find(void) {
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); wb.context_find_row=row;gtk_widget_set_name(row,"insight-context-search-row"); wb.find_entry=gtk_search_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(wb.find_entry),"Find in Context…"); gtk_widget_set_tooltip_text(wb.find_entry,"Use ~term for fuzzy search when Regex is off");
 gtk_box_pack_start(GTK_BOX(row),wb.find_entry,TRUE,TRUE,0); GtkWidget *previous=gtk_button_new_with_label("Previous"),*next=gtk_button_new_with_label("Next");
 gtk_box_pack_start(GTK_BOX(row),previous,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),next,FALSE,FALSE,0);
 wb.find_case=gtk_check_button_new_with_label("Case"); wb.find_word=gtk_check_button_new_with_label("Word"); wb.find_regex=gtk_check_button_new_with_label("Regex");
 gtk_box_pack_start(GTK_BOX(row),wb.find_case,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),wb.find_word,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(row),wb.find_regex,FALSE,FALSE,0);
 wb.find_status=gtk_label_new(""); gtk_label_set_ellipsize(GTK_LABEL(wb.find_status),PANGO_ELLIPSIZE_END); gtk_widget_set_size_request(wb.find_status,80,-1); gtk_box_pack_start(GTK_BOX(row),wb.find_status,FALSE,FALSE,0);
 g_signal_connect(previous,"clicked",G_CALLBACK(context_find),GINT_TO_POINTER(-1)); g_signal_connect(next,"clicked",G_CALLBACK(context_find),NULL); g_signal_connect(wb.find_entry,"activate",G_CALLBACK(context_find),NULL); g_signal_connect(wb.find_entry,"key-press-event",G_CALLBACK(browser_key),NULL);
 gtk_box_pack_start(GTK_BOX(wb.box),row,FALSE,FALSE,0);
 gtk_widget_show_all(row);gtk_widget_set_no_show_all(row,TRUE);gtk_widget_hide(row);
}
static gboolean preview_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget; (void)data;
 if(event->keyval==GDK_KEY_Return && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK))) { open_context(NULL,NULL); return TRUE; }
 if(event->keyval==GDK_KEY_Escape) { editor_focus(); return TRUE; }
 if(event->keyval==GDK_KEY_f && (event->state&GEANY_PRIMARY_MOD_MASK)) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.context_find_toggle),TRUE);gtk_widget_grab_focus(wb.find_entry); return TRUE; }
 if(event->keyval==GDK_KEY_F3) { context_find(NULL,GINT_TO_POINTER(event->state&GDK_SHIFT_MASK ? -1 : 1)); return TRUE; }
 if(event->keyval==GDK_KEY_F12 && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SHIFT_MASK))) return preview_definition(sci_get_current_position(wb.preview));
 return FALSE;
}
typedef struct { gint position;gchar *path,*digest; } PreviewClick;
static void preview_click_free(gpointer data) { PreviewClick *click=data;g_free(click->path);g_free(click->digest);g_free(click); }
static gboolean preview_single_click(gpointer data) {
 PreviewClick *click=data;wb.preview_click_timer=0;
 if(wb.alive && !g_strcmp0(click->path,wb.source_path) && !g_strcmp0(click->digest,wb.source_digest)) preview_definition(click->position);
 return G_SOURCE_REMOVE;
}
static void reveal_context_file(GtkMenuItem *item,gpointer data) { (void)item;(void)data;if(wb.source_path && wb.source_path[0]!='@') ui_reveal_file(wb.source_path); }
static void context_location_popup(GtkLabel *label,GtkMenu *menu,gpointer data) {
 (void)label;(void)data;GtkWidget *item=gtk_menu_item_new_with_label(insight_tr("Show in file manager"));gtk_widget_set_name(item,"insight-context-location-reveal");gtk_widget_set_sensitive(item,wb.source_path && wb.source_path[0]!='@');g_signal_connect(item,"activate",G_CALLBACK(reveal_context_file),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);gtk_widget_show(item);
}
static void context_popup_edit(GtkMenuItem *item,gpointer command) { (void)item;SSM(wb.preview,GPOINTER_TO_UINT(command),0,0); }
static gboolean preview_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 if(event->button==3) { GtkWidget *menu=gtk_menu_new(),*item=gtk_menu_item_new_with_label("Show in file manager");
 for(guint n=0;n<2;n++) {GtkWidget *entry=gtk_menu_item_new_with_label(n ? "Context forward":"Context back");gtk_widget_set_sensitive(entry,wb.history && (n ? wb.history_position+1<(gint)wb.history->len:wb.history_position>0));g_signal_connect(entry,"activate",G_CALLBACK(history_move),GINT_TO_POINTER(n ? 1:-1));gtk_menu_shell_append(GTK_MENU_SHELL(menu),entry);}
  const gchar *labels[]={"Copy","Select All"};const guint commands[]={SCI_COPY,SCI_SELECTALL};for(guint i=0;i<G_N_ELEMENTS(commands);i++) { GtkWidget *edit=gtk_menu_item_new_with_label(labels[i]);if(i==0) gtk_widget_set_sensitive(edit,SSM(wb.preview,SCI_GETSELECTIONSTART,0,0)!=SSM(wb.preview,SCI_GETSELECTIONEND,0,0));g_signal_connect(edit,"activate",G_CALLBACK(context_popup_edit),GUINT_TO_POINTER(commands[i]));gtk_menu_shell_append(GTK_MENU_SHELL(menu),edit); }gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());gtk_widget_set_sensitive(item,wb.source_path && wb.source_path[0]!='@');gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(reveal_context_file),NULL);g_signal_connect_swapped(menu,"selection-done",G_CALLBACK(gtk_widget_destroy),menu);insight_i18n_apply(menu);gtk_widget_show_all(menu);gtk_menu_popup_at_pointer(GTK_MENU(menu),(GdkEvent *)event);return TRUE; }
 (void)widget; (void)data;if(event->button!=1) return FALSE;
 if(wb.preview_click_timer) { g_source_remove(wb.preview_click_timer);wb.preview_click_timer=0; }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),FALSE);
 gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINT,(uptr_t)event->x,(sptr_t)event->y);
 if(event->type==GDK_2BUTTON_PRESS) {
  if(position>=0) { sci_set_selection(wb.preview,position,position);open_context(NULL,NULL); }
  return TRUE;
 }
 if(event->type!=GDK_BUTTON_PRESS || position<0) return FALSE;
 if(!SSM(wb.preview,SCI_INDICATORVALUEAT,INSIGHT_LINK,position)) return FALSE;
 gint delay=250;g_object_get(gtk_settings_get_default(),"gtk-double-click-time",&delay,NULL);
 PreviewClick *click=g_new0(PreviewClick,1);click->position=position;click->path=g_strdup(wb.source_path);click->digest=g_strdup(wb.source_digest);
 sci_set_selection(wb.preview,position,position);gtk_widget_grab_focus(GTK_WIDGET(wb.preview));
 wb.preview_click_timer=g_timeout_add_full(G_PRIORITY_DEFAULT,(guint)MAX(1,delay)+20,preview_single_click,click,preview_click_free);
 return TRUE;
}
void insight_workbench_editor_updated(gboolean content) {
 if(!wb.alive) return;
 insight_window_list_editor_updated();
 GeanyDocument *doc=document_get_current(); if(!wb.preview || !doc) return;
 if(content && wb.bookmark_handles && !wb.bookmark_sync_timer) wb.bookmark_sync_timer=g_timeout_add(350,bookmark_sync_idle,NULL);
 gint position=sci_get_current_position(doc->editor->sci),anchor=(gint)SSM(doc->editor->sci,SCI_GETANCHOR,0,0);
 gboolean focused=gtk_widget_has_focus(GTK_WIDGET(doc->editor->sci));
 /* An edit/caret update may arrive before focus-in. Do not cache that as an
  * already-followed focused position. Unchanged focus-out keeps manual locks. */
 if(!content && wb.editor_follow_id==doc->id && wb.editor_follow_position==position && wb.editor_follow_anchor==anchor && (!focused || wb.editor_follow_focused)) return;
 stop_definition(); wb.follow_doc=0;
 wb.editor_follow_id=doc->id; wb.editor_follow_position=position; wb.editor_follow_anchor=anchor;wb.editor_follow_focused=focused;
 if(focused) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(wb.follow),TRUE);
 queue_definition(FALSE); insight_workbench_refresh();
}
void insight_workbench_refresh(void) {
 if(!wb.preview) return;
 if(wb.refresh) g_source_remove(wb.refresh);
 wb.refresh=g_timeout_add(60,refresh_now,NULL);
}
typedef struct { GeanyDocument *doc; gchar *scratch; GeanyDocument *undo_doc; int message_set; } NativeHost;
static const char *native_get_text(void *ctx) {
 NativeHost *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_contents(h->doc->editor->sci,sci_get_length(h->doc->editor->sci)+1);
 return h->scratch;
}
static const char *native_get_selection(void *ctx) {
 NativeHost *h=ctx; g_free(h->scratch);
 h->scratch=sci_get_selection_contents(h->doc->editor->sci); return h->scratch;
}
static int native_replace_selection(void *ctx,const char *text) {
 NativeHost *h=ctx; sci_replace_sel(h->doc->editor->sci,text); return 1;
}
static unsigned long native_get_position(void *ctx) {
 NativeHost *h=ctx; return (unsigned long)sci_get_current_position(h->doc->editor->sci);
}
static int native_go_line(void *ctx,unsigned long line) {
 NativeHost *h=ctx;
 if(!line || line>(unsigned long)sci_get_line_count(h->doc->editor->sci)) return 0;
 return navqueue_goto_line(h->doc,h->doc,(gint)line);
}
static int native_open_file(void *ctx,const char *path) {
 NativeHost *h=ctx; if(!g_utf8_validate(path,-1,NULL) || !g_path_is_absolute(path)) return 0;
 gchar *locale=utils_get_locale_from_utf8(path); GeanyDocument *next=document_open_file(locale,FALSE,NULL,NULL); g_free(locale);
 if(!next) return 0;
 if(h->undo_doc && h->undo_doc!=next) { sci_end_undo_action(h->undo_doc->editor->sci); sci_start_undo_action(next->editor->sci); h->undo_doc=next; }
 h->doc=next; return 1;
}
static int native_save_file(void *ctx) { NativeHost *h=ctx; return h->doc->file_name && document_save_file(h->doc,FALSE); }
static const char *native_filename(void *ctx) { return ((NativeHost *)ctx)->doc->file_name; }
static int native_selection_range(void *ctx,unsigned long *start,unsigned long *end) {
 NativeHost *h=ctx; *start=(unsigned long)sci_get_selection_start(h->doc->editor->sci); *end=(unsigned long)sci_get_selection_end(h->doc->editor->sci); return 1;
}
static int native_valid_range(NativeHost *h,unsigned long start,unsigned long end) {
 ScintillaObject *sci=h->doc->editor->sci; unsigned long length=(unsigned long)sci_get_length(sci);
 if(start>end || end>length) return 0;
 return (start==length || (((unsigned char)sci_get_char_at(sci,(gint)start)&0xc0)!=0x80)) &&
  (end==length || (((unsigned char)sci_get_char_at(sci,(gint)end)&0xc0)!=0x80));
}
static int native_set_selection(void *ctx,unsigned long start,unsigned long end) {
 NativeHost *h=ctx; if(!native_valid_range(h,start,end)) return 0; sci_set_selection_start(h->doc->editor->sci,(gint)start); sci_set_selection_end(h->doc->editor->sci,(gint)end); return 1;
}
static unsigned long native_line_count(void *ctx) { return (unsigned long)sci_get_line_count(((NativeHost *)ctx)->doc->editor->sci); }
static const char *native_line_text(void *ctx,unsigned long line) {
 NativeHost *h=ctx; if(!line || line>(unsigned long)sci_get_line_count(h->doc->editor->sci)) return NULL;
 g_free(h->scratch); h->scratch=sci_get_line(h->doc->editor->sci,(gint)line-1); return h->scratch;
}
static int native_replace_range(void *ctx,unsigned long start,unsigned long end,const char *text) {
 NativeHost *h=ctx; if(!native_valid_range(h,start,end) || !g_utf8_validate(text,-1,NULL)) return 0;
 sci_set_target_start(h->doc->editor->sci,(gint)start); sci_set_target_end(h->doc->editor->sci,(gint)end);
 return sci_replace_target(h->doc->editor->sci,text,FALSE)>=0;
}
static int native_insert(void *ctx,unsigned long at,const char *text) { return native_replace_range(ctx,at,at,text); }
static int native_message(void *ctx,const char *text) {
 NativeHost *h=ctx; if(!g_utf8_validate(text,-1,NULL)) return 0; h->message_set=1; ui_set_statusbar(TRUE,"%s",text); return 1;
}
typedef struct {
    HdlJob *snapshot;
    gchar *name, *path;
    guint line, column;
    gboolean show_uses, preview_context;
    GWeakRef viewer;
} WaveJump;

/* Release the waveform lookup and its HDL snapshot. */
/* Освобождает данные поиска по диаграмме и снимок HDL. */
static void wave_jump_free(gpointer data)
{
    WaveJump *jump = data;
    g_weak_ref_clear(&jump->viewer);
    hdl_job_free(jump->snapshot);
    g_free(jump->name);
    g_free(jump->path);
    g_free(jump);
}

/* Resolve the declaration and load its project references off the UI thread. */
/* Ищет объявление и загружает ссылки проекта вне потока интерфейса. */
static void wave_jump_run(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
    (void)object;
    WaveJump *jump = data;
    HdlJob *snapshot = jump->snapshot;
    gboolean found = snapshot->valid && insight_slang_wave_definition(snapshot->service, jump->name,
        snapshot->paths, snapshot->buffers, snapshot->vivado, cancel,
        &jump->path, &jump->line, &jump->column);

    if (found && jump->show_uses)
        snapshot->result = insight_slang_project_configured(snapshot->service, snapshot->paths,
            snapshot->buffers, snapshot->root, snapshot->cache, snapshot->vivado, cancel);
    g_task_return_boolean(task, found);
}

enum {
    WAVE_USE_ROLE, WAVE_USE_NAME, WAVE_USE_FILE, WAVE_USE_LINE,
    WAVE_USE_CONNECTION, WAVE_USE_HOPS, WAVE_USE_SELECTION, WAVE_USE_FROM, WAVE_USE_COLUMNS
};

typedef struct {
    GHashTable *edges;
    GHashTable *declarations;
    GHashTable *references;
    guint references_remaining;
    gboolean reference_limit_reached;
} WaveDependencyIndex;
