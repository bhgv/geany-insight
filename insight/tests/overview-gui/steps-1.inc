/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Ordered phases of the production overview regression fixture.
 * RU: Последовательные этапы проверки настоящего интерфейса проекта. */
    (void)data;
    static guint last=G_MAXUINT;
    if(last!=phase)
    {
        g_printerr("Overview smoke phase %u\n",phase);
        last=phase;
    }
    GeanyDocument *doc=document_get_current();
    g_assert_nonnull(doc);
    if(phase==0)
    {
        const char *icons[]=
        {
            "classviewer-class","classviewer-macro","classviewer-member","classviewer-method","classviewer-namespace","classviewer-other","classviewer-struct","classviewer-var","geany-save-all","geany-build","geany-close-all"
        }
        ;
        for(guint i=0;i<G_N_ELEMENTS(icons);i++)
        {
            GError *error=NULL;
            GdkPixbuf *icon=gtk_icon_theme_load_icon(gtk_icon_theme_get_default(),icons[i],16,0,&error);
            g_assert_no_error(error);
            g_assert_nonnull(icon);
            g_object_unref(icon);
        }
        GString *source=g_string_new(NULL);
        for(guint i=0;i<500;i++) g_string_append_printf(source,"int function_%u(void) { return %u; }\n",i,i);
        gchar *path=g_build_filename(test_directory,"overview-smoke.c",NULL);
        g_assert_true(g_file_set_contents(path,source->str,-1,NULL));
        doc=document_open_file(path,FALSE,filetypes_lookup_by_name("C"),NULL);
        g_free(path);
        g_string_free(source,TRUE);
        original_id=doc->id;
        window=gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci));
        gtk_window_resize(GTK_WINDOW(window),1100,800);
        GtkWidget *view=ui_lookup_widget(window,"menu_view1_menu");
        GList *items=gtk_container_get_children(GTK_CONTAINER(view));
        for(GList *p=items;p;p=p->next) if(GTK_IS_CHECK_MENU_ITEM(p->data) && !g_strcmp0(gtk_menu_item_get_label(GTK_MENU_ITEM(p->data)),"Overview")) overview_menu=p->data;
        g_list_free(items);
        g_assert_nonnull(overview_menu);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),TRUE);
    }
    else if(phase==1)
    {
        same_text(doc);
        keybindings_send_command(GEANY_KEY_GROUP_FOCUS,GEANY_KEYS_INSIGHT_ACTIVATEFILESYMBOLS);
        GtkNotebook *sidebar=GTK_NOTEBOOK(ui_lookup_widget(window,"notebook3"));
        GtkWidget *page=gtk_notebook_get_nth_page(sidebar,gtk_notebook_get_current_page(sidebar));
        g_assert_cmpstr(gtk_notebook_get_tab_label_text(sidebar,page),==,"File Symbols");
        g_assert_nonnull(doc->tm_file);
        g_assert_cmpuint(doc->tm_file->tags_array->len,>,0);
        g_assert_cmpint(SSM(map_for(doc),SCI_GETSTYLEAT,0,0),!=,0);
        keybindings_send_command(GEANY_KEY_GROUP_FOCUS,GEANY_KEYS_INSIGHT_ACTIVATEEDITOR);
        SSM(doc->editor->sci,SCI_INSERTTEXT,0,(sptr_t)"/* данные saving writing */\n");
        SSM(doc->editor->sci,SCI_SETSEL,0,3);
        SSM(doc->editor->sci,SCI_SETFIRSTVISIBLELINE,200,0);
    }
    else if(phase==2)
    {
        same_text(doc);
        ScintillaObject *map=map_for(doc);
        gint first=(gint)SSM(map,SCI_GETFIRSTVISIBLELINE,0,0),main_first=(gint)SSM(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0);
        g_assert_cmpint(first,<=,main_first);
        g_assert_cmpint(first+SSM(map,SCI_LINESONSCREEN,0,0),>,main_first);
        screenshot();
        initial_size=(gint)SSM(map,SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0);
        gint caret=sci_get_current_position(doc->editor->sci);
        GdkEvent *event=gdk_event_new(GDK_BUTTON_PRESS);
        event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map)));
        event->button.button=1;
        event->button.x=10;
        event->button.y=10;
        gtk_widget_event(GTK_WIDGET(map),event);
        gdk_event_free(event);
        event=gdk_event_new(GDK_BUTTON_RELEASE);
        event->button.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map)));
        event->button.button=1;
        event->button.x=10;
        event->button.y=10;
        gtk_widget_event(GTK_WIDGET(map),event);
        gdk_event_free(event);
        g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,caret);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_GETANCHOR,0,0),==,0);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_GETFIRSTVISIBLELINE,0,0),!=,main_first);
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),FALSE);
        g_assert_false(gtk_widget_get_visible(gtk_widget_get_parent(GTK_WIDGET(map))));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(overview_menu),TRUE);
        event=gdk_event_new(GDK_SCROLL);
        event->scroll.window=g_object_ref(gtk_widget_get_window(GTK_WIDGET(map)));
        event->scroll.state=GDK_CONTROL_MASK;
        event->scroll.direction=GDK_SCROLL_UP;
        gtk_widget_event(GTK_WIDGET(map),event);
        gdk_event_free(event);
    }
    else if(phase==3)
    {
        g_assert_cmpint(SSM(doc->editor->sci,SCI_GETIDLESTYLING,0,0),==,SC_IDLESTYLING_ALL);
        same_text(doc);
        g_assert_cmpint(SSM(map_for(doc),SCI_STYLEGETSIZEFRACTIONAL,STYLE_DEFAULT,0),>,initial_size);
        GtkWidget *keywords=find_named(window,"insight-search-keywords"),*variations=find_named(window,"insight-search-variations"),*scope=find_named(window,"insight-search-scope"),*query=find_named(window,"insight-project-query");
        g_assert_nonnull(keywords);
        g_assert_nonnull(variations);
        g_assert_nonnull(scope);
        g_assert_nonnull(query);
        g_assert_false(gtk_widget_get_sensitive(variations));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(keywords),TRUE);
        g_assert_true(gtk_widget_get_sensitive(variations));
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(variations),TRUE);
        gtk_combo_box_set_active(GTK_COMBO_BOX(scope),2);
        gtk_entry_set_text(GTK_ENTRY(query),"save write");
        g_signal_emit_by_name(query,"activate");
    }
    else if(phase==4)
    {
        GtkWidget *view=find_named(window,"insight-search-results"),*preview=find_named(window,"insight-context-code");
        g_assert_nonnull(view);
        g_assert_nonnull(preview);
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);
        if(!retained_search_row)
        {
            GtkTreeIter first;
            g_assert_true(gtk_tree_model_get_iter_first(model,&first));
            GtkTreePath *keep=gtk_tree_model_get_path(model,&first);
            retained_search_row=gtk_tree_row_reference_new(model,keep);
            gtk_tree_selection_select_path(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),keep);
            gtk_tree_path_free(keep);
            GtkWidget *query=find_named(window,"insight-project-query");
            g_signal_emit_by_name(query,"activate");
            return G_SOURCE_CONTINUE;
        }
        GtkWidget *status=find_named(window,"insight-search-status");
        if(!g_str_has_prefix(gtk_label_get_text(GTK_LABEL(status)),"2 results")) return G_SOURCE_CONTINUE;
        g_assert_true(gtk_tree_row_reference_valid(retained_search_row));
        GtkTreeIter retained;
        g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&retained));
        gtk_tree_row_reference_free(retained_search_row);
        retained_search_row=NULL;
        GtkTreeIter iter;
        g_assert_true(gtk_tree_model_get_iter_first(model,&iter));
        gulong start,end;
        gtk_tree_model_get(model,&iter,3,&start,4,&end,-1);
        g_assert_cmpuint(end-start,==,6);
        GtkTreePath *path=gtk_tree_path_new_from_indices(0,-1);
        gtk_tree_view_set_cursor(GTK_TREE_VIEW(view),path,NULL,FALSE);
        gtk_tree_path_free(path);
        g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,28,start),>,0);
        g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,29,start),>,0);
        gint before=sci_get_current_position(doc->editor->sci),anchor=SSM(doc->editor->sci,SCI_GETANCHOR,0,0);
        GdkEvent *browse=gdk_event_new(GDK_KEY_PRESS);
        browse->key.window=g_object_ref(gtk_widget_get_window(view));
        browse->key.state=GDK_MOD1_MASK;
        browse->key.keyval=GDK_KEY_Down;
        gtk_widget_event(view,browse);
        gdk_event_free(browse);
        g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter));
        GtkTreePath *selected=gtk_tree_model_get_path(model,&iter);
        g_assert_cmpint(gtk_tree_path_get_indices(selected)[0],==,1);
        gtk_tree_path_free(selected);
        gtk_tree_model_get(model,&iter,3,&start,4,&end,-1);
        g_assert_cmpuint(end-start,==,7);
        gulong column;
        gtk_tree_model_get(model,&iter,9,&column,-1);
        g_assert_cmpuint(column,==,18);
        g_assert_cmpint(SSM(SCINTILLA(preview),SCI_INDICATORVALUEAT,29,start),>,0);
        g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,before);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_GETANCHOR,0,0),==,anchor);
        GdkEvent *activate=gdk_event_new(GDK_KEY_PRESS);
        activate->key.window=g_object_ref(gtk_widget_get_window(view));
        activate->key.state=GDK_CONTROL_MASK;
        activate->key.keyval=GDK_KEY_l;
        gtk_widget_event(view,activate);
        gdk_event_free(activate);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_GETSELECTIONSTART,0,0),==,start);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_GETSELECTIONEND,0,0),==,end);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,28,start),>,0);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,29,start),>,0);
        const char *change="/* edited after search */\n";
        SSM(doc->editor->sci,SCI_APPENDTEXT,strlen(change),(sptr_t)change);
    }
    else if(phase==5)
    {
        GtkWidget *view=find_named(window,"insight-search-results"),*query=find_named(window,"insight-project-query");
        GtkTreeModel *model=gtk_tree_view_get_model(GTK_TREE_VIEW(view));
        GtkTreeIter iter;
        g_assert_true(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter));
        gulong start,end;
        gtk_tree_model_get(model,&iter,3,&start,4,&end,-1);
        gint before=sci_get_current_position(doc->editor->sci),anchor=SSM(doc->editor->sci,SCI_GETANCHOR,0,0);
        GtkTreePath *path=gtk_tree_model_get_path(model,&iter);
        gtk_tree_view_row_activated(GTK_TREE_VIEW(view),path,NULL);
        gtk_tree_path_free(path);
        g_assert_cmpint(sci_get_current_position(doc->editor->sci),==,before);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_GETANCHOR,0,0),==,anchor);
        /* An edit after the matched line preserves its live source anchor. */ g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,28,start),>,0);
        g_assert_cmpint(SSM(doc->editor->sci,SCI_INDICATORVALUEAT,29,start),>,0);
        gtk_entry_set_text(GTK_ENTRY(query),"(");
        g_signal_emit_by_name(query,"activate");
        g_assert_cmpint(gtk_tree_model_iter_n_children(model,NULL),==,2);
        GtkWidget *context_page=find_named(window,"insight-context");
        g_assert_nonnull(context_page);
        GtkWidget *bottom=gtk_widget_get_parent(context_page);
        g_assert_true(GTK_IS_NOTEBOOK(bottom));
        g_assert_cmpstr(gtk_notebook_get_tab_label_text(GTK_NOTEBOOK(bottom),context_page),==,"Context");
        gtk_notebook_set_current_page(GTK_NOTEBOOK(bottom),gtk_notebook_page_num(GTK_NOTEBOOK(bottom),context_page));
    }
    else if(phase==6)
    {
        GtkWidget *preview=find_named(window,"insight-context-code");
        ScintillaObject *context=SCINTILLA(preview);
        gchar *text=sci_get_contents(context,sci_get_length(context)+1),*name=strstr(text,"function_0");
        g_assert_nonnull(name);
        gint position=(gint)(name-text);
        g_free(text);
        g_assert_cmpint(SSM(context,SCI_INDICATORVALUEAT,31,position),>,0);
        g_assert_cmpint(SSM(context,SCI_GETMARGINTYPEN,1,0),==,SC_MARGIN_NUMBER);
        g_assert_cmpint(SSM(context,SCI_GETMARGINWIDTHN,1,0),>,0);
        gdouble x=(gdouble)SSM(context,SCI_POINTXFROMPOSITION,0,position)+1,y=(gdouble)SSM(context,SCI_POINTYFROMPOSITION,0,position)+1;
        GdkEvent *event=gdk_event_new(GDK_MOTION_NOTIFY);
        event->motion.window=g_object_ref(gtk_widget_get_window(preview));
        event->motion.x=x;
        event->motion.y=y;
        gtk_widget_event(preview,event);
        gdk_event_free(event);
        g_assert_cmpint(SSM(context,SCI_GETCURSOR,0,0),==,8);
        SSM(doc->editor->sci,SCI_SETSEL,0,0);
        event=gdk_event_new(GDK_BUTTON_PRESS);
        event->button.window=g_object_ref(gtk_widget_get_window(preview));
        event->button.button=1;
        event->button.x=x;
        event->button.y=y;
        gtk_widget_event(preview,event);
        gdk_event_free(event);
        /* A single Context click must leave the main editor at its original line. */ g_assert_cmpint(sci_get_current_line(doc->editor->sci),==,0);
        GtkWidget *history=find_named(window,"insight-search-history");
        g_assert_nonnull(history);
        gtk_button_clicked(GTK_BUTTON(history));
        g_timeout_add(150,pick_history,history);
        document_set_text_changed(doc,FALSE);
        GeanyDocument *second=document_new_file(NULL,filetypes_lookup_by_name("C"),"int second(void) { return 2; }\n");
        g_assert_nonnull(second);
        g_assert_true(document_close(document_find_by_id(original_id)));
    }
    else if(phase==7)
    {
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(find_named(window,"insight-project-query"))),==,"save write");
        same_text(doc);
        document_set_text_changed(doc,FALSE);
        GtkWidget *view=find_named(window,"insight-project-files");
        g_assert_nonnull(view);
        g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1);
        GtkWidget *button=find_button(window,"Add and Remove Project Files…");
        g_assert_nonnull(button);
        g_timeout_add(150,edit_membership,NULL);
        gtk_button_clicked(GTK_BUTTON(button));
    }
    else if(phase==8)
    {
        project_empty();
        gchar *path=g_build_filename(test_directory,"overview-smoke.c",NULL);
        g_assert_nonnull(document_open_file(path,FALSE,filetypes_lookup_by_name("C"),NULL));
        g_free(path);
    }
    else if(phase==9)
    {
        project_empty();
        GtkWidget *button=find_button(window,"Add and Remove Project Files…");
        g_assert_nonnull(button);
        g_timeout_add(150,edit_membership,GINT_TO_POINTER(1));
        gtk_button_clicked(GTK_BUTTON(button));
    }
    else if(phase==10)
    {
        project_empty();
        GKeyFile *key=g_key_file_new();
        g_assert_true(g_key_file_load_from_file(key,test_project,G_KEY_FILE_NONE,NULL));
        g_assert_true(g_key_file_get_boolean(key,"insight-project-files","automatic",NULL));
        gsize count=0;
        gchar **excluded=g_key_file_get_string_list(key,"insight-project-files","excluded-files",&count,NULL);
        g_assert_cmpuint(count,==,1);
        g_assert_cmpstr(excluded[0],==,"overview-smoke.c");
        g_strfreev(excluded);
        g_key_file_unref(key);
        document_set_text_changed(doc,FALSE);
        GtkWidget *close=ui_lookup_widget(window,"project_close1"),*open=ui_lookup_widget(window,"project_open1");
        g_assert_true(GTK_IS_MENU_ITEM(close));
        g_assert_true(GTK_IS_MENU_ITEM(open));
        gtk_menu_item_activate(GTK_MENU_ITEM(close));
        g_timeout_add(150,select_project,NULL);
        gtk_menu_item_activate(GTK_MENU_ITEM(open));
    }
    else if(phase==11)
    {
        g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke"));
        project_empty();
        document_set_text_changed(doc,FALSE);
        g_timeout_add(150,dismiss_invalid,NULL);
        g_timeout_add(150,select_project,GINT_TO_POINTER(1));
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
    }
    else if(phase==12)
    {
        g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke"));
        project_empty();
        document_set_text_changed(doc,FALSE);
        g_assert_true(g_file_set_contents(bad_project,"[project\nname=Broken\n",-1,NULL));
        g_timeout_add(150,dismiss_invalid,NULL);
        g_timeout_add(150,select_project,GINT_TO_POINTER(1));
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
    }
    else if(phase==13)
    {
        g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke"));
        project_empty();
        document_set_text_changed(doc,FALSE);
        GtkWidget *query=find_named(window,"insight-project-query");
        gtk_entry_set_text(GTK_ENTRY(query),"function_");
        g_signal_emit_by_name(query,"activate");
        g_timeout_add(150,select_project,GINT_TO_POINTER(2));
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
    }
    else if(phase==14)
    {
        GtkWidget *old_results=find_named(window,"insight-search-results");
        g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(old_results)),NULL),==,0);
        g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI second"));
        GtkWidget *view=find_named(window,"insight-project-files");
        g_assert_nonnull(view);
        g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_tree_view_get_model(GTK_TREE_VIEW(view)),NULL),==,1);
        document_set_text_changed(doc,FALSE);
        g_timeout_add(150,select_project,NULL);
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
    }
    else if(phase==15)
    {
        g_assert_nonnull(strstr(gtk_window_get_title(GTK_WINDOW(window)),"GUI smoke"));
        project_empty();
        document_set_text_changed(doc,FALSE);
        g_timeout_add(150,select_project,GINT_TO_POINTER(2));
        gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"project_open1")));
    }
