/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Literal and regular-expression search over comparison snapshots.
 * RU: Буквальный и регулярный поиск по снимкам сравнения. */

static gboolean identifier_char(gunichar c) {
 return c=='_' || g_unichar_isalnum(c) || g_unichar_ismark(c);
}
static gboolean search_match(GtkTextIter start,const gchar *query,GtkTextSearchFlags flags,
 gboolean backwards,gboolean whole,GtkTextIter *a,GtkTextIter *z) {
 while(backwards ? gtk_text_iter_backward_search(&start,query,flags,a,z,NULL) : gtk_text_iter_forward_search(&start,query,flags,a,z,NULL)) {
  GtkTextIter before=*a;gboolean left=gtk_text_iter_backward_char(&before);
  if(!whole || ((!left || !identifier_char(gtk_text_iter_get_char(&before))) &&
     (gtk_text_iter_is_end(z) || !identifier_char(gtk_text_iter_get_char(z))))) return TRUE;
  start=backwards ? *a : *z;
 }
 return FALSE;
}
/* Regex hits own no live GtkTextIters: all positions refer to one UTF-8 snapshot. */
typedef struct { gint begin,end;gchar *replacement; } RegexHit;
static void regex_hit_free(gpointer data) { RegexHit *hit=data;g_free(hit->replacement);g_free(hit); }
static void regex_reset(GtkWidget *widget,gpointer data) { (void)widget;Merge *m=data;m->zero_found=FALSE;if(m->find_status) gtk_widget_set_tooltip_text(m->find_status,NULL);if(m->replace_status) gtk_widget_set_tooltip_text(m->replace_status,NULL); }
static void regex_error(GtkWidget *status,const gchar *text,GError *error) {
 gtk_label_set_text(GTK_LABEL(status),text);const gchar *detail=error ? g_strrstr(error->message,": ") : NULL;gtk_widget_set_tooltip_text(status,error ? (detail ? detail+2 : error->message) : text);g_clear_error(&error);
}
/* Deliberately bounded replacement grammar, avoiding binary/octal escapes. */
static gchar *regex_expand(GRegex *regex,GMatchInfo *info,const gchar *replacement) {
 GString *out=g_string_new(NULL);gint captures=g_regex_get_capture_count(regex);
 for(const gchar *p=replacement;*p;) {
  if(*p!='\\') { if(info) g_string_append_c(out,*p);p++; }
  else {
   p++;if(!*p) goto invalid;
   if(*p=='\\' || *p=='n' || *p=='r' || *p=='t') { if(info) g_string_append_c(out,*p=='n' ? '\n' : *p=='r' ? '\r' : *p=='t' ? '\t' : '\\');p++; }
   else {
    gint group=-1;
    if(g_ascii_isdigit(*p)) { group=*p++-'0';if(!group && g_ascii_isdigit(*p)) goto invalid;if(group && g_ascii_isdigit(*p)) group=10*group+(*p++-'0'); }
    else if(*p=='g' && p[1]=='<') {
     p+=2;const gchar *end=strchr(p,'>');if(!end || end==p || end-p>128) goto invalid;
     gchar *name=g_strndup(p,end-p);gboolean numeric=TRUE;for(const gchar *q=name;*q;q++) if(!g_ascii_isdigit(*q)) numeric=FALSE;
     if(numeric) { if(strlen(name)>5) { g_free(name);goto invalid; }group=(gint)g_ascii_strtoull(name,NULL,10); }
     else group=g_regex_get_string_number(regex,name);
     g_free(name);p=end+1;
    } else goto invalid;
    if(group<0 || group>captures) goto invalid;
    if(info) { gchar *part=g_match_info_fetch(info,group);if(part) { if(!g_utf8_validate(part,-1,NULL)) { g_free(part);goto invalid; }g_string_append(out,part);g_free(part); } }
   }
  }
  if(out->len>1024*1024) goto invalid;
 }
 return g_string_free(out,FALSE);
invalid:
 g_string_free(out,TRUE);return NULL;
}
static GPtrArray *regex_collect(Merge *m,GtkTextBuffer *buffer,guint maximum,
 const gchar *replacement,gboolean *truncated,GtkWidget *status) {
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));gchar *text=contents(buffer);gsize length=strlen(text);
 if(truncated) *truncated=FALSE;
 if(strlen(query)>8192 || length>1024*1024) { g_free(text);regex_error(status,"Regular expression limit: 8192-byte pattern and 1 MiB text.",NULL);return NULL; }
 GRegexCompileFlags flags=G_REGEX_MULTILINE|G_REGEX_NEWLINE_ANYCRLF;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=G_REGEX_CASELESS;
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_dotall))) flags|=G_REGEX_DOTALL;
 /* No JIT: depth and match limits remain effective in both PCRE generations. */
 gchar *pattern=g_strconcat("(*LIMIT_MATCH=100000)(*LIMIT_RECURSION=256)",query,NULL);GError *error=NULL;
 GRegex *regex=g_regex_new(pattern,flags,0,&error);g_free(pattern);
 if(!regex) { g_free(text);regex_error(status,"Invalid regular expression; hover for details.",error);return NULL; }
 if(replacement) { gchar *valid=regex_expand(regex,NULL,replacement);if(!valid) { g_regex_unref(regex);g_free(text);regex_error(status,"Invalid replacement escape or capture group.",NULL);return NULL; }g_free(valid); }
 GPtrArray *hits=g_ptr_array_new_with_free_func(regex_hit_free);GMatchInfo *info=NULL;
 gboolean matched=g_regex_match_full(regex,text,length,0,0,&info,&error),whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_word));
 guint scanned=0;gint previous_byte=0,previous_char=0;guint64 expanded=0;const gchar *failure=NULL;
 while(matched && !error) {
  gint begin,end;g_match_info_fetch_pos(info,0,&begin,&end);
  if(begin<previous_byte || end<begin || (gsize)end>length || (text[begin] && (text[begin]&0xc0)==0x80) || (text[end] && (text[end]&0xc0)==0x80)) { failure="Regular expression must match complete UTF-8 characters.";break; }
  const gchar *before=begin ? g_utf8_find_prev_char(text,text+begin) : NULL;
  gboolean split_crlf=begin==end && begin>0 && text[begin-1]=='\r' && text[begin]=='\n';
  gboolean boundary=!split_crlf && (!whole || ((!before || !identifier_char(g_utf8_get_char(before))) && (!text[end] || !identifier_char(g_utf8_get_char(text+end)))));
  if(++scanned>100000 || (boundary && hits->len==maximum)) { if(truncated) *truncated=TRUE;else failure="Regular expression match limit: 100000. Nothing changed.";break; }
  gint first=previous_char+g_utf8_pointer_to_offset(text+previous_byte,text+begin);previous_byte=begin;previous_char=first;
  if(boundary) {
   RegexHit *hit=g_new0(RegexHit,1);hit->begin=first;hit->end=first+g_utf8_pointer_to_offset(text+begin,text+end);
   if(replacement) { hit->replacement=regex_expand(regex,info,replacement);if(!hit->replacement) { regex_hit_free(hit);failure="Replacement expansion is invalid or exceeds 1 MiB. Nothing changed.";break; }expanded+=strlen(hit->replacement);if(expanded>8*1024*1024) { regex_hit_free(hit);failure="Replacement expansion limit: 8 MiB total. Nothing changed.";break; } }
   g_ptr_array_add(hits,hit);
  }
  matched=g_match_info_next(info,&error);
 }
 if(info) g_match_info_free(info);
 g_regex_unref(regex);g_free(text);
 if(error || failure) { g_ptr_array_unref(hits);regex_error(status,failure ? failure : "Regular expression work limit or matching error. Nothing changed.",error);return NULL; }
 gtk_widget_set_tooltip_text(status,NULL);return hits;
}
static void regex_find(Merge *m,guint side,gboolean backwards) {
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GPtrArray *hits=regex_collect(m,buffer,100000,NULL,NULL,m->find_status);if(!hits) return;
 GtkTextIter a,z;gboolean selected=gtk_text_buffer_get_selection_bounds(buffer,&a,&z);if(!selected) { gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_insert(buffer));z=a; }
 gint offset=gtk_text_iter_get_offset(backwards ? &a : &z);gint chosen=-1;
 gboolean skip_zero=!selected && m->zero_found && m->zero_side==(gint)side && m->zero_offset==offset && m->zero_serial==m->history_serial;
 for(guint i=0;i<hits->len;i++) { RegexHit *hit=g_ptr_array_index(hits,i);if(skip_zero && hit->begin==offset && hit->end==offset) continue;
  if(backwards ? hit->end<=offset : hit->begin>=offset) { chosen=i;if(!backwards) break; }
 }
 gboolean wrapped=chosen<0 && hits->len && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_wrap));
 if(wrapped) chosen=backwards ? (gint)hits->len-1 : 0;
 if(chosen>=0) { RegexHit *hit=g_ptr_array_index(hits,chosen);gtk_text_buffer_get_iter_at_offset(buffer,&a,hit->begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,hit->end);gtk_text_buffer_select_range(buffer,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0,0.5);
  m->zero_found=hit->begin==hit->end;m->zero_offset=hit->begin;m->zero_side=side;m->zero_serial=m->history_serial;
  gchar *label=g_strdup_printf(wrapped ? "Wrapped: line %d" : "Found: line %d",gtk_text_iter_get_line(&a)+1);gtk_label_set_text(GTK_LABEL(m->find_status),label);g_free(label);
 } else gtk_label_set_text(GTK_LABEL(m->find_status),"Text not found.");
 g_ptr_array_unref(hits);
}
static void selection_search(Merge *m,guint side,gint direction) {
 if(m->busy || side>2) return;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z,limit;guint scanned=0;
 gboolean selection=gtk_text_buffer_get_selection_bounds(buffer,&a,&limit);
 if(selection) { while(gtk_text_iter_compare(&a,&limit)<0 && !identifier_char(gtk_text_iter_get_char(&a)) && scanned++<4096) gtk_text_iter_forward_char(&a);if(gtk_text_iter_compare(&a,&limit)>=0) return; }
 else { gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_insert(buffer));if(!identifier_char(gtk_text_iter_get_char(&a))) { GtkTextIter before=a;if(!gtk_text_iter_backward_char(&before) || !identifier_char(gtk_text_iter_get_char(&before))) return;a=before; } }
 if(scanned>=4096 || !identifier_char(gtk_text_iter_get_char(&a))) return;
 z=a;
 for(;;) { GtkTextIter before=a;if(!gtk_text_iter_backward_char(&before) || !identifier_char(gtk_text_iter_get_char(&before))) break;if(scanned++>=4096) return;a=before; }
 while(!gtk_text_iter_is_end(&z) && identifier_char(gtk_text_iter_get_char(&z))) { if(scanned++>=4096) return;gtk_text_iter_forward_char(&z); }
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),FALSE);
 gchar *query=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);gtk_entry_set_text(GTK_ENTRY(m->find_entry),query);g_free(query);gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),side);
 if(direction) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_word),TRUE);gtk_text_buffer_select_range(buffer,&a,&z);find_text(m,direction<0);gtk_widget_grab_focus(m->views[side]); }
}
static void search_result_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer dialog) { (void)view;(void)path;(void)column;gtk_dialog_response(GTK_DIALOG(dialog),GTK_RESPONSE_APPLY); }
typedef struct { GtkWidget *dialog,*tree,*filter_entry,*fields[4],*status,*preview,*copy_selected;GtkTextBuffer *snapshot;GtkTreeModelFilter *filter;gchar *fold,*source;gboolean can_edit,closing; } SearchOutput;
static gboolean search_output_visible(GtkTreeModel *model,GtkTreeIter *iter,gpointer data) {
 SearchOutput *out=data;if(!*out->fold) return TRUE;gchar *text=NULL;gtk_tree_model_get(model,iter,4,&text,-1);gchar *fold=g_utf8_casefold(text ? text : "",-1);gboolean visible=strstr(fold,out->fold)!=NULL;g_free(fold);g_free(text);return visible;
}
static void search_output_selection(GtkTreeSelection *selection,gpointer data) {
 SearchOutput *out=data;if(out->closing) return;GtkTreeIter iter;GtkTreeModel *model;gboolean selected=gtk_tree_selection_get_selected(selection,&model,&iter);gtk_dialog_set_response_sensitive(GTK_DIALOG(out->dialog),GTK_RESPONSE_APPLY,selected);gtk_dialog_set_response_sensitive(GTK_DIALOG(out->dialog),GTK_RESPONSE_YES,selected && out->can_edit);
 gtk_widget_set_sensitive(out->copy_selected,selected);
 GtkTextBuffer *preview=gtk_text_view_get_buffer(GTK_TEXT_VIEW(out->preview));gtk_text_buffer_set_text(preview,"",0);
 if(selected) {
  gint begin,end;gtk_tree_model_get(model,&iter,0,&begin,1,&end,-1);GtkTextIter a,z,match;
  gtk_text_buffer_get_iter_at_offset(out->snapshot,&match,begin);a=match;gtk_text_iter_set_line_offset(&a,0);
  for(guint i=0;i<2;i++) if(!gtk_text_iter_backward_line(&a)) break;
  /* Bound context even when a logical line is exceptionally long. */
  if(begin-gtk_text_iter_get_offset(&a)>2048) gtk_text_buffer_get_iter_at_offset(out->snapshot,&a,begin-2048);
  gtk_text_buffer_get_iter_at_offset(out->snapshot,&z,end);for(guint i=0;i<3;i++) if(!gtk_text_iter_forward_line(&z)) break;
  if(gtk_text_iter_get_offset(&z)-gtk_text_iter_get_offset(&a)>4096) gtk_text_buffer_get_iter_at_offset(out->snapshot,&z,gtk_text_iter_get_offset(&a)+4096);
  gint base=gtk_text_iter_get_offset(&a),limit=gtk_text_iter_get_offset(&z);gchar *text=gtk_text_buffer_get_text(out->snapshot,&a,&z,TRUE);gtk_text_buffer_set_text(preview,text,-1);g_free(text);
  gtk_text_buffer_get_iter_at_offset(preview,&a,begin-base);gtk_text_buffer_get_iter_at_offset(preview,&z,MIN(end,limit)-base);
  gtk_text_buffer_apply_tag_by_name(preview,"match",&a,&z);gtk_text_buffer_place_cursor(preview,&a);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(out->preview),&a,0.1,TRUE,0,0.5);
 }

}
static void search_output_filter(GtkEditable *entry,gpointer data) {
 SearchOutput *out=data;if(out->closing) return;g_free(out->fold);out->fold=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(entry)),-1);gtk_tree_model_filter_refilter(out->filter);gchar *label=g_strdup_printf("Visible matches: %u",gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(out->tree)),NULL));gtk_label_set_text(GTK_LABEL(out->status),label);g_free(label);
}
static void search_output_columns(GtkToggleButton *button,gpointer data) {
 (void)button;SearchOutput *out=data;if(out->closing) return;for(guint i=0;i<3;i++) gtk_tree_view_column_set_visible(gtk_tree_view_get_column(GTK_TREE_VIEW(out->tree),i),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(out->fields[i+1])));
}
static void tsv_field(GString *out,const gchar *text) {
 for(const gchar *p=text;*p;p++) { if(*p=='\t') g_string_append(out,"\\t");else if(*p=='\r') g_string_append(out,"\\r");else if(*p=='\n') g_string_append(out,"\\n");else if(*p=='\\') g_string_append(out,"\\\\");else g_string_append_c(out,*p); }
}
static void search_output_copy(GtkWidget *button,gpointer data) {
 SearchOutput *out=data;if(out->closing) return;gboolean selected_only=button==out->copy_selected;GtkTreeIter selected;GtkTreeModel *selected_model=NULL;if(selected_only && !gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(out->tree)),&selected_model,&selected)) return;gboolean fields[4],any=FALSE;for(guint i=0;i<4;i++) { fields[i]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(out->fields[i]));any|=fields[i]; }
 if(!any) { gtk_label_set_text(GTK_LABEL(out->status),"Select at least one output field.");return; }
 GString *text=g_string_new(NULL);const gchar *headers[]={"File","Line","Column","Text"};gboolean first=TRUE;
 for(guint i=0;i<4;i++) if(fields[i]) { if(!first) g_string_append_c(text,'\t');g_string_append(text,headers[i]);first=FALSE; }
  g_string_append_c(text,'\n');
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(out->tree));GtkTreeIter iter;guint rows=0;gboolean exceeded=FALSE;
 for(gboolean valid=selected_only ? (iter=selected,TRUE) : gtk_tree_model_get_iter_first(model,&iter);valid;valid=selected_only ? FALSE : gtk_tree_model_iter_next(model,&iter)) {
  guint line,column;gchar *excerpt=NULL;gtk_tree_model_get(model,&iter,2,&line,3,&column,4,&excerpt,-1);gchar *line_text=g_strdup_printf("%u",line),*column_text=g_strdup_printf("%u",column);const gchar *values[]={out->source,line_text,column_text,excerpt ? excerpt : ""};first=TRUE;
  for(guint i=0;i<4;i++) if(fields[i]) { if(!first) g_string_append_c(text,'\t');tsv_field(text,values[i]);first=FALSE; }
  g_string_append_c(text,'\n');g_free(excerpt);g_free(line_text);g_free(column_text);rows++;
  if(text->len>4*1024*1024) { exceeded=TRUE;break; }
 }
 if(exceeded) gtk_label_set_text(GTK_LABEL(out->status),"Copy limit: 4 MiB. Clipboard unchanged.");
 else { gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text->str,text->len);gchar *label=g_strdup_printf(selected_only ? "Copied selected matches: %u" : "Copied visible matches: %u",rows);gtk_label_set_text(GTK_LABEL(out->status),label);g_free(label); }
 g_string_free(text,TRUE);
}
static void search_output_step(SearchOutput *out,gboolean backwards) {
 GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(out->tree));gint count=gtk_tree_model_iter_n_children(model,NULL);if(!count) return;
 GtkTreeIter iter;gint index=backwards ? count-1 : 0;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(out->tree)),NULL,&iter)) { GtkTreePath *old=gtk_tree_model_get_path(model,&iter);index=(gtk_tree_path_get_indices(old)[0]+count+(backwards ? -1 : 1))%count;gtk_tree_path_free(old); }
 GtkTreePath *path=gtk_tree_path_new_from_indices(index,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(out->tree),path,NULL,FALSE);gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(out->tree),path,NULL,FALSE,0,0);gtk_tree_path_free(path);
}
static gboolean search_output_key(GtkWidget *widget,GdkEventKey *event,gpointer data) {
 (void)widget;SearchOutput *out=data;if(out->closing) return FALSE;
 guint modifiers=event->state&gtk_accelerator_get_default_mod_mask();
 if(modifiers==GDK_CONTROL_MASK && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F)) { gtk_widget_grab_focus(out->filter_entry);return TRUE; }
 if(event->keyval==GDK_KEY_F3 && !(modifiers&~GDK_SHIFT_MASK)) { search_output_step(out,modifiers&GDK_SHIFT_MASK);return TRUE; }
 if((event->keyval==GDK_KEY_Return || event->keyval==GDK_KEY_KP_Enter || event->keyval==GDK_KEY_l || event->keyval==GDK_KEY_L) && modifiers==GDK_CONTROL_MASK) { if(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(out->dialog),GTK_RESPONSE_APPLY))) gtk_dialog_response(GTK_DIALOG(out->dialog),GTK_RESPONSE_APPLY);return TRUE; }
 if(gtk_window_get_focus(GTK_WINDOW(out->dialog))==out->filter_entry && event->keyval==GDK_KEY_Down && !modifiers) { GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(out->tree));if(gtk_tree_model_iter_n_children(model,NULL)) { GtkTreePath *path=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(out->tree),path,NULL,FALSE);gtk_tree_path_free(path);gtk_widget_grab_focus(out->tree); }return TRUE; }
 return FALSE;
}
static void find_all(Merge *m,guint side) {
 if(m->busy || side>2) return;
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));if(!*query) { gtk_label_set_text(GTK_LABEL(m->find_status),"Enter search text.");return; }
 GtkTextBuffer *search_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));gboolean regex_mode=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_regex)),regex_truncated=FALSE;
 GPtrArray *regex_hits=regex_mode ? regex_collect(m,search_buffer,5000,NULL,&regex_truncated,m->find_status) : NULL;if(regex_mode && !regex_hits) return;
 GtkWidget *window=m->window;g_object_ref(window);guint64 serial=m->history_serial;
 GtkTextBuffer *buffer=search_buffer;GtkTextSearchFlags flags=GTK_TEXT_SEARCH_TEXT_ONLY;if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=GTK_TEXT_SEARCH_CASE_INSENSITIVE;gboolean whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_word));
 GtkListStore *model=gtk_list_store_new(5,G_TYPE_INT,G_TYPE_INT,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_STRING);GtkTextIter start,a,z;gtk_text_buffer_get_start_iter(buffer,&start);guint count=0;gboolean truncated=FALSE;
 while(regex_mode ? count<regex_hits->len : search_match(start,query,flags,FALSE,whole,&a,&z)) {
  if(regex_mode) { RegexHit *hit=g_ptr_array_index(regex_hits,count);gtk_text_buffer_get_iter_at_offset(buffer,&a,hit->begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,hit->end); }
  if(count==5000) { truncated=TRUE;break; }
  GtkTextIter begin=a,end=a;gtk_text_iter_set_line_offset(&begin,0);if(!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);if(gtk_text_iter_get_offset(&end)-gtk_text_iter_get_offset(&begin)>120) { begin=a;end=a;gtk_text_iter_forward_chars(&end,120); }
  gchar *excerpt=gtk_text_buffer_get_text(buffer,&begin,&end,TRUE);for(gchar *p=excerpt;*p;p++) if(*p=='\r'||*p=='\n'||*p=='\t') *p=' ';
  GtkTreeIter iter;gtk_list_store_append(model,&iter);gtk_list_store_set(model,&iter,0,gtk_text_iter_get_offset(&a),1,gtk_text_iter_get_offset(&z),2,gtk_text_iter_get_line(&a)+1,3,gtk_text_iter_get_line_offset(&a)+1,4,excerpt,-1);g_free(excerpt);count++;start=z;
 }
 if(regex_hits) g_ptr_array_unref(regex_hits);
 truncated|=regex_truncated;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Pane Search Results",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CANCEL,"Go to match",GTK_RESPONSE_APPLY,"Edit source file",GTK_RESPONSE_YES,NULL);g_object_ref(dialog);gtk_widget_set_name(dialog,"insight-diff-search-results");gtk_window_set_default_size(GTK_WINDOW(dialog),800,560);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));gchar *heading=regex_mode ? g_strdup_printf("%s — %s — %s",insight_tr(side==0 ? "Left" : side==1 ? "Right" : "Result"),insight_tr("Regular expression"),query) : g_strdup_printf("%s — %s",insight_tr(side==0 ? "Left" : side==1 ? "Right" : "Result"),query);GtkWidget *pattern=gtk_label_new(heading);g_free(heading);gtk_label_set_ellipsize(GTK_LABEL(pattern),PANGO_ELLIPSIZE_END);gtk_box_pack_start(GTK_BOX(box),pattern,FALSE,FALSE,4);gchar *summary=g_strdup_printf(truncated ? "Showing first %u matches (limit reached)." : "Matches: %u",count);gtk_box_pack_start(GTK_BOX(box),gtk_label_new(summary),FALSE,FALSE,4);g_free(summary);
 SearchOutput *out=g_new0(SearchOutput,1);out->dialog=dialog;out->snapshot=gtk_text_buffer_new(NULL);gchar *snapshot_text=contents(buffer);gtk_text_buffer_set_text(out->snapshot,snapshot_text,-1);g_free(snapshot_text);out->fold=g_strdup("");gchar *native_source=side<2 && m->loaded_paths[side] ? g_strdup(m->loaded_paths[side]) : NULL;out->source=native_source ? g_filename_display_name(native_source) : g_strdup(side==0 ? "Left" : side==1 ? "Right" : "Result");out->can_edit=native_source && source_opener;
 GtkWidget *controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,4);out->filter_entry=gtk_search_entry_new();gtk_entry_set_placeholder_text(GTK_ENTRY(out->filter_entry),"Filter displayed matches");gtk_entry_set_max_length(GTK_ENTRY(out->filter_entry),256);gtk_widget_set_name(out->filter_entry,"insight-diff-search-filter");gtk_box_pack_start(GTK_BOX(controls),out->filter_entry,TRUE,TRUE,0);
 GtkWidget *copy=gtk_button_new_with_label("Copy visible matches");gtk_widget_set_name(copy,"insight-diff-search-copy");gtk_box_pack_start(GTK_BOX(controls),copy,FALSE,FALSE,0);g_signal_connect(copy,"clicked",G_CALLBACK(search_output_copy),out);
 out->copy_selected=gtk_button_new_with_label("Copy selected match");gtk_widget_set_name(out->copy_selected,"insight-diff-search-copy-selected");gtk_box_pack_start(GTK_BOX(controls),out->copy_selected,FALSE,FALSE,0);g_signal_connect(out->copy_selected,"clicked",G_CALLBACK(search_output_copy),out);
 GtkWidget *formats=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),formats,FALSE,FALSE,4);const gchar *field_labels[]={"Copy source path","Line","Column","Text preview"};for(guint i=0;i<4;i++) { out->fields[i]=gtk_check_button_new_with_label(field_labels[i]);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(out->fields[i]),m->search_fields[i]);gtk_box_pack_start(GTK_BOX(formats),out->fields[i],FALSE,FALSE,0);g_object_ref(out->fields[i]);gchar *name=g_strdup_printf("insight-diff-search-field-%u",i);gtk_widget_set_name(out->fields[i],name);g_free(name);g_signal_connect(out->fields[i],"toggled",G_CALLBACK(search_output_columns),out); }
 out->filter=GTK_TREE_MODEL_FILTER(gtk_tree_model_filter_new(GTK_TREE_MODEL(model),NULL));g_object_unref(model);gtk_tree_model_filter_set_visible_func(out->filter,search_output_visible,out,NULL);GtkTreeModel *sorted=gtk_tree_model_sort_new_with_model(GTK_TREE_MODEL(out->filter));GtkWidget *tree=gtk_tree_view_new_with_model(sorted);g_object_unref(sorted);out->tree=tree;gtk_widget_set_name(tree,"insight-diff-search-results-list");g_signal_connect_object(tree,"row-activated",G_CALLBACK(search_result_activated),dialog,0);const gchar *titles[]={"Line","Column","Text preview"};for(guint i=0;i<3;i++) { gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,titles[i],gtk_cell_renderer_text_new(),"text",i+2,NULL);gtk_tree_view_column_set_sort_column_id(gtk_tree_view_get_column(GTK_TREE_VIEW(tree),i),i+2); }search_output_columns(NULL,out);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);out->status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),out->status,FALSE,FALSE,4);
 GtkWidget *preview_label=gtk_label_new("Match context (search snapshot)");gtk_label_set_xalign(GTK_LABEL(preview_label),0);gtk_box_pack_start(GTK_BOX(box),preview_label,FALSE,FALSE,0);GtkWidget *preview_scroll=gtk_scrolled_window_new(NULL,NULL);gtk_widget_set_size_request(preview_scroll,-1,110);out->preview=gtk_text_view_new();gtk_widget_set_name(out->preview,"insight-diff-search-preview");gtk_text_view_set_editable(GTK_TEXT_VIEW(out->preview),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(out->preview),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(out->preview),GTK_WRAP_WORD_CHAR);gtk_text_buffer_create_tag(gtk_text_view_get_buffer(GTK_TEXT_VIEW(out->preview)),"match","background","#ffe080","foreground","#202020",NULL);gtk_container_add(GTK_CONTAINER(preview_scroll),out->preview);gtk_box_pack_start(GTK_BOX(box),preview_scroll,TRUE,TRUE,4);
 GtkWidget *keys=gtk_label_new("F3 / Shift+F3: select match — Ctrl+Enter / Ctrl+L: go to match");gtk_box_pack_start(GTK_BOX(box),keys,FALSE,FALSE,4);
 g_signal_connect(dialog,"key-press-event",G_CALLBACK(search_output_key),out);
 GtkTreeSelection *selection=g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)));g_signal_connect(selection,"changed",G_CALLBACK(search_output_selection),out);if(count) { GtkTreePath *path=gtk_tree_path_new_first();gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path); }search_output_selection(selection,out);g_signal_connect(out->filter_entry,"changed",G_CALLBACK(search_output_filter),out);gtk_widget_show_all(dialog);
 gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
 gchar *open_path=NULL;guint open_line=1;
 if((response==GTK_RESPONSE_APPLY || response==GTK_RESPONSE_YES) && m && !m->busy && m->history_serial==serial) { GtkTreeModel *selected_model;GtkTreeIter iter;if(gtk_tree_selection_get_selected(selection,&selected_model,&iter)) { gint begin,end;gtk_tree_model_get(selected_model,&iter,0,&begin,1,&end,2,&open_line,-1);if(response==GTK_RESPONSE_YES && out->can_edit) open_path=g_strdup(native_source);buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));gtk_text_buffer_get_iter_at_offset(buffer,&a,begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,end);gtk_text_buffer_select_range(buffer,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0,0.5);gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),side);gtk_widget_grab_focus(m->views[side]); } }
 else if((response==GTK_RESPONSE_APPLY || response==GTK_RESPONSE_YES) && m && m->history_serial!=serial) gtk_label_set_text(GTK_LABEL(m->find_status),"Text changed; run Find all again.");
 if(m) for(guint i=0;i<4;i++) m->search_fields[i]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(out->fields[i]));
 out->closing=TRUE;gtk_widget_destroy(dialog);g_object_unref(selection);for(guint i=0;i<4;i++) g_object_unref(out->fields[i]);g_object_unref(out->snapshot);g_object_unref(out->filter);g_free(out->fold);g_free(out->source);g_free(out);g_free(native_source);g_object_unref(dialog);g_object_unref(window);if(open_path && source_opener) source_opener(open_path,open_line);g_free(open_path);
}
static void find_all_clicked(GtkWidget *widget,gpointer data) { (void)widget;Merge *m=data;find_all(m,gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side))); }
static void find_text(Merge *m,gboolean backwards) {
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));if(!*query) { gtk_label_set_text(GTK_LABEL(m->find_status),"Enter search text.");return; }
 if(m->busy) return;
 int side=gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side));
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_regex))) { regex_find(m,side,backwards);return; }
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z,start;
 if(gtk_text_buffer_get_selection_bounds(buffer,&a,&z)) start=backwards ? a : z;
 else gtk_text_buffer_get_iter_at_mark(buffer,&start,gtk_text_buffer_get_insert(buffer));
 GtkTextSearchFlags flags=GTK_TEXT_SEARCH_TEXT_ONLY;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=GTK_TEXT_SEARCH_CASE_INSENSITIVE;
 gboolean whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_word));
 gboolean found=search_match(start,query,flags,backwards,whole,&a,&z);
 gboolean wrapped=!found && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_wrap));
 if(wrapped) {
  if(backwards) gtk_text_buffer_get_end_iter(buffer,&start);else gtk_text_buffer_get_start_iter(buffer,&start);
  found=search_match(start,query,flags,backwards,whole,&a,&z);
 }
 if(found) {
  gtk_text_buffer_select_range(buffer,&a,&z);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0,0.5);
  gchar *label=g_strdup_printf(wrapped ? "Wrapped: line %d" : "Found: line %d",gtk_text_iter_get_line(&a)+1);gtk_label_set_text(GTK_LABEL(m->find_status),label);g_free(label);
 } else gtk_label_set_text(GTK_LABEL(m->find_status),"Text not found.");
}
static void find_clicked(GtkWidget *w,gpointer data) { Merge *m=data;find_text(m,w==m->find_previous); }
static void find_activate(GtkEntry *entry,gpointer data) { (void)entry;find_text(data,FALSE); }
/* Recognize uniform and initial-capital case; mixed identifiers stay literal. */
