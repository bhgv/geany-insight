/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Explicit edits and replacement operations on the merge result.
 * RU: Явные правки и замены в результате сведения. */

static gchar *preserve_case(const gchar *old,const gchar *replacement) {
 guint upper=0,lower=0,cased=0;gboolean initial=TRUE;
 for(const gchar *p=old;*p;p=g_utf8_next_char(p)) {
  gunichar c=g_utf8_get_char(p);gboolean up=g_unichar_isupper(c)||g_unichar_istitle(c),down=g_unichar_islower(c);
  if(up || down) { if((cased==0 && !up) || (cased>0 && !down)) initial=FALSE;cased++;upper+=up;lower+=down; }
 }
 if(upper && !lower) return g_utf8_strup(replacement,-1);
 if(lower && !upper) return g_utf8_strdown(replacement,-1);
 if(initial && cased) {
  gchar *low=g_utf8_strdown(replacement,-1);
  for(gchar *p=low;*p;p=g_utf8_next_char(p)) if(g_unichar_islower(g_utf8_get_char(p)) || g_unichar_isupper(g_utf8_get_char(p))) {
   gchar *next=g_utf8_next_char(p),*up=g_utf8_strup(p,next-p),*prefix=g_strndup(low,p-low),*text=g_strconcat(prefix,up,next,NULL);g_free(up);g_free(prefix);g_free(low);return text;
  }
  return low;
 }
 return g_strdup(replacement);
}
static gchar *toggle_case(const gchar *text) {
 GString *out=g_string_new(NULL);
 for(const gchar *p=text;*p;) { const gchar *next=g_utf8_next_char(p);gunichar c=g_utf8_get_char(p);
  gchar *part=(g_unichar_isupper(c)||g_unichar_istitle(c)) ? g_utf8_strdown(p,next-p) : g_utf8_strup(p,next-p);g_string_append(out,part);g_free(part);p=next;
 }
 return g_string_free(out,FALSE);
}
static void selection_case(Merge *m,guint action) {
 if(m->busy || m->changing) return;
 GtkTextBuffer *buffer=result(m);GtkTextIter a,z;
 if(!gtk_text_buffer_get_selection_bounds(buffer,&a,&z)) return;
 gchar *old=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);
 gchar *text=action==0 ? g_utf8_strup(old,-1) : action==1 ? g_utf8_strdown(old,-1) : toggle_case(old);
 gchar *all=contents(buffer);guint64 bytes=strlen(all)-strlen(old)+strlen(text);g_free(all);
 if(bytes>1024*1024) gtk_label_set_text(GTK_LABEL(m->replace_status),"Case conversion limit: 1 MiB result. Nothing changed.");
 else if(strcmp(old,text)) {
  gint begin=gtk_text_iter_get_offset(&a);gtk_text_buffer_begin_user_action(buffer);gtk_text_buffer_delete(buffer,&a,&z);gtk_text_buffer_insert(buffer,&a,text,-1);gtk_text_buffer_get_iter_at_offset(buffer,&z,begin);gtk_text_buffer_select_range(buffer,&z,&a);gtk_text_buffer_end_user_action(buffer);
  gtk_label_set_text(GTK_LABEL(m->replace_status),"Selection case changed.");
 }
 g_free(text);g_free(old);
}
typedef struct { gint begin,end;gchar *text; } ReplaceRange;

/* Decisions are staged against one immutable revision; Cancel discards all. */
static gboolean confirm_ranges(Merge *m,GArray *ranges) {
 GtkWidget *window=m->window;g_object_ref(window);guint64 serial=m->history_serial;
 GtkWidget *dialog=gtk_dialog_new_with_buttons("Confirm replacements",GTK_WINDOW(window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,"Cancel batch",GTK_RESPONSE_CANCEL,"Skip",GTK_RESPONSE_NO,"Replace",GTK_RESPONSE_YES,"Replace remaining",GTK_RESPONSE_APPLY,NULL);
 g_object_ref(dialog);gtk_widget_set_name(dialog,"insight-diff-confirm-replace");gtk_window_set_default_size(GTK_WINDOW(dialog),680,360);
 GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog)),*label=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,8);
 GtkWidget *view=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,8);
 gboolean ok=TRUE;guint candidate=0,total=ranges->len;
 for(guint i=0;i<ranges->len;) {
  ReplaceRange *range=&g_array_index(ranges,ReplaceRange,i);GtkTextIter a,z;gtk_text_buffer_get_iter_at_offset(result(m),&a,range->begin);gtk_text_buffer_get_iter_at_offset(result(m),&z,range->end);
  gchar *old=gtk_text_buffer_get_text(result(m),&a,&z,TRUE),*old_preview=g_utf8_substring(old,0,MIN(g_utf8_strlen(old,-1),2048)),*new_preview=g_utf8_substring(range->text,0,MIN(g_utf8_strlen(range->text,-1),2048));
  gchar *title=g_strdup_printf("Candidate %u of %u — line %d, column %d",++candidate,total,gtk_text_iter_get_line(&a)+1,gtk_text_iter_get_line_offset(&a)+1);gtk_label_set_text(GTK_LABEL(label),title);
  gchar *preview=g_strdup_printf("Matched text (up to 2048 characters):\n%s\n\nReplacement (up to 2048 characters):\n%s",old_preview,new_preview);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)),preview,-1);
  g_free(title);g_free(preview);g_free(old);g_free(old_preview);g_free(new_preview);gtk_widget_show_all(dialog);
  gint response=gtk_dialog_run(GTK_DIALOG(dialog));m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
  if(!m || m->busy || serial!=m->history_serial) { ok=FALSE;break; }
  if(response==GTK_RESPONSE_APPLY) break;
  if(response==GTK_RESPONSE_YES) i++;
  else if(response==GTK_RESPONSE_NO) { g_free(range->text);g_array_remove_index(ranges,i); }
  else { ok=FALSE;break; }
 }
 gtk_widget_destroy(dialog);g_object_unref(dialog);g_object_unref(window);return ok;
}
static gboolean line_range_add(GArray *ranges,gint begin,gint end,const gchar *text,guint64 removed,guint64 *bytes) {
 ReplaceRange range={begin,end,g_strdup(text)};g_array_append_val(ranges,range);*bytes=*bytes-removed+strlen(text);
 return ranges->len<=100000 && *bytes<=1024*1024;
}
static void edit_lines(Merge *m,guint action) {
 if(m->busy || m->changing || action>3) return;
 GtkTextBuffer *buffer=result(m);GtkTextIter a,z;
 gboolean selection=gtk_text_buffer_get_selection_bounds(buffer,&a,&z);
 if(!selection) gtk_text_buffer_get_iter_at_mark(buffer,&a,gtk_text_buffer_get_insert(buffer));
 gint first=gtk_text_iter_get_line(&a),last=selection ? gtk_text_iter_get_line(&z) : first;
 if(selection && last>first && gtk_text_iter_get_line_offset(&z)==0) last--;
 gtk_spin_button_update(GTK_SPIN_BUTTON(m->edit_tab_width));
 guint width=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->edit_tab_width));
 gchar *all=contents(buffer);guint64 bytes=strlen(all);g_free(all);
 GArray *ranges=g_array_new(FALSE,FALSE,sizeof(ReplaceRange));gboolean ok=TRUE;
 for(gint line=first;line<=last && ok;line++) {
  gtk_text_buffer_get_iter_at_line(buffer,&a,line);z=a;if(!gtk_text_iter_ends_line(&z)) gtk_text_iter_forward_to_line_end(&z);
  gchar *text=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);gint begin=gtk_text_iter_get_offset(&a);
  if(action==0 && *text && *text!='#') { gchar *spaces=g_strnfill(width,' ');ok=line_range_add(ranges,begin,begin,spaces,0,&bytes);g_free(spaces); }
  else if(action==1) {
   guint count=0;if(*text=='\t') count=1;else while(text[count]==' ' && count<width) count++;
   if(count) ok=line_range_add(ranges,begin,begin+count,"",count,&bytes);
  } else if(action==2) {
   guint column=0;gint offset=begin;
   for(const gchar *p=text;*p && ok;p=g_utf8_next_char(p),offset++) {
    if(*p=='\t') { guint spaces=width-column%width;gchar *fill=g_strnfill(spaces,' ');ok=line_range_add(ranges,offset,offset+1,fill,1,&bytes);g_free(fill);column+=spaces; }
    else column++;
   }
  } else if(action==3) {
   gsize length=strlen(text),end=length;while(end && (text[end-1]==' ' || text[end-1]=='\t')) end--;
   if(end<length) { gint offset=begin+g_utf8_strlen(text,end);ok=line_range_add(ranges,offset,gtk_text_iter_get_offset(&z),"",length-end,&bytes); }
  }
  g_free(text);
 }
 if(!ok) gtk_label_set_text(GTK_LABEL(m->replace_status),"Line edit limit: 100000 changes and 1 MiB result. Nothing changed.");
 else if(ranges->len) {
  gtk_text_buffer_begin_user_action(buffer);
  for(guint i=ranges->len;i>0;i--) { ReplaceRange range=g_array_index(ranges,ReplaceRange,i-1);gtk_text_buffer_get_iter_at_offset(buffer,&a,range.begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,range.end);gtk_text_buffer_delete(buffer,&a,&z);gtk_text_buffer_insert(buffer,&a,range.text,-1); }
  gtk_text_buffer_end_user_action(buffer);gtk_label_set_text(GTK_LABEL(m->replace_status),"Result lines edited.");
 } else gtk_label_set_text(GTK_LABEL(m->replace_status),"No changes.");
 for(guint i=0;i<ranges->len;i++) g_free(g_array_index(ranges,ReplaceRange,i).text);
 g_array_unref(ranges);
}
static void replace_result(GtkWidget *widget,gpointer data) {
 Merge *m=data;if(m->busy || m->changing) return;
 const gchar *query=gtk_entry_get_text(GTK_ENTRY(m->find_entry));
 if(!*query) { gtk_label_set_text(GTK_LABEL(m->replace_status),"Enter search text.");return; }
 GtkTextBuffer *buffer=result(m);GtkTextIter start,limit,a,z;gint scope=gtk_combo_box_get_active(GTK_COMBO_BOX(m->replace_scope));gboolean selection_only=scope==1;
 if(selection_only) {
  if(!gtk_text_buffer_get_selection_bounds(buffer,&start,&limit)) { gtk_label_set_text(GTK_LABEL(m->replace_status),"Select result text first.");return; }
 } else {
  gtk_text_buffer_get_bounds(buffer,&start,&limit);
  if(scope==2) { if(gtk_text_buffer_get_selection_bounds(buffer,&a,&z)) start=a;else gtk_text_buffer_get_iter_at_mark(buffer,&start,gtk_text_buffer_get_insert(buffer)); }
 }
 GtkTextSearchFlags flags=GTK_TEXT_SEARCH_TEXT_ONLY;
 if(!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_case))) flags|=GTK_TEXT_SEARCH_CASE_INSENSITIVE;
 gboolean whole=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_word));
 gboolean once=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->replace_once));
 const gchar *replacement=gtk_entry_get_text(GTK_ENTRY(m->replace_entry));
 gboolean keep_case=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->replace_case));
 gchar *original=contents(buffer);guint64 bytes=strlen(original);g_free(original);
 gboolean regex_mode=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->find_regex));GPtrArray *regex_hits=regex_mode ? regex_collect(m,buffer,100000,replacement,NULL,m->replace_status) : NULL;if(regex_mode && !regex_hits) return;guint regex_index=0;
 GArray *ranges=g_array_new(FALSE,FALSE,sizeof(ReplaceRange));gint last_line=-1;gboolean exceeded=FALSE;
 while(regex_mode ? regex_index<regex_hits->len : search_match(start,query,flags,FALSE,whole,&a,&z)) {
  const gchar *current_replacement=replacement;
  if(regex_mode) { RegexHit *hit=g_ptr_array_index(regex_hits,regex_index++);gtk_text_buffer_get_iter_at_offset(buffer,&a,hit->begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,hit->end);current_replacement=hit->replacement;if(gtk_text_iter_compare(&a,&start)<0) continue; }
  if(gtk_text_iter_compare(&z,&limit)>0) { if(regex_mode) continue;break; }
  if(regex_mode && selection_only && gtk_text_iter_equal(&a,&z) && gtk_text_iter_equal(&a,&limit)) continue;
  if(!once || gtk_text_iter_get_line(&a)!=last_line) {
   gchar *old=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);gsize old_bytes=strlen(old);
   /* A no-op must not create history or consume the bounded match budget. */
   gchar *text=keep_case ? preserve_case(old,current_replacement) : g_strdup(current_replacement);gsize replacement_bytes=strlen(text);
   gboolean same=!strcmp(old,text);g_free(old);
   last_line=gtk_text_iter_get_line(&a);
   if(!same) { ReplaceRange range={gtk_text_iter_get_offset(&a),gtk_text_iter_get_offset(&z),text};g_array_append_val(ranges,range);bytes=bytes-old_bytes+replacement_bytes;
    if(ranges->len>100000 || bytes>1024*1024) { exceeded=TRUE;break; }
   } else g_free(text);
  }
  start=z;
 }
 if(regex_hits) g_ptr_array_unref(regex_hits);
 if(exceeded) gtk_label_set_text(GTK_LABEL(m->replace_status),"Replacement limit: 100000 changes and 1 MiB result. Nothing changed.");
 else if(widget!=m->replace_apply) { gchar *label=g_strdup_printf("Preview: %u replacements",ranges->len);gtk_label_set_text(GTK_LABEL(m->replace_status),label);g_free(label); }
 else if(ranges->len) {
  GtkWidget *window=m->window;g_object_ref(window);gboolean approved=TRUE;
  if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->replace_confirm))) approved=confirm_ranges(m,ranges);
  m=g_object_get_data(G_OBJECT(window),"insight-diff-state");
  if(!m) { g_object_unref(window);goto cleanup_replacements; }
  g_object_unref(window);
  if(!approved) { gtk_label_set_text(GTK_LABEL(m->replace_status),"Replacement batch cancelled or text changed. Nothing applied.");goto cleanup_replacements; }
  if(!ranges->len) { gtk_label_set_text(GTK_LABEL(m->replace_status),"No changes.");goto cleanup_replacements; }
  gtk_text_buffer_begin_user_action(buffer);
  /* Reverse edits preserve offsets and tracked merge marks. */
  for(guint i=ranges->len;i>0;i--) { ReplaceRange range=g_array_index(ranges,ReplaceRange,i-1);gtk_text_buffer_get_iter_at_offset(buffer,&a,range.begin);gtk_text_buffer_get_iter_at_offset(buffer,&z,range.end);gtk_text_buffer_delete(buffer,&a,&z);gtk_text_buffer_insert(buffer,&a,range.text,-1); }
  gtk_text_buffer_end_user_action(buffer);
  gchar *label=g_strdup_printf("Replaced: %u",ranges->len);gtk_label_set_text(GTK_LABEL(m->replace_status),label);g_free(label);
 } else gtk_label_set_text(GTK_LABEL(m->replace_status),"No changes.");
 cleanup_replacements:
 for(guint i=0;i<ranges->len;i++) g_free(g_array_index(ranges,ReplaceRange,i).text);
 g_array_unref(ranges);
}
