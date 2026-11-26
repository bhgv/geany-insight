/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
/* EN: Refresh text-tool availability for the active destination. menu: target menu; data: callback context
* or encoded action.
* RU: Обновляет доступность текстовых инструментов для текущего назначения. menu: меню назначения; data:
* контекст либо код команды. */
static void tools_menu_show(GtkWidget *menu,gpointer data)
{
    (void)data;
    Request request={
        0
    };
    gboolean ready=tools_capture(&request);
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"renumber"),ready||insight_renumber_ranges_available());
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"guid"),ready);
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"character"),ready);
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"insert-file"),ready);
    GeanyDocument *doc=document_get_current();
    gboolean single=doc && SEND(doc->editor->sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(doc->editor->sci)==SC_SEL_STREAM && sci_has_selection(doc->editor->sci) && !SEND(doc->editor->sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(doc->editor->sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0);
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"save-selection"),single);
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"save-copy"),doc!=NULL);
    gtk_widget_set_sensitive(g_object_get_data(G_OBJECT(menu),"buffer-transfer"),doc!=NULL);
}
/* EN: Create the shared editing tools menu. menu: target menu.
* RU: Создаёт общее меню инструментов правки. menu: меню назначения. */
void insight_paste_tools_menu(GtkWidget *menu)
{
    paragraph_menu(menu);
    calculate_menu(menu);
    word_edit_menu(menu);
    GtkWidget *parent=gtk_menu_item_new_with_label("Insert and clipboard"),*sub=gtk_menu_new();
    gtk_widget_set_name(parent,"insight-insert-tools");
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),sub);
    const gchar *labels[]={
        "Insert GUID","Insert ASCII / Unicode…","Show Clipboard"
    },*ids[]={
        "insight-insert-guid","insight-insert-character","insight-show-clipboard"
    };
    GCallback actions[]={
        G_CALLBACK(insert_guid),G_CALLBACK(insert_character),G_CALLBACK(show_clipboard)
    };
    for(guint i=0;i<G_N_ELEMENTS(labels);i++) {
        GtkWidget *entry=gtk_menu_item_new_with_label(labels[i]);
        gtk_widget_set_name(entry,ids[i]);
        if(i<2) g_object_set_data(G_OBJECT(sub),i ? "character" : "guid",entry);
        g_signal_connect(entry,"activate",actions[i],NULL);
        gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry);
    }
    gtk_menu_shell_append(GTK_MENU_SHELL(sub),gtk_separator_menu_item_new());
    const gchar *file_labels[]={
        "Insert File…","Save Selection…","Save a Copy…"
    },*file_ids[]={
        "insight-insert-file","insight-save-selection","insight-save-copy"
    },*file_keys[]={
        "insert-file","save-selection","save-copy"
    };
    for(guint i=0;i<3;i++) {
        GtkWidget *entry=gtk_menu_item_new_with_label(file_labels[i]);
        gtk_widget_set_name(entry,file_ids[i]);
        g_object_set_data(G_OBJECT(sub),file_keys[i],entry);
        g_signal_connect(entry,"activate",i ? G_CALLBACK(export_text) : G_CALLBACK(insert_file),GINT_TO_POINTER(i==1));
        gtk_menu_shell_append(GTK_MENU_SHELL(sub),entry);
    }
    GtkWidget *transfer=gtk_menu_item_new_with_label("Copy Text to Buffer…");
    gtk_widget_set_name(transfer,"insight-buffer-transfer");
    g_object_set_data(G_OBJECT(sub),"buffer-transfer",transfer);
    g_signal_connect(transfer,"activate",G_CALLBACK(transfer_text),NULL);
    keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_BUFFERTRANSFER,transfer);
    gtk_menu_shell_append(GTK_MENU_SHELL(sub),transfer);
    GtkWidget *renumber=gtk_menu_item_new_with_label("Renumber…");
    gtk_widget_set_name(renumber,"insight-renumber");
    keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_RENUMBER,renumber);
    g_object_set_data(G_OBJECT(sub),"renumber",renumber);
    g_signal_connect(renumber,"activate",G_CALLBACK(renumber_text),NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(sub),renumber);
    g_signal_connect(sub,"show",G_CALLBACK(tools_menu_show),NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),parent);
    gtk_widget_show_all(parent);
}
