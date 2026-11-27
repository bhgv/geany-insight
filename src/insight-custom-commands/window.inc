/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Construct the modeless external-tool editor and its context, argument, output and diagnostic pages.
 * RU: Создаёт немодальное окно внешних инструментов и страницы контекста, аргументов, вывода и диагностики. */

/* EN: Present or build the command window without executing tools; item/unused are unused menu callback arguments.
 * RU: Показывает либо создаёт окно команд без запуска инструментов; item/unused — неиспользуемые аргументы меню. */
static void show(GtkMenuItem *item,gpointer unused)
{
    (void)item;
    (void)unused;
    if(command_window)
    {
        gtk_window_present(GTK_WINDOW(command_window));
        return;
    }
    CommandView *v=g_new0(CommandView,1);
    v->refs=1;
    v->locked=g_ptr_array_new();
    v->path=g_build_filename(app->configdir,"insight","custom-commands.conf",NULL);
    GError *error=NULL;
    v->commands=insight_custom_commands_load(v->path,&error);
    if (!v->commands)
        v->commands = g_ptr_array_new_with_free_func(insight_custom_command_free);
    v->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    command_window=v->window;
    gtk_widget_set_name(v->window,"insight-custom-commands");
    gtk_window_set_transient_for(GTK_WINDOW(v->window),GTK_WINDOW(main_widgets.window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(v->window),TRUE);
    insight_i18n_key(G_OBJECT(v->window),"title","Custom Commands");
    g_object_set_data_full(G_OBJECT(v->window),"insight-custom-state",v,view_unref);
    g_signal_connect(v->window,"destroy",G_CALLBACK(destroyed),v);
    GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);
    gtk_container_add(GTK_CONTAINER(v->window),root);
    v->tabs=insight_compare_ui_tabs(root,"insight-custom-command-tabs");
    GtkWidget *commands=insight_compare_ui_page(v->tabs,"Commands"),*context=insight_compare_ui_page(v->tabs,"Context"),*output=insight_compare_ui_page(v->tabs,"Output"),*arguments=insight_compare_ui_page(v->tabs,"Arguments"),*diagnostics=insight_compare_ui_page(v->tabs,"Diagnostics");
    GtkWidget *arg_grid=gtk_grid_new();
    gtk_grid_set_column_spacing(GTK_GRID(arg_grid),6);
    gtk_grid_set_row_spacing(GTK_GRID(arg_grid),3);
    gtk_box_pack_start(GTK_BOX(arguments),arg_grid,FALSE,FALSE,0);
    for(guint i=0;i<9;i++)
    {
        gchar *label=g_strdup_printf("%%%u",i+1),*name=g_strdup_printf("insight-custom-command-argument-%u",i+1);
        GtkWidget *l=gtk_label_new(label);
        v->arguments[i]=gtk_entry_new();
        gtk_widget_set_name(v->arguments[i],name);
        insight_ai_ui_entry(v->arguments[i],"Value substituted for this numbered command argument",1);
        insight_i18n_key(G_OBJECT(v->arguments[i]),"tooltip-text","Value substituted for this numbered command argument");
        gtk_grid_attach(GTK_GRID(arg_grid),l,(i%3)*2,i/3,1,1);
        gtk_grid_attach(GTK_GRID(arg_grid),v->arguments[i],(i%3)*2+1,i/3,1,1);
        g_ptr_array_add(v->locked,v->arguments[i]);
        g_free(label);
        g_free(name);
    }
    v->profiles=gtk_combo_box_text_new();
    gtk_widget_set_name(v->profiles,"insight-custom-command-profiles");
    gtk_box_pack_start(GTK_BOX(commands),v->profiles,FALSE,FALSE,0);
    g_ptr_array_add(v->locked,v->profiles);
    g_signal_connect(v->profiles,"changed",G_CALLBACK(select_profile),v);
    v->name=entry(commands,"Name","insight-custom-command-name","Name of the saved external tool",v);
    v->command=entry(commands,"Command","insight-custom-command-template","Executable and quoted arguments; context variables expand inside each argument",v);
    v->directory=entry(commands,"Working directory","insight-custom-command-directory","Absolute working directory; empty uses the current project or file directory",v);
    shortcut_options(v,commands);
    GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(commands),row,FALSE,FALSE,0);
    const gchar *labels[]=
    {
        "New command","Save command","Delete command","Move up","Move down"
    };
    for(guint i=0;i<5;i++)
    {
        gchar *name=g_strdup_printf("insight-custom-command-edit-%u",i);
        GtkWidget *b=button(row,labels[i],name,G_CALLBACK(edit_profile),v,TRUE);
        g_object_set_data(G_OBJECT(b),"custom-action",GINT_TO_POINTER(i));
        g_free(name);
    }
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(commands),row,FALSE,FALSE,0);
    v->run=button(row,"Run","insight-custom-command-run",G_CALLBACK(run),v,FALSE);
    v->stop=button(row,"Stop","insight-custom-command-stop",G_CALLBACK(stop),v,FALSE);
    button(row,"Preview command","insight-custom-command-preview",G_CALLBACK(preview),v,FALSE);
    GtkWidget *refresh=gtk_button_new_with_label("");
    insight_i18n_key(G_OBJECT(refresh),"label","Refresh context");
    gtk_box_pack_start(GTK_BOX(context),refresh,FALSE,FALSE,0);
    g_signal_connect(refresh,"clicked",G_CALLBACK(preview),v);
    v->context=text_view(context,"insight-custom-command-context",FALSE);
    GtkWidget *sc=gtk_widget_get_parent(v->context);
    gtk_widget_set_size_request(sc,-1,180);
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(output),row,FALSE,FALSE,0);
    const gchar *outs[]=
    {
        "Clear output","Copy output","Save command output"
    };
    for(guint i=0;i<3;i++)
    {
        gchar *name=g_strdup_printf("insight-custom-command-output-%u",i);
        GtkWidget *b=button(row,outs[i],name,G_CALLBACK(output_action),v,FALSE);
        g_object_set_data(G_OBJECT(b),"output-action",GINT_TO_POINTER(i));
        g_free(name);
    }
    output_editor_buttons(v,output);
    diagnostic_options(v,diagnostics);
    row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);
    gtk_box_pack_start(GTK_BOX(diagnostics),row,FALSE,FALSE,0);
    button(row,"Parse Source Links","insight-custom-command-parse-links",G_CALLBACK(parse_links),v,FALSE);
    const gchar *link_labels[]=
    {
        "First link","Previous link","Next link"
    };
    const gint directions[]=
    {
        0,-1,1
    };
    for(guint i=0;i<3;i++)
    {
        gchar *name=g_strdup_printf("insight-custom-command-link-%u",i);
        GtkWidget *b=button(row,link_labels[i],name,G_CALLBACK(link_button),v,FALSE);
        g_object_set_data(G_OBJECT(b),"link-direction",GINT_TO_POINTER(directions[i]));
        g_free(name);
    }
    button(row,"Open source","insight-custom-command-open-link",G_CALLBACK(open_link),v,FALSE);
    v->links_status=gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(v->links_status),0);
    g_object_set_data(G_OBJECT(v->links_status),"insight-no-i18n",GINT_TO_POINTER(1));
    gtk_box_pack_start(GTK_BOX(diagnostics),v->links_status,FALSE,FALSE,0);
    v->links=gtk_list_store_new(LINK_FIELDS,G_TYPE_STRING,G_TYPE_UINT,G_TYPE_UINT,G_TYPE_STRING);
    v->links_view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(v->links));
    g_object_unref(v->links);
    gtk_widget_set_name(v->links_view,"insight-custom-command-links");
    gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(v->links_view),LINK_FILE);
    gtk_tree_view_set_search_column(GTK_TREE_VIEW(v->links_view),LINK_MESSAGE);
    const gchar *headings[]=
    {
        "File","Line","Column","Message"
    };
    for(guint i=0;i<LINK_FIELDS;i++)
    {
        GtkCellRenderer *cell=gtk_cell_renderer_text_new();
        if(i==LINK_FILE || i==LINK_MESSAGE) g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,"width-chars",1,"max-width-chars",50,NULL);
        GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes("",cell,"text",i,NULL);
        insight_i18n_key(G_OBJECT(column),"title",headings[i]);
        if(i==LINK_COLUMN) gtk_tree_view_column_set_cell_data_func(column,cell,link_column,NULL,NULL);
        gtk_tree_view_column_set_resizable(column,TRUE);
        gtk_tree_view_column_set_expand(column,i==LINK_FILE || i==LINK_MESSAGE);
        gtk_tree_view_column_set_sort_column_id(column,i);
        gtk_tree_view_append_column(GTK_TREE_VIEW(v->links_view),column);
    }
    GtkWidget *link_scroll=gtk_scrolled_window_new(NULL,NULL);
    gtk_widget_set_size_request(link_scroll,-1,160);
    gtk_container_add(GTK_CONTAINER(link_scroll),v->links_view);
    gtk_box_pack_start(GTK_BOX(diagnostics),link_scroll,FALSE,FALSE,0);
    g_signal_connect(v->links_view,"row-activated",G_CALLBACK(link_activated),v);
    g_signal_connect(v->window,"key-press-event",G_CALLBACK(command_key),v);
    link_count(v);
    v->status=gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(v->status),0);
    gtk_label_set_selectable(GTK_LABEL(v->status),TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(v->status),PANGO_ELLIPSIZE_END);
    gtk_box_pack_start(GTK_BOX(root),v->status,FALSE,FALSE,0);
    v->output=text_view(root,"insight-custom-command-output",TRUE);
    insight_panel_help_notebook(GTK_NOTEBOOK(v->tabs),"custom-commands.md");
    rebuild(v,v->commands->len ? 0:-1);
    if(!v->commands->len) gtk_entry_set_text(GTK_ENTRY(v->directory),"%j");
    note(v,error ? error->message:"Ready to run an external tool");
    g_clear_error(&error);
    insight_i18n_on_refresh(G_OBJECT(v->window),locale,v);
    controls(v);
    insight_ai_ui_window(v->window,"Minimize window","Maximize / restore window","Close window");
    GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(v->window));
    if(GTK_IS_HEADER_BAR(header))
    {
        v->title=gtk_label_new(NULL);
        gtk_widget_set_name(v->title,"insight-custom-command-title");
        gtk_style_context_add_class(gtk_widget_get_style_context(v->title),"title");
        gtk_label_set_ellipsize(GTK_LABEL(v->title),PANGO_ELLIPSIZE_END);
        gtk_label_set_width_chars(GTK_LABEL(v->title),1);
        insight_i18n_key(G_OBJECT(v->title),"label","Custom Commands");
        gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header),v->title);
    }
    gtk_widget_show_all(v->window);
}
