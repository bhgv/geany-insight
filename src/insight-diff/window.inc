/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Build the production comparison and merge window.
 * RU: Создаёт настоящее окно сравнения и сведения. */

/* EN: Create a comparison owned by its GTK window with optional parent; return session state until window destruction. Initial construction preserves legacy merge/review behavior.
 * RU: Создаёт сравнение во владении GTK-окна с необязательным parent; возвращает состояние до уничтожения окна. Начальная сборка сохраняет режим сведения/review. */
static Merge *merge_new(GtkWidget *parent)
{
    Merge *m=g_new0(Merge,1);
    for(guint i=0;i<4;i++) m->search_fields[i]=TRUE;
    m->history=g_ptr_array_new_with_free_func(snapshot_free);
    m->saved=g_strdup("");
    m->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_widget_set_name(m->window,"insight-diff-window");
    gtk_window_set_transient_for(GTK_WINDOW(m->window),GTK_WINDOW(parent));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(m->window),TRUE);
    gtk_window_set_default_size(GTK_WINDOW(m->window),INSIGHT_COMPARE_WIDTH,INSIGHT_COMPARE_HEIGHT);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);
    gtk_container_set_border_width(GTK_CONTAINER(box),8);
    gtk_container_add(GTK_CONTAINER(m->window),box);
    for(int i=0;i<2;i++)
    {
        GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
        gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);
        gtk_box_pack_start(GTK_BOX(row),gtk_label_new(i ? "Right" : "Left"),FALSE,FALSE,0);
        m->paths[i]=gtk_entry_new();
        gtk_widget_set_name(m->paths[i],i ? "insight-diff-path-right":"insight-diff-path-left");
        gtk_box_pack_start(GTK_BOX(row),m->paths[i],TRUE,TRUE,0);
        GtkWidget *browse=button(row,"Browse…",G_CALLBACK(choose),m);
        g_object_set_data(G_OBJECT(browse),"side",GINT_TO_POINTER(i));
    }
    GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
    m->compare_button=button(bar,"Compare",G_CALLBACK(compare),m);
    gtk_widget_set_name(m->compare_button,"insight-diff-compare");
    m->cancel_button=button(bar,"Cancel comparison",G_CALLBACK(cancel_compare),m);
    gtk_widget_set_name(m->cancel_button,"insight-diff-cancel");
    gtk_widget_set_sensitive(m->cancel_button,FALSE);
    m->take[0]=button(bar,"Take Left",G_CALLBACK(take),m);
    m->take[1]=button(bar,"Take Right",G_CALLBACK(take),m);
    m->take_all[0]=button(bar,"All Left",G_CALLBACK(take_all),m);
    m->take_all[1]=button(bar,"All Right",G_CALLBACK(take_all),m);
    gtk_widget_set_name(m->take_all[1],"insight-diff-all-right");
    gtk_widget_set_tooltip_text(m->take_all[0],"Take all difference blocks from Left — one Undo");
    gtk_widget_set_tooltip_text(m->take_all[1],"Take all difference blocks from Right — one Undo");
    m->save_button=button(bar,"Save result…",G_CALLBACK(save),m);
    m->external=button(bar,"External KDiff3 (optional)",G_CALLBACK(external),m);
    GtkWidget *options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),options,FALSE,FALSE,0);
    m->previous=button(options,"Previous difference",G_CALLBACK(step),m);
    m->next=button(options,"Next difference",G_CALLBACK(step),m);
    m->list_toggle=gtk_check_button_new_with_label("Difference list");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->list_toggle),TRUE);
    gtk_box_pack_start(GTK_BOX(options),m->list_toggle,FALSE,FALSE,0);
    m->wrap_toggle=gtk_check_button_new_with_label("Wrap result lines");
    gtk_box_pack_start(GTK_BOX(options),m->wrap_toggle,FALSE,FALSE,0);
    m->sync=gtk_check_button_new_with_label("Scroll sources together");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->sync),TRUE);
    gtk_box_pack_start(GTK_BOX(options),m->sync,FALSE,FALSE,0);
    GtkWidget *comparison=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),comparison,FALSE,FALSE,0);
    m->ignore_space=gtk_check_button_new_with_label("Ignore spaces and tabs");
    gtk_box_pack_start(GTK_BOX(comparison),m->ignore_space,FALSE,FALSE,0);
    m->ignore_eol=gtk_check_button_new_with_label("Ignore line ending style");
    gtk_box_pack_start(GTK_BOX(comparison),m->ignore_eol,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(comparison),gtk_label_new("Apply with Compare; ignored text stays from Left."),FALSE,FALSE,0);
    button(comparison,"Compare directories…",G_CALLBACK(show_directory),m);
    m->numbers=gtk_check_button_new_with_label("Line numbers");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->numbers),TRUE);
    gtk_box_pack_start(GTK_BOX(options),m->numbers,FALSE,FALSE,0);
    GtkWidget *display=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),display,FALSE,FALSE,0);
    m->mono=gtk_check_button_new_with_label("Mono Font View");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->mono),TRUE);
    gtk_box_pack_start(GTK_BOX(display),m->mono,FALSE,FALSE,0);
    m->vertical=gtk_check_button_new_with_label("Vertical scrollbars");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->vertical),TRUE);
    gtk_box_pack_start(GTK_BOX(display),m->vertical,FALSE,FALSE,0);
    m->overview=gtk_check_button_new_with_label("Overview");
    gtk_box_pack_start(GTK_BOX(display),m->overview,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(display),gtk_label_new("Scale (%)"),FALSE,FALSE,0);
    m->zoom=gtk_spin_button_new_with_range(50,250,10);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->zoom),100);
    gtk_widget_set_name(m->zoom,"insight-diff-zoom");
    gtk_box_pack_start(GTK_BOX(display),m->zoom,FALSE,FALSE,0);
    GtkWidget *settings=button(display,"Options…",G_CALLBACK(view_options),m);
    gtk_widget_set_name(settings,"insight-diff-options-button");
    m->font_css=gtk_css_provider_new();
    GtkWidget *history_button=button(display,"Result History…",G_CALLBACK(history_dialog),m);
    gtk_widget_set_name(history_button,"insight-diff-history-button");
    GtkWidget *whitespace=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),whitespace,FALSE,FALSE,0);
    m->visible_space=gtk_check_button_new_with_label("Visible tabs and spaces");
    gtk_box_pack_start(GTK_BOX(whitespace),m->visible_space,FALSE,FALSE,0);
    m->visible_eol=gtk_check_button_new_with_label("Visible line endings");
    gtk_box_pack_start(GTK_BOX(whitespace),m->visible_eol,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(whitespace),gtk_label_new("· space   → tab   ¶ line ending"),FALSE,FALSE,4);
    GtkWidget *find=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),find,FALSE,FALSE,0);
    m->find_side=gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Left");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Right");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->find_side),"Result");
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->find_side),0);
    gtk_box_pack_start(GTK_BOX(find),m->find_side,FALSE,FALSE,0);
    m->find_entry=gtk_search_entry_new();
    gtk_widget_set_name(m->find_entry,"insight-diff-search");
    gtk_entry_set_placeholder_text(GTK_ENTRY(m->find_entry),"Find text — Ctrl+F, F3 / Shift+F3");
    gtk_box_pack_start(GTK_BOX(find),m->find_entry,TRUE,TRUE,0);
    m->find_case=gtk_check_button_new_with_label("Match case");
    gtk_box_pack_start(GTK_BOX(find),m->find_case,FALSE,FALSE,0);
    m->find_word=gtk_check_button_new_with_label("Whole words only");
    gtk_box_pack_start(GTK_BOX(find),m->find_word,FALSE,FALSE,0);
    m->find_wrap=gtk_check_button_new_with_label("Wrap around");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(m->find_wrap),TRUE);
    gtk_box_pack_start(GTK_BOX(find),m->find_wrap,FALSE,FALSE,0);
    m->find_previous=button(find,"Find previous",G_CALLBACK(find_clicked),m);
    m->find_next=button(find,"Find next",G_CALLBACK(find_clicked),m);
    button(find,"Find all…",G_CALLBACK(find_all_clicked),m);
    m->find_status=gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(find),m->find_status,FALSE,FALSE,0);
    g_signal_connect(m->find_entry,"activate",G_CALLBACK(find_activate),m);
    g_signal_connect(m->find_entry,"key-press-event",G_CALLBACK(key),m);
    GtkWidget *regex_options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),regex_options,FALSE,FALSE,0);
    m->find_regex=gtk_check_button_new_with_label("Regular expressions (Perl compatible)");
    gtk_box_pack_start(GTK_BOX(regex_options),m->find_regex,FALSE,FALSE,0);
    m->find_dotall=gtk_check_button_new_with_label("Dot matches line endings");
    gtk_box_pack_start(GTK_BOX(regex_options),m->find_dotall,FALSE,FALSE,0);
    GtkWidget *regex_help=gtk_label_new("Replacement: \\1, \\g<name>, \\n, \\t, \\\\");
    gtk_box_pack_start(GTK_BOX(regex_options),regex_help,FALSE,FALSE,4);
    GtkWidget *resetters[]=
    {
        m->find_entry,m->find_side,m->find_case,m->find_word,m->find_regex,m->find_dotall
    }
    ;
    for(guint i=0;i<G_N_ELEMENTS(resetters);i++) g_signal_connect(resetters[i],i<2 ? "changed" : "toggled",G_CALLBACK(regex_reset),m);
    GtkWidget *replace=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),replace,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(replace),gtk_label_new("Replace in Result — Ctrl+H"),FALSE,FALSE,0);
    m->replace_entry=gtk_entry_new();
    gtk_widget_set_name(m->replace_entry,"insight-diff-replacement");
    gtk_entry_set_placeholder_text(GTK_ENTRY(m->replace_entry),"Replacement text (empty deletes matches)");
    gtk_box_pack_start(GTK_BOX(replace),m->replace_entry,TRUE,TRUE,0);
    GtkWidget *replace_options=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),replace_options,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(replace_options),gtk_label_new("Replace scope"),FALSE,FALSE,0);
    m->replace_scope=gtk_combo_box_text_new();
    gtk_widget_set_name(m->replace_scope,"insight-diff-replace-scope");
    const gchar *scopes[]=
    {
        "Whole Result","Result selection only","Current position to end"
    }
    ;
    for(guint i=0;i<G_N_ELEMENTS(scopes);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->replace_scope),scopes[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->replace_scope),0);
    gtk_box_pack_start(GTK_BOX(replace_options),m->replace_scope,FALSE,FALSE,0);
    gtk_widget_set_tooltip_text(m->replace_scope,"Current position starts at the selection beginning or caret; replacement never wraps.");
    m->replace_once=gtk_check_button_new_with_label("One occurrence per line");
    gtk_box_pack_start(GTK_BOX(replace_options),m->replace_once,FALSE,FALSE,0);
    button(replace,"Preview count",G_CALLBACK(replace_result),m);
    m->replace_apply=button(replace,"Replace all in Result",G_CALLBACK(replace_result),m);
    gtk_widget_set_name(m->replace_apply,"insight-diff-replace-all");
    m->replace_case=gtk_check_button_new_with_label("Preserve old case");
    gtk_box_pack_start(GTK_BOX(replace_options),m->replace_case,FALSE,FALSE,0);
    gtk_widget_set_name(m->replace_case,"insight-diff-preserve-case");
    m->replace_confirm=gtk_check_button_new_with_label("Confirm replacements");
    gtk_widget_set_name(m->replace_confirm,"insight-diff-confirm-replacements");
    gtk_box_pack_start(GTK_BOX(replace_options),m->replace_confirm,FALSE,FALSE,0);
    m->replace_status=gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(replace_options),m->replace_status,FALSE,FALSE,0);
    gtk_widget_set_tooltip_text(replace,"Uses search text, regex, case and whole-word options above; follows Replace scope without wrapping.");
    GtkWidget *line=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(box),line,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(line),gtk_label_new("Go to Line — Ctrl+G"),FALSE,FALSE,0);
    m->line_side=gtk_combo_box_text_new();
    for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->line_side),i==0 ? "Left" : i==1 ? "Right" : "Result");
    gtk_combo_box_set_active(GTK_COMBO_BOX(m->line_side),0);
    gtk_box_pack_start(GTK_BOX(line),m->line_side,FALSE,FALSE,0);
    m->line_number=gtk_spin_button_new_with_range(1,G_MAXINT,1);
    gtk_entry_set_width_chars(GTK_ENTRY(m->line_number),10);
    gtk_widget_set_name(m->line_number,"insight-diff-line");
    gtk_box_pack_start(GTK_BOX(line),m->line_number,FALSE,FALSE,0);
    m->line_select=gtk_check_button_new_with_label("Select line text");
    gtk_box_pack_start(GTK_BOX(line),m->line_select,FALSE,FALSE,0);
    button(line,"Go",G_CALLBACK(go_line),m);
    GtkWidget *first=button(line,"First line",G_CALLBACK(line_begin),m);
    g_object_set_data(G_OBJECT(m->window),"line-first",first);
    button(line,"Last line",G_CALLBACK(line_begin),m);
    GtkWidget *edit_menu=gtk_menu_new(),*edit_button=gtk_menu_button_new();
    gtk_button_set_label(GTK_BUTTON(edit_button),"Result menu");
    gtk_widget_set_name(edit_button,"insight-diff-result-menu");
    gtk_menu_button_set_popup(GTK_MENU_BUTTON(edit_button),edit_menu);
    gtk_box_pack_start(GTK_BOX(line),edit_button,FALSE,FALSE,0);
    g_signal_connect_object(edit_menu,"show",G_CALLBACK(result_menu_show),m->window,0);
    gtk_box_pack_start(GTK_BOX(line),gtk_label_new("Edit tab width"),FALSE,FALSE,0);
    m->edit_tab_width=gtk_spin_button_new_with_range(1,16,1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(m->edit_tab_width),4);
    gtk_entry_set_width_chars(GTK_ENTRY(m->edit_tab_width),2);
    gtk_box_pack_start(GTK_BOX(line),m->edit_tab_width,FALSE,FALSE,0);
    m->line_status=gtk_label_new("");
    gtk_box_pack_start(GTK_BOX(line),m->line_status,FALSE,FALSE,0);
    gtk_widget_set_tooltip_text(m->line_number,"Logical line number; values beyond the end go to the last line.");
    g_signal_connect(m->line_number,"activate",G_CALLBACK(go_line),m);
    g_signal_connect(m->line_number,"key-press-event",G_CALLBACK(key),m);
    GtkWidget *panes=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    m->outer_pane=panes;
    gtk_widget_set_name(panes,"insight-diff-browser-split");
    gtk_box_pack_start(GTK_BOX(box),panes,TRUE,TRUE,0);
    m->list=gtk_list_store_new(6,G_TYPE_INT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
    m->hunk_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(m->list));
    GtkCellRenderer *context_renderer=gtk_cell_renderer_text_new();
    g_object_set(context_renderer,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,NULL);
    m->symbol_column=gtk_tree_view_column_new_with_attributes(insight_tr("Symbol / scope"),context_renderer,"text",5,NULL);
    gtk_tree_view_column_set_resizable(m->symbol_column,TRUE);
    gtk_tree_view_column_set_expand(m->symbol_column,TRUE);
    gtk_tree_view_append_column(GTK_TREE_VIEW(m->hunk_view),m->symbol_column);
    gtk_tree_view_column_set_visible(m->symbol_column,FALSE);
    for(guint i=0;i<3;i++)
    {
        GtkCellRenderer *renderer=gtk_cell_renderer_text_new();
        g_object_set(renderer,"ellipsize",PANGO_ELLIPSIZE_MIDDLE,NULL);
        if(i==2) insight_i18n_cell(renderer);
        GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(insight_tr(i==0 ? "Left":i==1 ? "Right":"Merge status"),renderer,"text",i+2,NULL);
        gtk_tree_view_column_set_resizable(c,TRUE);
        gtk_tree_view_column_set_expand(c,i<2);
        gtk_tree_view_append_column(GTK_TREE_VIEW(m->hunk_view),c);
        if(i<2) m->file_columns[i]=c;
        else
        {
            m->merge_column=c;
            GtkWidget *label=gtk_label_new(insight_tr("Merge status"));
            gtk_widget_set_margin_end(label,24);
            gtk_tree_view_column_set_widget(c,label);
            gtk_widget_show(label);
        }
    }
    gtk_widget_set_name(m->hunk_view,"insight-diff-differences");
    m->selection=g_object_ref(gtk_tree_view_get_selection(GTK_TREE_VIEW(m->hunk_view)));
    g_signal_connect(m->selection,"changed",G_CALLBACK(selection),m);
    g_signal_connect_swapped(m->selection,"changed",G_CALLBACK(map_redraw),m);
    m->list_scroll=help_overlay(scroll(m->hunk_view),"internal-diff.md");
    gtk_widget_set_size_request(m->list_scroll,-1,110);
    gtk_paned_pack1(GTK_PANED(panes),m->list_scroll,FALSE,FALSE);
    GtkWidget *merge_split=gtk_paned_new(GTK_ORIENTATION_VERTICAL),*columns=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    gtk_widget_set_name(merge_split,"insight-diff-merge-split");
    gtk_widget_set_name(columns,"insight-diff-source-split");
    gtk_paned_pack2(GTK_PANED(panes),merge_split,TRUE,FALSE);
    gtk_paned_pack1(GTK_PANED(merge_split),columns,TRUE,FALSE);
    gtk_paned_set_position(GTK_PANED(merge_split),260);
    gtk_paned_set_position(GTK_PANED(columns),540);
    for(int i=0;i<3;i++)
    {
        GtkWidget *column=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);
        if(i==0) gtk_paned_pack1(GTK_PANED(columns),column,TRUE,FALSE);
        else if(i==1) gtk_paned_pack2(GTK_PANED(columns),column,TRUE,FALSE);
        else
        {
            m->result_panel=column;
            gtk_widget_set_name(column,"insight-diff-result-panel");
            gtk_paned_pack2(GTK_PANED(merge_split),column,TRUE,FALSE);
        }
        m->headers[i]=gtk_label_new(NULL);
        gtk_label_set_selectable(GTK_LABEL(m->headers[i]),TRUE);
        gtk_label_set_xalign(GTK_LABEL(m->headers[i]),0);
        gtk_label_set_ellipsize(GTK_LABEL(m->headers[i]),PANGO_ELLIPSIZE_MIDDLE);
        gtk_widget_set_margin_end(m->headers[i],24);
        gtk_box_pack_start(GTK_BOX(column),help_overlay(m->headers[i],i==2 ? "panel-merge-result.md":"panel-diff-input.md"),FALSE,FALSE,0);
        if(i==2)
        {
            m->history_ui=insight_history_ui(column,m,diff_history_count,diff_history_position,diff_history_jump,diff_history_tip);
            m->history_ui->label=diff_history_label;
            m->undo=m->history_ui->undo;
            m->redo=m->history_ui->redo;
            GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
            m->destination_kind=gtk_combo_box_text_new();
            const gchar *choices[]=
            {
                "Merge into Left file","Merge into Right file","Separate output file"
            }
            ;
            for(guint n=0;n<3;n++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(m->destination_kind),insight_tr(choices[n]));
            gtk_combo_box_set_active(GTK_COMBO_BOX(m->destination_kind),2);
            gtk_box_pack_start(GTK_BOX(row),m->destination_kind,FALSE,FALSE,0);
            m->destination=gtk_entry_new();
            gtk_widget_set_name(m->destination,"insight-diff-output-path");
            gtk_widget_set_tooltip_text(m->destination,insight_tr("Output filename; choosing a destination does not write until Save."));
            gtk_box_pack_start(GTK_BOX(row),m->destination,TRUE,TRUE,0);
            button(row,"Browse…",G_CALLBACK(destination_browse),m);
            gtk_box_pack_start(GTK_BOX(column),row,FALSE,FALSE,0);
            g_signal_connect(m->destination_kind,"changed",G_CALLBACK(destination_choice),m);
            g_signal_connect(m->destination,"changed",G_CALLBACK(destination_changed),m);
        }
        m->views[i]=gtk_text_view_new();
        gtk_widget_set_name(m->views[i],i==0 ? "insight-diff-left" : i==1 ? "insight-diff-right" : "insight-diff-result");
        gtk_text_view_set_monospace(GTK_TEXT_VIEW(m->views[i]),TRUE);
        gtk_text_view_set_editable(GTK_TEXT_VIEW(m->views[i]),i==2);
        GtkWidget *body=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,0);
        gtk_box_pack_start(GTK_BOX(column),body,TRUE,TRUE,0);
        m->scrollers[i]=scroll(m->views[i]);
        gtk_box_pack_start(GTK_BOX(body),m->scrollers[i],TRUE,TRUE,0);
        if(i<2)
        {
            m->maps[i]=gtk_drawing_area_new();
            gtk_widget_set_size_request(m->maps[i],18,-1);
            gtk_widget_set_no_show_all(m->maps[i],TRUE);
            gtk_box_pack_start(GTK_BOX(body),m->maps[i],FALSE,FALSE,0);
            g_object_ref(m->maps[i]);
            gtk_widget_add_events(m->maps[i],GDK_BUTTON_PRESS_MASK);
            gtk_widget_set_tooltip_text(m->maps[i],"Overview: click a marker to select its difference; click elsewhere to scroll.");
            g_signal_connect(m->maps[i],"draw",G_CALLBACK(map_draw),m);
            g_signal_connect(m->maps[i],"button-press-event",G_CALLBACK(map_click),m);
        }
        gtk_style_context_add_provider(gtk_widget_get_style_context(m->views[i]),GTK_STYLE_PROVIDER(m->font_css),GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        gtk_widget_add_events(m->views[i],GDK_SCROLL_MASK|GDK_SMOOTH_SCROLL_MASK);
        g_signal_connect(m->views[i],"scroll-event",G_CALLBACK(zoom_scroll),m);
        g_signal_connect(m->views[i],"key-press-event",G_CALLBACK(key),m);
        g_signal_connect(m->views[i],"populate-popup",G_CALLBACK(source_menu),m);
        g_signal_connect_after(m->views[i],"draw",G_CALLBACK(draw_numbers),m);
        g_signal_connect_after(m->views[i],"draw",G_CALLBACK(draw_whitespace),m);
        g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),"changed",G_CALLBACK(gutter_size),m);
        if(i<2) gtk_text_buffer_create_tag(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])),"difference","background",i ? "#d5f5d5" : "#ffe0d0","foreground","#202020",NULL);
    }
    for(int i=0;i<2;i++)
    {
        g_signal_connect(m->views[i],"button-press-event",G_CALLBACK(pick_block),m);
        m->alignment[i]=g_ptr_array_new_with_free_func(g_object_unref);
        m->horizontal[i]=g_object_ref(gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(m->views[i])));
        g_signal_connect(m->horizontal[i],"value-changed",G_CALLBACK(horizontal_scroll),m);
        g_signal_connect(m->views[i],"size-allocate",G_CALLBACK(source_allocated),m);
        m->adjustments[i]=g_object_ref(gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(m->views[i])));
        g_signal_connect(m->adjustments[i],"value-changed",G_CALLBACK(sync_scroll),m);
    }
    g_signal_connect(m->list_toggle,"toggled",G_CALLBACK(show_list),m);
    g_signal_connect(m->wrap_toggle,"toggled",G_CALLBACK(wrap),m);
    m->output=g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[2])));
    m->status=gtk_label_new("Choose two UTF-8 files and press Compare.");
    gtk_label_set_xalign(GTK_LABEL(m->status),0);
    gtk_label_set_line_wrap(GTK_LABEL(m->status),TRUE);
    gtk_box_pack_start(GTK_BOX(box),m->status,FALSE,FALSE,0);
    g_signal_connect(result(m),"begin-user-action",G_CALLBACK(action_begin),m);
    g_signal_connect(result(m),"end-user-action",G_CALLBACK(action_end),m);
    g_signal_connect(result(m),"insert-text",G_CALLBACK(manual_insert),m);
    g_signal_connect(result(m),"delete-range",G_CALLBACK(manual_delete),m);
    g_signal_connect(result(m),"changed",G_CALLBACK(changed),m);
    g_signal_connect(m->window,"key-press-event",G_CALLBACK(key),m);
    g_signal_connect(m->window,"delete-event",G_CALLBACK(closing),m);
    g_signal_connect(m->window,"destroy",G_CALLBACK(destroyed),m);
    for(gint i=0;i<2;i++)
    {
        g_signal_connect(m->adjustments[i],"value-changed",G_CALLBACK(map_adjustment),m);
        g_signal_connect(m->adjustments[i],"changed",G_CALLBACK(map_adjustment),m);
    }
    g_signal_connect(m->numbers,"toggled",G_CALLBACK(number_toggle),m);
    number_toggle(NULL,m);
    g_object_set_data(G_OBJECT(m->window),"insight-diff-state",m);
    insight_i18n_on_refresh(G_OBJECT(m->window),diff_locale_changed,m);
    g_signal_connect(m->visible_space,"toggled",G_CALLBACK(display_changed),m);
    g_signal_connect(m->visible_eol,"toggled",G_CALLBACK(display_changed),m);
    g_signal_connect(m->zoom,"value-changed",G_CALLBACK(display_changed),m);
    g_signal_connect(m->mono,"toggled",G_CALLBACK(display_changed),m);
    g_signal_connect(m->vertical,"toggled",G_CALLBACK(display_changed),m);
    g_signal_connect(m->overview,"toggled",G_CALLBACK(display_changed),m);
    GtkWidget *controls[G_N_ELEMENTS(view_checks)];
    view_controls(m,controls);
    for(guint i=0;i<G_N_ELEMENTS(view_checks);i++)
    {
        g_object_ref(controls[i]);
        gchar *name=g_strconcat("insight-diff-",view_checks[i],NULL);
        gtk_widget_set_name(controls[i],name);
        g_free(name);
    }
    g_object_ref(m->zoom);
    GtkWidget *tool_rows[]=
    {
        gtk_widget_get_parent(m->paths[0]),gtk_widget_get_parent(m->paths[1]),bar,options,comparison,display,whitespace,find,regex_options,replace,replace_options,line
    }
    ;
    toolbar_build(m,box,tool_rows);
    GtkWidget *binary_page=gtk_widget_get_parent(gtk_widget_get_parent(m->paths[0]));
    GtkWidget *binary=gtk_button_new_with_label("Binary comparison / Hex editor…");
    gtk_box_pack_start(GTK_BOX(binary_page),binary,FALSE,FALSE,0);
    g_signal_connect(binary,"clicked",G_CALLBACK(binary_mode),m);
    GtkWidget *selection_row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);
    GtkWidget *merge_page=gtk_widget_get_parent(bar);
    gtk_box_pack_start(GTK_BOX(merge_page),selection_row,FALSE,FALSE,0);
    for(guint side=0;side<2;side++)
    {
        GtkWidget *b=gtk_button_new_with_label(side ? "Insert Right selection":"Insert Left selection");
        gtk_widget_set_tooltip_text(b,"Insert the last source selection at the Result caret or replace its selection. Source text stays captured while choosing the Result range. Undo restores Result.");
        g_object_set_data(G_OBJECT(b),"side",GUINT_TO_POINTER(side));
        gtk_box_pack_start(GTK_BOX(selection_row),b,FALSE,FALSE,0);
        g_signal_connect(b,"clicked",G_CALLBACK(merge_source_selection),m);
    }
    for(guint i=0;i<3;i++)
    {
        m->syntax_buffers[i]=g_object_ref(gtk_text_view_get_buffer(GTK_TEXT_VIEW(m->views[i])));
        g_signal_connect(m->syntax_buffers[i],"changed",G_CALLBACK(syntax_changed),m);
        if(i<2)
        {
            g_signal_connect(m->syntax_buffers[i],"mark-set",G_CALLBACK(remember_source_selection),m);
            g_signal_connect(m->syntax_buffers[i],"changed",G_CALLBACK(forget_source_selection),m);
        }
    }
    diff_mode_create(m,box);
    file_headers(m);
    record(m);
    selection(NULL,m);
    gtk_widget_show_all(m->window);
    view_restore(m,view_defaults);
    display_changed(NULL,m);
    open_compares=g_list_prepend(open_compares,m);
    return m;
}

