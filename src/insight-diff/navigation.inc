/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Search focus, line navigation and comparison keyboard commands.
 * RU: Фокус поиска, переходы по строкам и команды клавиатуры сравнения. */

static void focus_find(Merge *m) {
 if(m->toolbar) gtk_notebook_set_current_page(GTK_NOTEBOOK(m->toolbar),3);
 GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(m->window));
 for(int i=0;i<3;i++) if(focus==m->views[i]) {
  gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),i);
  GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i]));GtkTextIter a,z;
  if(gtk_text_buffer_get_selection_bounds(buffer,&a,&z) && gtk_text_iter_get_offset(&z)-gtk_text_iter_get_offset(&a)<=4096) {
   gchar *selection=gtk_text_buffer_get_text(buffer,&a,&z,TRUE);
   if(!strchr(selection,'\n') && !strchr(selection,'\r')) { gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_regex),FALSE);gtk_entry_set_text(GTK_ENTRY(m->find_entry),selection); }
   g_free(selection);
  }
 }
 gtk_widget_grab_focus(m->find_entry);gtk_editable_select_region(GTK_EDITABLE(m->find_entry),0,-1);
}
static gint focused_pane(Merge *m) {
 GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(m->window));
 for(gint side=0;side<3;side++) if(focus==m->views[side]) return side;
 return CLAMP(gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)),0,2);
}
static void focus_line(Merge *m,gint side) {
 if(m->toolbar) gtk_notebook_set_current_page(GTK_NOTEBOOK(m->toolbar),5);
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter at;
 gtk_text_buffer_get_iter_at_mark(buffer,&at,gtk_text_buffer_get_insert(buffer));
 gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side),side);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),gtk_text_iter_get_line(&at)+1);
 gtk_widget_grab_focus(m->line_number);gtk_editable_select_region(GTK_EDITABLE(m->line_number),0,-1);
}
static void go_line(GtkWidget *widget,gpointer data) {
 (void)widget;Merge *m=data;if(m->busy) return;
 gint side=CLAMP(gtk_combo_box_get_active(GTK_COMBO_BOX(m->line_side)),0,2);
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[side]));GtkTextIter at,end;
 gtk_spin_button_update(GTK_SPIN_BUTTON(m->line_number));
 gint line=MIN(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->line_number)),gtk_text_buffer_get_line_count(buffer));
 gtk_text_buffer_get_iter_at_line(buffer,&at,line-1);end=at;
 if(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->line_select))) {
  if(!gtk_text_iter_ends_line(&end)) gtk_text_iter_forward_to_line_end(&end);
  gtk_text_buffer_select_range(buffer,&at,&end);
 } else gtk_text_buffer_place_cursor(buffer,&at);
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),line);
 gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(m->views[side]),&at,0.1,TRUE,0,0.5);gtk_widget_grab_focus(m->views[side]);
 gchar *label=g_strdup_printf("Line %d of %d",line,gtk_text_buffer_get_line_count(buffer));gtk_label_set_text(GTK_LABEL(m->line_status),label);g_free(label);
}
static void line_begin(GtkWidget *widget,gpointer data) { Merge *m=data;gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->line_number),widget==g_object_get_data(G_OBJECT(m->window),"line-first") ? 1 : G_MAXINT);go_line(widget,m); }
static void selection_search(Merge *,guint,gint);
static void find_all(Merge *,guint);
/* EN: Dispatch event for comparison m in data; w identifies the focused control. Read-only mode consumes hidden Result undo; return whether handled.
 * RU: Обрабатывает event сравнения m из data; w задаёт элемент фокуса. Просмотр не меняет скрытый Result через Undo; возвращает признак обработки. */
static gboolean key(GtkWidget *w,GdkEventKey *event,gpointer data)
{
    (void)w;
    Merge *m=data;
    if(m->browse && (event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_h || event->keyval==GDK_KEY_H)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mode),TRUE);
    if(m->browse && (event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_z || event->keyval==GDK_KEY_Z || event->keyval==GDK_KEY_y || event->keyval==GDK_KEY_Y)) return TRUE;
    if(m->review_tools && (event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F || event->keyval==GDK_KEY_g || event->keyval==GDK_KEY_G)) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->review_tools),TRUE);
    if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_g || event->keyval==GDK_KEY_G))
    {
        focus_line(m,focused_pane(m));
        return TRUE;
    }
    if(event->keyval==GDK_KEY_F6 && !(event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK)))
    {
        gint count=m->browse ? 2:3;
        gint side=(focused_pane(m)+((event->state&GDK_SHIFT_MASK) ? count-1:1))%count;
        gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side),side);
        gtk_widget_grab_focus(m->views[side]);
        return TRUE;
    }
    if(event->keyval==GDK_KEY_Escape && w==m->line_number)
    {
        gtk_widget_grab_focus(m->views[focused_pane(m)]);
        return TRUE;
    }
    if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_h || event->keyval==GDK_KEY_H))
    {
        if(m->review_tools) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->review_tools),TRUE);
        if(m->toolbar) gtk_notebook_set_current_page(GTK_NOTEBOOK(m->toolbar),4);
        gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),2);
        gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),gtk_text_buffer_get_has_selection(result(m)) ? 1 : 0);
        gtk_widget_grab_focus(m->replace_entry);
        gtk_editable_select_region(GTK_EDITABLE(m->replace_entry),0,-1);
        return TRUE;
    }
    if((event->state&GDK_CONTROL_MASK) && event->keyval==GDK_KEY_F3)
    {
        selection_search(m,focused_pane(m),(event->state&GDK_SHIFT_MASK) ? -1 : 1);
        return TRUE;
    }
    if((event->state&(GDK_CONTROL_MASK|GDK_SHIFT_MASK))==(GDK_CONTROL_MASK|GDK_SHIFT_MASK) && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F))
    {
        GtkWidget *focus=gtk_window_get_focus(GTK_WINDOW(m->window));
        guint side=gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side));
        for(guint i=0;i<3;i++) if(focus==m->views[i]) side=i;
        find_all(m,side);
        return TRUE;
    }
    if(event->keyval==GDK_KEY_F3)
    {
        find_text(m,(event->state&GDK_SHIFT_MASK)!=0);
        return TRUE;
    }
    if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_f || event->keyval==GDK_KEY_F))
    {
        focus_find(m);
        return TRUE;
    }
    if(event->keyval==GDK_KEY_Escape && w==m->find_entry)
    {
        gtk_widget_grab_focus(m->views[gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side))]);
        return TRUE;
    }
    if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_q || event->keyval==GDK_KEY_Q))
    {
        view_options(NULL,m);
        return TRUE;
    }
    if((event->state&GDK_CONTROL_MASK) && (event->keyval==GDK_KEY_e || event->keyval==GDK_KEY_E))
    {
        GtkWidget *focus=w==m->window ? gtk_window_get_focus(GTK_WINDOW(m->window)) : w;
        for(guint i=0;i<2;i++) if(focus==m->views[i])
        {
            edit_source(m,i);
            return TRUE;
        }
    }
    if(m->busy) return FALSE;
    if((event->state&GDK_MOD1_MASK) && (event->keyval==GDK_KEY_Left || event->keyval==GDK_KEY_Right))
    {
        gint side=event->keyval==GDK_KEY_Right;
        if(event->state&GDK_SHIFT_MASK) take_all(m->take_all[side],m);
        else take(m->take[side],m);
        return TRUE;
    }
    if(!(event->state&GDK_CONTROL_MASK)) return FALSE;
    if(event->keyval==GDK_KEY_Up || event->keyval==GDK_KEY_Down)
    {
        navigate(data,event->keyval==GDK_KEY_Up ? -1 : 1);
        return TRUE;
    }
    if(event->keyval==GDK_KEY_z || event->keyval==GDK_KEY_Z)
    {
        if(event->state&GDK_SHIFT_MASK) redo(NULL,data);
        else undo(NULL,data);
        return TRUE;
    }
    if(event->keyval==GDK_KEY_y || event->keyval==GDK_KEY_Y)
    {
        redo(NULL,data);
        return TRUE;
    }
    return FALSE;
}
static void message(Merge *m,const gchar *text) { gtk_label_set_text(GTK_LABEL(m->status),text); }
/* Identifier boundaries deliberately include underscores and Unicode marks. */
