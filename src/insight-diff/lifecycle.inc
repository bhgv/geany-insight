/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Comparison close guards and owned-resource cleanup.
 * RU: Проверки закрытия сравнения и освобождение собственных ресурсов. */

static gboolean discard(Merge *m) {
 gchar *text=contents(result(m));gboolean dirty=g_strcmp0(text,m->saved)!=0;g_free(text);
 if(!dirty) return TRUE;
 GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(m->window),GTK_DIALOG_MODAL,GTK_MESSAGE_QUESTION,GTK_BUTTONS_NONE,"Discard unsaved merge result?");
 gtk_dialog_add_buttons(GTK_DIALOG(d),"Cancel",GTK_RESPONSE_CANCEL,"Discard",GTK_RESPONSE_ACCEPT,NULL);
 gboolean ok=gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT;gtk_widget_destroy(d);return ok;
}
static gboolean closing(GtkWidget *w,GdkEvent *event,gpointer data) { (void)w;(void)event;return !discard(data); }
/* EN: Release m's definition ranges, alignment, hunk marks and result history before replacement or destruction.
 * RU: Освобождает диапазоны определений, выравнивание, метки блоков и историю m перед заменой либо уничтожением. */
static void clear_diff(Merge *m)
{
    for(guint side=0;side<2;side++) g_clear_pointer(&m->symbols[side],g_ptr_array_unref);
    g_clear_pointer(&m->symbol_history, g_array_unref);
    alignment_clear(m);
    for(size_t i=0;i<2*m->diff.count;i++) gtk_text_buffer_delete_mark(result(m),m->marks[i]);
    g_clear_pointer(&m->resolutions,g_free);
    g_clear_pointer(&m->marks,g_free);
    insight_diff_clear(&m->diff);
    g_ptr_array_set_size(m->history,0);
    m->position=0;
}
/* EN: Release the comparison in data after w is destroyed, including private snapshot ranges; cancel pending work.
 * RU: Освобождает сравнение data после уничтожения w, включая диапазоны снимков; отменяет незавершённую работу. */
static void destroyed(GtkWidget *w,gpointer data)
{
    (void)w;
    Merge *m=data;
    if(!view_defaults) view_defaults=g_key_file_new();
    view_save(m,view_defaults);
    open_compares=g_list_remove(open_compares,m);
    g_object_set_data(G_OBJECT(m->window),"insight-diff-state",NULL);
    if(m->alignment_idle) g_source_remove(m->alignment_idle);
    if(m->syntax_idle) g_source_remove(m->syntax_idle);
    for(guint i=0;i<3;i++)
    {
        g_signal_handlers_disconnect_by_data(m->syntax_buffers[i],m);
        g_object_unref(m->syntax_buffers[i]);
    }
    g_clear_pointer(&m->answers,g_ptr_array_unref);
    g_free(m->answer_base);
    g_free(m->syntax_path);
    if(m->cancel) g_cancellable_cancel(m->cancel);
    g_clear_object(&m->cancel);
    /* Child text widgets can already be destroyed here; their buffer owns the marks. */ g_signal_handlers_disconnect_by_data(m->output,m);
    g_signal_handlers_disconnect_by_data(m->selection,m);
    for(int i=0;i<2;i++)
    {
        g_signal_handlers_disconnect_by_data(m->adjustments[i],m);
        g_object_unref(m->adjustments[i]);
        g_signal_handlers_disconnect_by_data(m->horizontal[i],m);
        g_object_unref(m->horizontal[i]);
        g_ptr_array_unref(m->alignment[i]);
    }
    g_object_unref(m->output);
    g_object_unref(m->selection);
    g_free(m->resolutions);
    g_free(m->marks);
    insight_diff_clear(&m->diff);
    g_ptr_array_free(m->history,TRUE);
    for(int i=0;i<2;i++)
    {
        g_clear_pointer(&m->symbols[i],g_ptr_array_unref);
        g_free(m->sources[i]);
        g_free(m->loaded_paths[i]);
        g_free(m->picked[i]);
    }
    for(gint i=0;i<2;i++)
    {
        g_clear_pointer(&m->map_blocks[i],g_array_unref);
        g_clear_object(&m->maps[i]);
    }
    GtkWidget *controls[G_N_ELEMENTS(view_checks)];
    view_controls(m,controls);
    for(guint i=0;i<G_N_ELEMENTS(view_checks);i++) g_object_unref(controls[i]);
    g_object_unref(m->zoom);
    g_clear_object(&m->font_css);
    g_free(m->saved);
    g_object_unref(m->list);
    if(m->review_destroy) m->review_destroy(m->review_data);
    g_clear_pointer(&m->symbol_history, g_array_unref);
    g_free(m);
}
