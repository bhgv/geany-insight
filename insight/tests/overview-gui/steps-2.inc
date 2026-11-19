/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Ordered phases of the production overview regression fixture.
 * RU: Последовательные этапы проверки настоящего интерфейса проекта. */
    else if(phase==16)
    {
        GString *large=g_string_sized_new(1400000);
        for(guint i=0;i<100000;i++) g_string_append(large,"function_0();\n");
        large_last=(gint)large->len-14;
        gchar *directory=g_path_get_dirname(test_project),*filename=g_build_filename(directory,"large-preview.c",NULL);
        g_free(directory);
        g_assert_true(g_file_set_contents(filename,large->str,large->len,NULL));
        g_string_free(large,TRUE);
        GtkWidget *view=find_named(window,"insight-project-files");
        GtkListStore *model=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
        GtkTreeIter iter;
        gtk_list_store_append(model,&iter);
        gtk_list_store_set(model,&iter,0,"large-preview.c",1,filename,-1);
        GtkTreePath *path=gtk_tree_model_get_path(GTK_TREE_MODEL(model),&iter);
        heartbeat_source=g_timeout_add(5,preview_heartbeat,NULL);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE);
        gtk_tree_path_free(path);
        g_free(filename);
    }
    else if(phase==17)
    {
        ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code"));
        g_assert_cmpint(sci_get_length(preview),==,1400000);
        if(!SSM(preview,SCI_INDICATORVALUEAT,31,large_last)) return G_SOURCE_CONTINUE;
        g_assert_cmpuint(preview_heartbeats,>,1);
        if(!selection_stress)
        {
            selection_stress=TRUE;
            GtkWidget *page=find_named(window,"insight-context"),*book=gtk_widget_get_parent(page);
            gtk_notebook_set_current_page(GTK_NOTEBOOK(book),gtk_notebook_page_num(GTK_NOTEBOOK(book),page));
            SSM(preview,SCI_SETSEL,0,10);
            gtk_widget_queue_draw(GTK_WIDGET(preview));
            return G_SOURCE_CONTINUE;
        }
        if(!SSM(preview,SCI_INDICATORVALUEAT,30,9999*14)) return G_SOURCE_CONTINUE;
        g_assert_cmpint(heartbeat_max_gap,<,500000);
        g_print("Large preview/10000 highlights: max UI heartbeat gap %.1f ms\n",heartbeat_max_gap/1000.0);
        g_source_remove(heartbeat_source);
        heartbeat_source=0;
        GtkWidget *page=gtk_widget_get_parent(GTK_WIDGET(preview));
        while(page && !GTK_IS_NOTEBOOK(gtk_widget_get_parent(page))) page=gtk_widget_get_parent(page);
        g_assert_nonnull(page);
        GtkWidget *notebook=gtk_widget_get_parent(page);
        g_assert_true(GTK_IS_NOTEBOOK(notebook));
        g_assert_cmpstr(gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(notebook),page),==,"Context");
        gchar *directory=g_path_get_dirname(test_project),*filename=g_build_filename(directory,"double-click-large.txt",NULL);
        g_free(directory);
        gchar *large=g_malloc0(2200001);
        memset(large,'x',2200000);
        g_assert_true(g_file_set_contents(filename,large,2200000,NULL));
        g_free(large);
        GtkWidget *view=find_named(window,"insight-project-files");
        GtkListStore *model=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(view)));
        GtkTreeIter iter;
        gtk_list_store_append(model,&iter);
        gtk_list_store_set(model,&iter,0,"double-click-large.txt",1,filename,-1);
        GtkTreePath *path=gtk_tree_model_get_path(GTK_TREE_MODEL(model),&iter);
        gtk_tree_view_row_activated(GTK_TREE_VIEW(view),path,gtk_tree_view_get_column(GTK_TREE_VIEW(view),0));
        gtk_tree_path_free(path);
        g_assert_cmpstr(document_get_current()->file_name,==,filename);
        g_assert_cmpint(sci_get_length(document_get_current()->editor->sci),==,2200000);
        g_free(filename);
    }
    else if(phase==18)
    {
        gchar *filename=g_build_filename(test_directory,"overview-smoke.c",NULL);
        GeanyDocument *target=document_open_file(filename,FALSE,NULL,NULL);
        g_free(filename);
        g_assert_nonnull(target);
        gtk_widget_grab_focus(GTK_WIDGET(target->editor->sci));
        const gchar *source="int worker_first(void) { return 1; }\nint worker_second(void) { return 2; }\nint caller(void) { return worker_first() + worker_second(); }\n";
        sci_set_text(target->editor->sci,source);
        sci_set_current_position(target->editor->sci,(gint)(strstr(source,"return worker_first")-source)+9,FALSE);
        document_set_text_changed(target,FALSE);
    }
    else if(phase==19)
    {
        ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code"));
        if(sci_get_current_line(preview)!=0 || !SSM(preview,SCI_INDICATORVALUEAT,28,4))
        {
            static guint checks;
            if(++checks%5==0)
            {
                gchar *text=sci_get_contents(preview,MIN(sci_get_length(preview)+1,256));
                g_printerr("Pending definition: preview line=%d mark=%ld editor=%d text=%s\n",sci_get_current_line(preview),(long)SSM(preview,SCI_INDICATORVALUEAT,28,4),sci_get_current_position(doc->editor->sci),text);
                g_free(text);
                GtkWidget *choices=find_named(window,"insight-context-definitions");
                gchar *label=gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(choices));
                g_printerr("Candidates=%d label=%s tags=%u follow=%d focused=%d\n",gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(choices)),NULL),label ? label : "none",doc->tm_file ? doc->tm_file->tags_array->len : 0,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(find_named(window,"insight-context-follow"))),gtk_widget_has_focus(GTK_WIDGET(doc->editor->sci)));
                g_free(label);
            }
            return G_SOURCE_CONTINUE;
        }
        g_assert_nonnull(doc->tm_file);
        for(guint n=0;n<doc->tm_file->tags_array->len;n++)
        {
            TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,n);
            g_assert_true(tag->file==doc->tm_file);
        }
        gchar *source=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);
        gint position=(gint)(strstr(source,"+ worker_second")-source)+4;
        sci_set_current_position(doc->editor->sci,position,FALSE);
        g_free(source);
        g_timeout_add(150,move_back_before_definition,NULL);
        g_timeout_add(450,check_cancelled_definition,NULL);
    }
    else if(phase==20)
    {
        ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code"));
        g_assert_true(definition_cancel_checked);
        g_assert_cmpint(sci_get_current_line(preview),==,0);
        gchar *text=sci_get_contents(doc->editor->sci,sci_get_length(doc->editor->sci)+1);
        gchar *name=strstr(text,"+ worker_second");
        g_assert_nonnull(name);
        gint begin=(gint)(name-text)+2;
        SSM(doc->editor->sci,SCI_SETSEL,begin,begin+(gint)strlen("worker_second"));
        g_free(text);
    }
    else if(phase==21)
    {
        ScintillaObject *preview=SCINTILLA(find_named(window,"insight-context-code"));
        if(sci_get_current_line(preview)!=1) return G_SOURCE_CONTINUE;
        GtkWidget *mode=find_named(window,"insight-relation-mode"),*query=find_named(window,"insight-relation-query");
        gtk_combo_box_set_active(GTK_COMBO_BOX(mode),1);
        gtk_entry_set_text(GTK_ENTRY(query),"worker_first");
        g_signal_emit_by_name(query,"activate");
        gtk_entry_set_text(GTK_ENTRY(query),"worker_second");
        g_signal_emit_by_name(query,"activate");
        GtkWidget *symbols=find_named(window,"insight-symbol-query");
        gtk_entry_set_text(GTK_ENTRY(symbols),"worker_first");
        gtk_entry_set_text(GTK_ENTRY(symbols),"worker_second");
    }
    else if(phase==22)
    {
        GtkWidget *symbol_view=find_named(window,"insight-project-symbols");
        GtkTreeModel *symbol_model=gtk_tree_view_get_model(GTK_TREE_VIEW(symbol_view));
        if(gtk_tree_model_iter_n_children(symbol_model,NULL)!=1) return G_SOURCE_CONTINUE;
        GtkTreeIter selected_symbol;
        g_assert_true(gtk_tree_model_get_iter_first(symbol_model,&selected_symbol));
        gchar *symbol_name=NULL;
        gtk_tree_model_get(symbol_model,&selected_symbol,0,&symbol_name,-1);
        g_assert_cmpstr(symbol_name,==,"worker_second");
        g_free(symbol_name);
        GtkWidget *view=find_named(window,"insight-relations");
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        if(gtk_tree_model_iter_n_children(model,NULL)!=1) return G_SOURCE_CONTINUE;
        GtkTreeIter iter;
        g_assert_true(gtk_tree_model_get_iter_first(model,&iter));
        gchar *name=NULL;
        gtk_tree_model_get(model,&iter,7,&name,-1);
        g_assert_cmpstr(name,==,"worker_second");
        g_free(name);
        GtkTreePath *retained_path=gtk_tree_model_get_path(model,&iter);
        retained_relation_row=gtk_tree_row_reference_new(model,retained_path);
        gtk_tree_path_free(retained_path);
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&iter);
        GtkWidget *query=find_named(window,"insight-relation-query");
        g_signal_emit_by_name(query,"activate");
    }
    else if(phase==23)
    {
        GtkWidget *status=find_named(window,"insight-relation-status");
        if(!g_str_has_prefix(gtk_label_get_text(GTK_LABEL(status)),"1 tag-based")) return G_SOURCE_CONTINUE;
        GtkWidget *view=find_named(window,"insight-relations");
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        g_assert_true(gtk_tree_row_reference_valid(retained_relation_row));
        GtkTreeIter iter;
        g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter));
        gchar *name=NULL;
        gtk_tree_model_get(model,&iter,7,&name,-1);
        g_assert_cmpstr(name,==,"worker_second");
        g_free(name);
        gtk_tree_row_reference_free(retained_relation_row);
        retained_relation_row=NULL;
        /* A zero-tag unsaved buffer must hide old disk symbols in the project view. */ sci_set_text(doc->editor->sci,"/* no definitions remain */\n");
        document_set_text_changed(doc,FALSE);
    }
    else if(phase==24)
    {
        GtkWidget *view=find_named(window,"insight-project-symbols");
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        if(gtk_tree_model_iter_n_children(model,NULL)!=0) return G_SOURCE_CONTINUE;
        /* Remove a populated 10000-row model without monopolizing the UI loop. */ GtkWidget *files=find_named(window,"insight-project-files");
        GtkListStore *old=GTK_LIST_STORE(gtk_tree_view_get_model(GTK_TREE_VIEW(files)));
        g_object_ref(old);
        gtk_tree_view_set_model(GTK_TREE_VIEW(files),NULL);
        gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(old),GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID,GTK_SORT_ASCENDING);
        for(guint n=0;n<10000;n++)
        {
            GtkTreeIter iter;
            gchar *path=g_strdup_printf("/synthetic/file-%05u.c",n);
            gtk_list_store_append(old,&iter);
            gtk_list_store_set(old,&iter,0,path,1,path,-1);
            g_free(path);
        }
        gtk_tree_view_set_model(GTK_TREE_VIEW(files),GTK_TREE_MODEL(old));
        g_object_unref(old);
        heartbeat_previous=0;
        heartbeat_max_gap=0;
        preview_heartbeats=0;
        heartbeat_source=g_timeout_add(5,preview_heartbeat,NULL);
        GtkWidget *query=find_named(window,"insight-file-query");
        gtk_entry_set_text(GTK_ENTRY(query),"overview-smoke");
    }
    else if(phase==25)
    {
        GtkWidget *files=find_named(window,"insight-project-files");
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(files));
        if(gtk_tree_model_iter_n_children(model,NULL)!=1) return G_SOURCE_CONTINUE;
        GtkTreeIter iter;
        g_assert_true(gtk_tree_model_get_iter_first(model,&iter));
        gchar *name=NULL;
        gtk_tree_model_get(model,&iter,0,&name,-1);
        g_assert_cmpstr(name,==,"overview-smoke.c");
        g_free(name);
        GtkTreePath *retained_path=gtk_tree_model_get_path(model,&iter);
        retained_file_row=gtk_tree_row_reference_new(model,retained_path);
        gtk_tree_path_free(retained_path);
        gtk_tree_selection_select_iter(gtk_tree_view_get_selection(GTK_TREE_VIEW(files)),&iter);
        GtkWidget *query=find_named(window,"insight-file-query");
        gtk_entry_set_text(GTK_ENTRY(query),"missing-file");
        gtk_entry_set_text(GTK_ENTRY(query),"overview-smoke");
    }
    else if(phase==26)
    {
        GtkWidget *files=find_named(window,"insight-project-files");
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(files));
        if(gtk_tree_model_iter_n_children(model,NULL)!=1) return G_SOURCE_CONTINUE;
        g_assert_true(gtk_widget_get_sensitive(files));
        g_assert_true(gtk_tree_row_reference_valid(retained_file_row));
        GtkTreeIter retained;
        g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(files)),NULL,&retained));
        gtk_tree_row_reference_free(retained_file_row);
        retained_file_row=NULL;
        g_assert_cmpuint(preview_heartbeats,>,1);
        g_assert_cmpint(heartbeat_max_gap,<,500000);
        g_print("10000 file rows/incremental reconciliation: max UI heartbeat gap %.1f ms\n",heartbeat_max_gap/1000.0);
        g_source_remove(heartbeat_source);
        heartbeat_source=0;
        /* Pending tag timers/tasks must not follow a recycled document slot. */ gchar *directory=g_path_get_dirname(test_project),*path=g_build_filename(directory,"tag-close-reopen.c",NULL);
        g_free(directory);
        g_assert_true(g_file_set_contents(path,"int retained_original(void) { return 1; }\n",-1,NULL));
        GeanyDocument *temporary=document_open_file(path,FALSE,NULL,NULL);
        g_assert_nonnull(temporary);
        g_assert_nonnull(temporary->tm_file);
        g_assert_cmpuint(temporary->tm_file->tags_array->len,==,0);
        sci_set_text(temporary->editor->sci,"int stale_edit(void) { return 2; }\n");
        document_set_text_changed(temporary,FALSE);
        guint retired_id=temporary->id;
        g_assert_true(document_close(temporary));
        temporary=document_open_file(path,FALSE,NULL,NULL);
        g_assert_nonnull(temporary);
        g_assert_cmpuint(temporary->id,!=,retired_id);
        g_free(path);
    }
    else if(phase==27)
    {
        g_assert_nonnull(doc->tm_file);
        gboolean original=FALSE;
        for(guint n=0;n<doc->tm_file->tags_array->len;n++)
        {
            TMTag *tag=g_ptr_array_index(doc->tm_file->tags_array,n);
            g_assert_cmpstr(tag->name,!=,"stale_edit");
            if(!strcmp(tag->name,"retained_original")) original=TRUE;
        }
        g_assert_true(original);
        sci_set_text(doc->editor->sci,"parser Parser parserDefinition subparsersDefault parser _parser parser1");
        SSM(doc->editor->sci,SCI_SETSEL,0,6);
        document_set_text_changed(doc,FALSE);
    }
    else if(phase==28)
    {
        if(!SSM(doc->editor->sci,SCI_INDICATORVALUEAT,30,49)) return G_SOURCE_CONTINUE;
        g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,30,0),>,0);
        const gint rejected[]=
        {
            7,14,34,57,64
        }
        ;
        for(guint n=0;n<G_N_ELEMENTS(rejected);n++) g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,30,rejected[n]),==,0);
        gchar *extra=g_build_filename(test_directory,"not-indexed.unknown",NULL);
        const gchar bytes[]=
        {
            0,1,2,3
        }
        ;
        g_assert_true(g_file_set_contents(extra,bytes,sizeof bytes,NULL));
        gchar *symbol_filter=g_strdup_printf("file:\"%s\"",extra);
        gtk_entry_set_text(GTK_ENTRY(find_named(window,"insight-symbol-query")),symbol_filter);
        g_free(symbol_filter);
        g_free(extra);
        GtkWidget *file_query=find_named(window,"insight-file-query");
        gtk_entry_set_text(GTK_ENTRY(file_query),"not-indexed");
        GtkWidget *reindex=find_button(window,"Reindex");
        g_assert_nonnull(reindex);
        gtk_button_clicked(GTK_BUTTON(reindex));
    }
    else if(phase==29 || phase==30)
    {
        GtkWidget *view=find_named(window,"insight-project-files");
        GtkWidget *symbols=find_named(window,"insight-project-symbols");
        if(g_object_get_data(G_OBJECT(window),"insight-db-scanning") || !gtk_widget_get_sensitive(view) || !gtk_widget_get_sensitive(symbols)) return G_SOURCE_CONTINUE;
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,1);
        GtkTreeIter row;
        g_assert_true(gtk_tree_model_get_iter_first(model,&row));
        gchar *name=NULL,*path=NULL,*expected=g_build_filename(test_directory,"not-indexed.unknown",NULL);
        gtk_tree_model_get(model,&row,0,&name,1,&path,-1);
        g_assert_cmpstr(name,==,"not-indexed.unknown");
        g_assert_cmpstr(path,==,expected);
        g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(symbols)),NULL),==,0);
        if(phase==30) g_assert_cmpint(g_remove(expected),==,0);
        g_free(name);
        g_free(path);
        g_free(expected);
        gtk_button_clicked(GTK_BUTTON(find_button(window,"Reindex")));
    }
    else if(phase==31)
    {
        GtkWidget *view=find_named(window,"insight-project-files");
        if(g_object_get_data(G_OBJECT(window),"insight-db-scanning") || !gtk_widget_get_sensitive(view)) return G_SOURCE_CONTINUE;
        g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,0);
        /* Quit while both a scan and a search are queued. */ GtkWidget *reindex=find_button(window,"Reindex");
        if(reindex) gtk_button_clicked(GTK_BUTTON(reindex));
        GtkWidget *query=find_named(window,"insight-project-query");
        gtk_entry_set_text(GTK_ENTRY(query),"worker_second");
        g_signal_emit_by_name(query,"activate");
        GtkWidget *quit=ui_lookup_widget(window,"menu_quit1");
        g_assert_true(GTK_IS_MENU_ITEM(quit));
        gtk_menu_item_activate(GTK_MENU_ITEM(quit));
        g_print("Overview GUI smoke passed: editor icons, syntax, independent read-only, edits, scrolling, toggling, keyword variations, saved query history, preview-only result navigation, exact hit opening, result columns, stale result rejection, Context links, project Apply/Cancel, menu Open Project, invalid/malformed project preserves session, UTF-8 paths, project switching, responsive 1.4 MiB preview with 10000 batched highlights, asynchronous symbol filtering, Context notebook tab, double-click opens >2 MiB file, delayed and cancelled word/selection definition lookup, latest-request symbol relations, retained relation row and selection after completed identical query, async document tag publication, 10000 file rows/batched clearing, latest file query, tag timer close/reopen, zero-tag overlay, shutdown with queued work, tab close\n");
        return G_SOURCE_REMOVE;
    }
    phase++;
    return G_SOURCE_CONTINUE;
