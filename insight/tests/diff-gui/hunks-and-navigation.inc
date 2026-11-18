/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Retained main-fixture regressions for hunks and navigation.
 * RU: Сохранённые проверки основной GTK-фикстуры: hunks and navigation. */
    g_setenv("GSETTINGS_BACKEND","memory",TRUE);
    gtk_init(&argc,&argv);
    file_compare_modes_test();
    file_compare_symbol_navigation_test();
    file_compare_symbol_browser_test();
    file_compare_symbol_changes_test();
    line_navigation_test();
    search_options_test();
    replace_test();
    replace_scope_test();
    confirm_replace_test();
    case_test();
    line_edit_test();
    history_test();
    export_test();
    whitespace_test();
    search_actions_test();
    search_output_test();
    search_preview_test();
    output_limit_test();
    regex_search_test();
    regex_replace_test();
    regex_limits_test();
    insight_diff_set_source_opener(source_opened);
    GError *error=NULL;
    gchar *dir=g_dir_make_tmp("insight-diff-XXXXXX",&error);
    g_assert_no_error(error);
    gchar *left=g_build_filename(dir,"left.txt",NULL),*right=g_build_filename(dir,"right.txt",NULL);
    const char *a="start\nleft\ncommon\nlast left\nend\n",*b="start\nright\ncommon\nlast right\nextra\nend\n";
    g_assert_true(g_file_set_contents(left,a,-1,&error));
    g_assert_no_error(error);
    g_assert_true(g_file_set_contents(right,b,-1,&error));
    g_assert_no_error(error);
    Merge *m=merge_new(NULL);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_space),FALSE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->visible_eol),FALSE);
    flush();
    g_assert_false(gtk_widget_get_sensitive(m->next));
    gtk_entry_set_text(GTK_ENTRY(m->paths[0]),left);
    gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
    compare(NULL,m);
    wait_comparison(m);
    g_assert_cmpuint(m->diff.count,==,2);
    g_assert_cmpint(selected(m),==,0);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->headers[0])),left));
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->headers[1])),right));
    g_assert_nonnull(strstr(gtk_tree_view_column_get_title(m->file_columns[0]),left));
    g_assert_cmpint(gtk_tree_view_get_n_columns(GTK_TREE_VIEW(m->hunk_view)),==,4);
    g_assert_cmpuint(m->resolutions[0],==,0);
    take(m->take[0],m);
    g_assert_cmpuint(m->resolutions[0],==,1);
    undo(NULL,m);
    g_assert_cmpuint(m->resolutions[0],==,0);
    take(m->take[1],m);
    g_assert_cmpuint(m->resolutions[0],==,2);
    GtkTextIter edit;
    gtk_text_buffer_get_iter_at_mark(result(m),&edit,m->marks[0]);
    gtk_text_buffer_insert(result(m),&edit,"manual ",-1);
    g_assert_cmpuint(m->resolutions[0],==,3);
    g_assert_cmpuint(m->resolutions[1],==,0);
    undo(NULL,m);
    g_assert_cmpuint(m->resolutions[0],==,2);
    undo(NULL,m);
    g_assert_cmpuint(m->resolutions[0],==,0);
    history_end(m,FALSE);
    assert_text(m,a);
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->destination_kind),1);
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->destination)),==,right);
    gtk_entry_set_text(GTK_ENTRY(m->destination),"typed-output.txt");
    g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->destination_kind)),==,2);
    g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(m->destination)),==,"typed-output.txt");
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->destination_kind),2);
    gtk_entry_set_text(GTK_ENTRY(m->destination),"separate-output.txt");
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->headers[2])),"separate-output.txt"));
    gtk_entry_set_text(GTK_ENTRY(m->destination),"");
    g_assert_false(gtk_widget_get_sensitive(m->previous));
    g_assert_true(gtk_widget_get_sensitive(m->next));
    step(m->next,m);
    g_assert_cmpint(selected(m),==,1);
    step(m->next,m);
    g_assert_cmpint(selected(m),==,1);
    g_assert_false(gtk_widget_get_sensitive(m->next));
    step(m->previous,m);
    g_assert_cmpint(selected(m),==,0);
    GtkTextIter click;
    GtkTextBuffer *left_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[0]));
    gtk_text_buffer_get_iter_at_line(left_buffer,&click,3);
    GdkRectangle where;
    gtk_text_view_get_iter_location(GTK_TEXT_VIEW(m->views[0]),&click,&where);
    gint x,y;
    gtk_text_view_buffer_to_window_coords(GTK_TEXT_VIEW(m->views[0]),GTK_TEXT_WINDOW_TEXT,where.x,where.y+where.height/2,&x,&y);
    GdkEventButton mouse=
    {
        0
    }
    ;
    mouse.button=1;
    mouse.window=gtk_text_view_get_window(GTK_TEXT_VIEW(m->views[0]),GTK_TEXT_WINDOW_TEXT);
    mouse.x=x;
    mouse.y=y;
    g_assert_true(pick_block(m->views[0],&mouse,m));
    g_assert_cmpint(selected(m),==,1);
    navigate(m,-1);
    GdkEventKey event=
    {
        0
    }
    ;
    event.state=GDK_CONTROL_MASK;
    event.keyval=GDK_KEY_Down;
    g_assert_true(key(m->views[0],&event,m));
    g_assert_cmpint(selected(m),==,1);
    gtk_entry_set_text(GTK_ENTRY(m->find_entry),"left");
    gtk_entry_set_text(GTK_ENTRY(m->replace_entry),"replacement");
    replace_result(m->replace_apply,m);
    g_assert_cmpuint(m->history->len,==,2);
    undo(NULL,m);
    assert_text(m,a);
    take(m->take[1],m);
    g_assert_cmpuint(m->history->len,==,2);
    undo(NULL,m);
    assert_text(m,a);
    redo(NULL,m);
    navigate(m,-1);
    take(m->take[1],m);
    assert_text(m,b);
    undo(NULL,m);
    undo(NULL,m);
    assert_text(m,a);
    history_end(m,TRUE);
    assert_text(m,b);
    history_end(m,FALSE);
    assert_text(m,a);
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),1);
    gtk_entry_set_text(GTK_ENTRY(m->find_entry),"RIGHT");
    GtkTextBuffer *right_buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[1]));
    GtkTextIter start,end;
    gtk_text_buffer_get_start_iter(right_buffer,&start);
    gtk_text_buffer_place_cursor(right_buffer,&start);
    find_text(m,FALSE);
    g_assert_true(gtk_text_buffer_get_selection_bounds(right_buffer,&start,&end));
    gchar *match=gtk_text_buffer_get_text(right_buffer,&start,&end,TRUE);
    g_assert_cmpstr(match,==,"right");
    g_free(match);
    g_assert_cmpint(gtk_text_iter_get_line(&start),==,1);
    find_text(m,TRUE);
    g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(m->find_status)),"Wrapped"));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_case),TRUE);
    find_text(m,FALSE);
    g_assert_cmpstr(gtk_label_get_text(GTK_LABEL(m->find_status)),==,"Text not found.");
    gtk_widget_grab_focus(m->views[2]);
    event.keyval=GDK_KEY_f;
    event.state=GDK_CONTROL_MASK;
    g_assert_true(key(m->views[2],&event,m));
    g_assert_cmpint(gtk_combo_box_get_active(GTK_COMBO_BOX(m->find_side)),==,2);
    g_assert_true(gtk_widget_has_focus(m->find_entry));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),FALSE);
    for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT),==,0);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);
    flush();
    for(int i=0;i<3;i++) g_assert_cmpint(gtk_text_view_get_border_window_size(GTK_TEXT_VIEW(m->views[i]),GTK_TEXT_WINDOW_LEFT),>,0);
    copy_difference(m,1);
    gchar *copied=gtk_clipboard_wait_for_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD));
    g_assert_cmpstr(copied,==,"right\n");
    g_free(copied);
    GtkWidget *popup=gtk_menu_new();
    source_menu(GTK_TEXT_VIEW(m->views[0]),popup,m);
    g_assert_nonnull(popup);
    gtk_widget_destroy(popup);
    gtk_entry_set_text(GTK_ENTRY(m->paths[1]),"not-the-loaded-source");
    GtkTextIter source_line;
    gtk_text_buffer_get_iter_at_line(right_buffer,&source_line,2);
    gtk_text_buffer_place_cursor(right_buffer,&source_line);
    edit_source(m,1);
    g_assert_cmpstr(editor_path,==,right);
    g_assert_cmpuint(editor_line,==,3);
    gtk_entry_set_text(GTK_ENTRY(m->paths[1]),right);
    guint display_history=m->position;
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),120);
    flush();
    for(gint i=0;i<3;i++)
    {
        PangoFontDescription *font=NULL;
        gtk_style_context_get(gtk_widget_get_style_context(m->views[i]),GTK_STATE_FLAG_NORMAL,"font",&font,NULL);
        g_assert_cmpint(pango_font_description_get_size(font),==,12*PANGO_SCALE);
        pango_font_description_free(font);
    }
    GdkEventScroll wheel=
    {
        0
    }
    ;
    wheel.state=GDK_CONTROL_MASK;
    wheel.direction=GDK_SCROLL_SMOOTH;
    wheel.delta_y=-1;
    g_assert_true(zoom_scroll(m->views[0],&wheel,m));
    g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,130);
    wheel.state=0;
    g_assert_false(zoom_scroll(m->views[0],&wheel,m));
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),300);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,250);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),100);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->vertical),FALSE);
    for(gint i=0;i<3;i++)
    {
        GtkPolicyType h,v;
        gtk_scrolled_window_get_policy(GTK_SCROLLED_WINDOW(m->scrollers[i]),&h,&v);
        g_assert_cmpint(v,==,GTK_POLICY_NEVER);
    }
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->vertical),TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mono),FALSE);
    g_assert_false(gtk_text_view_get_monospace(GTK_TEXT_VIEW(m->views[0])));
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mono),TRUE);
    g_idle_add(options_response,GINT_TO_POINTER(GTK_RESPONSE_CANCEL));
    view_options(NULL,m);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,100);
    g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)));
    g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_space)));
    g_idle_add(options_response,GINT_TO_POINTER(GTK_RESPONSE_APPLY));
    view_options(NULL,m);
    g_assert_cmpint(gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(m->zoom)),==,130);
    g_assert_false(gtk_widget_get_visible(m->list_scroll));
    g_assert_false(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->numbers)));
    g_assert_cmpuint(m->position,==,display_history);
    g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_space)));
    g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(m->visible_eol)));
    assert_text(m,a);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),100);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->list_toggle),TRUE);
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->overview),TRUE);
    gint64 map_deadline=g_get_monotonic_time()+200000;
    while(g_get_monotonic_time()<map_deadline)
    {
        flush();
        g_usleep(1000);
    }
    g_assert_cmpint(gtk_widget_get_allocated_height(m->maps[0]),>,20);
    for(gint i=0;i<2;i++)
    {
        g_assert_true(gtk_widget_get_visible(m->maps[i]));
        g_assert_cmpuint(m->map_blocks[i]->len,==,m->diff.count);
    }
