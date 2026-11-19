/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Difference selection, source menus and result block acceptance.
 * RU: Выбор различий, меню исходников и принятие блоков в результат. */

static gint selected(Merge *m) {
 GtkTreeIter it;GtkTreeModel *model;gint id=-1;
 if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->hunk_view)),&model,&it)) gtk_tree_model_get(model,&it,0,&id,-1);
 return id;
}
/* EN: Refresh m's difference actions/status without changing source carets or scroll positions; return the selected block ID.
 * RU: Обновляет команды/статус различия m без изменения курсоров и прокрутки исходников; возвращает ID выбранного блока. */
static gint difference_controls(Merge *m)
{
    gint id = selected(m);
    for (guint side = 0; side < 2; side++)
    {
        gtk_widget_set_sensitive(m->take[side], !m->busy && !m->browse && id >= 0);
        gtk_widget_set_sensitive(m->take_all[side], !m->busy && !m->browse && m->diff.count > 0);
    }
    gtk_widget_set_sensitive(m->previous, m->diff.count && id != 0);
    gtk_widget_set_sensitive(m->next, m->diff.count && (id < 0 || (gsize)(id + 1) < m->diff.count));
    if (id >= 0 && (gsize)id < m->diff.count)
    {
        gchar *label = g_strdup_printf("Difference %d of %zu", id + 1, m->diff.count);
        message(m, label);
        g_free(label);
    }
    return id;
}

static void navigate(Merge *m,gint direction) {
 if(!m->diff.count) return;
 gint id=selected(m);
 id=id<0 ? (direction>0 ? 0 : (gint)m->diff.count-1) : id+direction;
 if(id<0 || (size_t)id>=m->diff.count) return;
 GtkTreePath *path=gtk_tree_path_new_from_indices(id,-1);
 gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE);
 gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(m->hunk_view),path,NULL,FALSE,0,0);
 gtk_tree_path_free(path);
}
static void step(GtkWidget *w,gpointer data) { Merge *m=data;navigate(m,w==m->next ? 1 : -1); }
static void show_list(GtkToggleButton *w,gpointer data) { gtk_widget_set_visible(((Merge*)data)->list_scroll,gtk_toggle_button_get_active(w)); }
static void wrap(GtkToggleButton *w,gpointer data) {
 Merge *m=data;
 for(int i=0;i<3;i++) gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(m->views[i]),i==2&&gtk_toggle_button_get_active(w) ? GTK_WRAP_WORD_CHAR : GTK_WRAP_NONE);
 alignment_queue(m);
}
static void sync_scroll(GtkAdjustment *adjustment,gpointer data) {
 Merge *m=data;
 if(m->scrolling || !m->sources[0] || !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->sync))) return;
 m->scrolling=TRUE;
 int side=adjustment==m->adjustments[1];
 if(m->aligned) { gtk_adjustment_set_value(m->adjustments[1-side],gtk_adjustment_get_value(adjustment));m->scrolling=FALSE;return; }
 GdkRectangle rect;GtkTextIter it;
 gtk_text_view_get_visible_rect(GTK_TEXT_VIEW(m->views[side]),&rect);
 gtk_text_view_get_line_at_y(GTK_TEXT_VIEW(m->views[side]),&it,rect.y,NULL);
 size_t line=insight_diff_map_line(&m->diff,side,m->sources[side],m->sources[1-side],(size_t)gtk_text_iter_get_line(&it));
 GtkTextBuffer *target=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1-side]));
 gtk_text_buffer_get_iter_at_line(target,&it,(gint)line);
 gint y,height;gtk_text_view_get_line_yrange(GTK_TEXT_VIEW(m->views[1-side]),&it,&y,&height);
 /* Adjustment updates are synchronous: no deferred scroll callback can feed
  * back into the source after the recursion guard has been released. */
 gtk_adjustment_set_value(m->adjustments[1-side],y);
 m->scrolling=FALSE;
}
static void selection(GtkTreeSelection *s, gpointer data);

/* EN: Track an ordinary primary text press in w for m in data, preserving GTK caret/selection handling.
 * Ignore busy sessions and modified/multiple clicks. Always return FALSE so GTK can place or extend the text caret.
 * RU: Отслеживает обычное нажатие в тексте w для m из data, сохраняя обработку курсора/выделения GTK.
 * Пропускает занятые сеансы, модификаторы и кратные щелчки. Всегда FALSE для обычной обработки текста GTK. */
static gboolean pick_block(GtkWidget *w, GdkEventButton *event, gpointer data)
{
    Merge *m = data;
    guint side = w == m->views[1];
    if (event->button != 1 || event->type != GDK_BUTTON_PRESS || m->busy || m->changing ||
        !m->sources[side] || !m->diff.count || (event->state & gtk_accelerator_get_default_mod_mask()) ||
        gtk_text_view_get_window_type(GTK_TEXT_VIEW(w), event->window) != GTK_TEXT_WINDOW_TEXT)
        return FALSE;
    gint x, y;
    GtkTextIter at;
    gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(w), GTK_TEXT_WINDOW_TEXT,
        (gint)event->x, (gint)event->y, &x, &y);
    gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(w), &at, x, y);
    gsize offset = g_utf8_offset_to_pointer(m->sources[side], gtk_text_iter_get_offset(&at)) - m->sources[side];
    for (gsize i = 0; i < m->diff.count; i++)
    {
        InsightDiffHunk *hunk = &m->diff.hunks[i];
        gsize begin = side ? hunk->b_begin : hunk->a_begin;
        gsize end = side ? hunk->b_end : hunk->a_end;
        if (offset < begin || (offset >= end && (begin != end || offset != begin)))
            continue;
        GtkTreePath *path = gtk_tree_path_new_from_indices((gint)i, -1);
        if (selected(m) != (gint)i)
        {
            g_signal_handlers_block_by_func(m->selection, G_CALLBACK(selection), m);
            gtk_tree_view_set_cursor(GTK_TREE_VIEW(m->hunk_view), path, NULL, FALSE);
            g_signal_handlers_unblock_by_func(m->selection, G_CALLBACK(selection), m);
        }
        gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(m->hunk_view), path, NULL, FALSE, 0, 0);
        gtk_tree_path_free(path);
        difference_controls(m);
        break;
    }
    return FALSE;
}
static gint chars(const gchar *s,size_t offset) { return (gint)g_utf8_pointer_to_offset(s,s+offset); }
static void highlight(Merge *m,int side,size_t begin,size_t end) {
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter a,z;
 gtk_text_buffer_get_iter_at_offset(b,&a,chars(m->sources[side],begin));gtk_text_buffer_get_iter_at_offset(b,&z,chars(m->sources[side],end));
 gtk_text_buffer_apply_tag_by_name(b,"difference",&a,&z);
}
/* EN: Select m's current difference from s/data, update guarded merge actions and scroll snapshots; no source bytes change.
 * RU: Выбирает различие m по s/data, обновляет доступность сведения и прокручивает снимки без изменения байтов исходников. */
static void selection(GtkTreeSelection *s,gpointer data)
{
    (void)s;
    Merge *m=data;
    if(m->changing) return;
    gint id=difference_controls(m);
    if(id<0 || (size_t)id>=m->diff.count) return;
    InsightDiffHunk *h=&m->diff.hunks[id];
    m->scrolling=TRUE;
    for(int side=0;side<3;side++)
    {
        GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));
        GtkTextIter a,z;
        if(side==2)
        {
            gtk_text_buffer_get_iter_at_mark(b,&a,m->marks[2*id]);
            gtk_text_buffer_get_iter_at_mark(b,&z,m->marks[2*id+1]);
        }
        else
        {
            gtk_text_buffer_get_iter_at_offset(b,&a,chars(m->sources[side],side ? h->b_begin : h->a_begin));
            gtk_text_buffer_get_iter_at_offset(b,&z,chars(m->sources[side],side ? h->b_end : h->a_end));
        }
        gtk_text_buffer_select_range(b,&a,&z);
        gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&a,0.1,TRUE,0.0,0.5);
    }
    m->scrolling=FALSE;

}
static void edit_source(Merge *m,guint side) {
 if(side>1 || m->busy || !source_opener || !m->loaded_paths[side]) return;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter iter;gtk_text_buffer_get_iter_at_mark(buffer,&iter,gtk_text_buffer_get_insert(buffer));guint line=gtk_text_iter_get_line(&iter)+1;gchar *path=g_strdup(m->loaded_paths[side]);source_opener(path,line);g_free(path);
}
static void copy_difference(Merge *m,guint side) {
 gint id=selected(m);if(side>1 || m->busy || m->changing || id<0 || (gsize)id>=m->diff.count) return;
 InsightDiffHunk *h=&m->diff.hunks[id];gsize begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;
 gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),m->sources[side]+begin,(gint)(end-begin));
}
static void write_result(Merge *,guint);
static void source_menu_action(GtkMenuItem *item,gpointer window) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m) return;
 gint side=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"side"));gint action=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item),"action"));
 if(action>=16) { if(action==19) find_all(m,side);else selection_search(m,side,action==16 ? 0 : action==17 ? 1 : -1); }else if(action==0) edit_source(m,side);else if(action==1) copy_difference(m,side);else if(action==2) view_options(GTK_WIDGET(item),m);else if(action==3) focus_line(m,side);else if(side==2 && action<7) selection_case(m,action-4);else if(side==2 && action<11) edit_lines(m,action-7);else if(side==2 && action<13) history_end(m,action==12);else if(side==2 && action==13) history_dialog(NULL,m);else if(side==2) write_result(m,action-13);
}
static void view_menu_toggle(GtkCheckMenuItem *item,gpointer toggle) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(toggle),gtk_check_menu_item_get_active(item)); }
static void source_menu(GtkTextView *view,GtkWidget *menu,gpointer data) {
 Merge *m=data;guint side=GTK_WIDGET(view)==m->views[1] ? 1 : GTK_WIDGET(view)==m->views[0] ? 0 : 2;
 gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
 const gchar *labels[]={"Edit File — Ctrl+E","Copy selected difference","File Compare Options…","Go to Line — Ctrl+G","Uppercase selection","Lowercase selection","Toggle selection case","Indent result lines","Outdent result lines","Tabs to spaces in result lines","Trim trailing spaces in result lines","Undo All retained changes","Redo All retained changes","Result History…","Save a copy of Result…","Save Result selection…","Load Search String","Search identifier forward — Ctrl+F3","Search identifier backward — Ctrl+Shift+F3","Find all matches in pane — Ctrl+Shift+F"};
 for(guint action=0;action<G_N_ELEMENTS(labels);action++) {
  if((side==2 && action<2) || (side!=2 && action>=4 && action<16)) continue;
  GtkWidget *item=gtk_menu_item_new_with_label(labels[action]);g_object_set_data(G_OBJECT(item),"side",GUINT_TO_POINTER(side));g_object_set_data(G_OBJECT(item),"action",GUINT_TO_POINTER(action));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect_object(item,"activate",G_CALLBACK(source_menu_action),m->window,0);
  if(action>=4) gtk_widget_set_sensitive(item,!m->busy && (action>=7 || gtk_text_buffer_get_has_selection(result(m))));
  if(action>=11) gtk_widget_set_sensitive(item,!m->busy && (action==11 ? m->position>0 : action==12 ? m->position+1<m->history->len : m->history->len>0));
  if(action>=14) gtk_widget_set_sensitive(item,!m->busy && (action==14 || gtk_text_buffer_get_has_selection(result(m))));
  if(action>=16) gtk_widget_set_sensitive(item,!m->busy);
  if(action<2) gtk_widget_set_sensitive(item,!m->busy && m->loaded_paths[side] && (action==0 ? source_opener!=NULL : selected(m)>=0));
 }
 GtkWidget *toggles[]={m->mono,m->numbers,m->list_toggle,m->wrap_toggle,m->vertical,m->overview,m->visible_space,m->visible_eol};
 for(guint i=0;i<G_N_ELEMENTS(toggles);i++) { GtkWidget *item=gtk_check_menu_item_new_with_label(gtk_button_get_label(GTK_BUTTON(toggles[i])));gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item),gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggles[i])));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect_object(item,"toggled",G_CALLBACK(view_menu_toggle),toggles[i],0); }
 gtk_widget_show_all(menu);
}
static void result_menu_show(GtkWidget *menu,gpointer window) {
 Merge *m=g_object_get_data(G_OBJECT(window),"insight-diff-state");if(!m) return;
 GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) gtk_widget_destroy(p->data);g_list_free(children);
 source_menu(GTK_TEXT_VIEW(m->views[2]),menu,m);
}
static void take(GtkWidget *w,gpointer data) {
 Merge *m=data;gint id=selected(m);if(m->busy || id<0 || (size_t)id>=m->diff.count) return;
 int side=w==m->take[1];InsightDiffHunk *h=&m->diff.hunks[id];
 size_t begin=side?h->b_begin:h->a_begin,end=side?h->b_end:h->a_end;
 GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(result(m),&a,m->marks[2*id]);gtk_text_buffer_get_iter_at_mark(result(m),&z,m->marks[2*id+1]);
 gchar *current=gtk_text_buffer_get_text(result(m),&a,&z,TRUE);
 gboolean same=strlen(current)==end-begin && !memcmp(current,m->sources[side]+begin,end-begin);g_free(current);
 if(same) { if(m->resolutions[id]!=side+1) { m->resolutions[id]=side+1;record(m); }return; }
 m->resolutions[id]=side+1;m->changing=TRUE;gtk_text_buffer_begin_user_action(result(m));
 gtk_text_buffer_delete(result(m),&a,&z);gtk_text_buffer_insert(result(m),&a,m->sources[side]+begin,(gint)(end-begin));
 gtk_text_buffer_end_user_action(result(m));m->changing=FALSE;record(m);selection(NULL,m);
}
/* Snapshot every range before editing. Reverse replacement leaves earlier
 * offsets stable; touching/collapsed boundaries after manual edits are rejected. */
static void take_all(GtkWidget *widget,gpointer data) {
 Merge *m=data;if(m->busy || !m->diff.count) return;
 gint side=widget==m->take_all[1];gint *ranges=g_new(gint,2*m->diff.count);gboolean any=FALSE;
 for(size_t i=0;i<m->diff.count;i++) {
  GtkTextIter a,z;gtk_text_buffer_get_iter_at_mark(result(m),&a,m->marks[2*i]);gtk_text_buffer_get_iter_at_mark(result(m),&z,m->marks[2*i+1]);
  ranges[2*i]=gtk_text_iter_get_offset(&a);ranges[2*i+1]=gtk_text_iter_get_offset(&z);
  if(ranges[2*i]>ranges[2*i+1] || (i && ranges[2*i]<=ranges[2*i-1])) {
   g_free(ranges);message(m,"Block boundaries overlap or touch after edits; compare again before taking all blocks.");return;
  }
  InsightDiffHunk *h=&m->diff.hunks[i];size_t begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;
  gchar *text=gtk_text_buffer_get_text(result(m),&a,&z,TRUE);
  if(strlen(text)!=end-begin || memcmp(text,m->sources[side]+begin,end-begin)) any=TRUE;
  g_free(text);
 }
 for(guint i=0;i<m->diff.count;i++) if(m->resolutions[i]!=side+1) any=TRUE;
 if(!any) { g_free(ranges);return; }
 for(guint i=0;i<m->diff.count;i++) m->resolutions[i]=side+1;
 m->changing=TRUE;gtk_text_buffer_begin_user_action(result(m));
 for(size_t i=m->diff.count;i-->0;) {
  InsightDiffHunk *h=&m->diff.hunks[i];size_t begin=side ? h->b_begin : h->a_begin,end=side ? h->b_end : h->a_end;
  GtkTextIter a,z;gtk_text_buffer_get_iter_at_offset(result(m),&a,ranges[2*i]);gtk_text_buffer_get_iter_at_offset(result(m),&z,ranges[2*i+1]);
  gtk_text_buffer_delete(result(m),&a,&z);gtk_text_buffer_insert(result(m),&a,m->sources[side]+begin,(gint)(end-begin));
 }
 gtk_text_buffer_end_user_action(result(m));m->changing=FALSE;g_free(ranges);record(m);selection(NULL,m);
}
