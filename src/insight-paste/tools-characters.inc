/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
/* EN: Insert a generated GUID into a guarded destination. item: source menu item; data: callback context
* or encoded action.
* RU: Вставляет созданный GUID в проверенное назначение. item: пункт меню; data: контекст либо код
* команды. */
static void insert_guid(GtkMenuItem *item,gpointer data)
{
    (void)item;
    (void)data;
    Request request={
        0
    };
    if(!tools_capture(&request)) return;
    gchar *guid=g_uuid_string_random();
    tools_insert(&request,guid);
    g_free(guid);
}
/* Unicode scalar values; no NUL (the editor's insertion API is NUL-terminated).
* Auto deliberately uses decimal for a plain leading zero, avoiding octal surprises. */
/* EN: Parse a bounded Unicode scalar using radix; return success and write result. input: input text;
* radix: numeric base; result: output/result.
* RU: Разбирает ограниченный Unicode-код с основанием radix; возвращает успех и записывает result. input:
* входной текст; radix: основание числа; result: результат. */
static gboolean character_code(const gchar *input,guint radix,gunichar *result)
{
    gchar *text=g_strdup(input);
    g_strstrip(text);
    const gchar *p=text;
    guint base=radix;
    if(!base) {
        base=10;
        if(g_ascii_strncasecmp(p,"U+",2)==0) {
            base=16;
            p+=2;
        }
        else if(g_ascii_strncasecmp(p,"0x",2)==0) {
            base=16;
            p+=2;
        }
        else if(g_ascii_strncasecmp(p,"0o",2)==0) {
            base=8;
            p+=2;
        }
        else if(g_ascii_strncasecmp(p,"0b",2)==0) {
            base=2;
            p+=2;
        }
    }
    else if((base==16 && (!g_ascii_strncasecmp(p,"0x",2) || !g_ascii_strncasecmp(p,"U+",2))) ||
    (base==8 && !g_ascii_strncasecmp(p,"0o",2)) || (base==2 && !g_ascii_strncasecmp(p,"0b",2))) p+=2;
    guint32 value=0;
    gboolean valid=*p!=0;
    for(;valid && *p;p++) {
        gint digit=g_ascii_xdigit_value(*p);
        if(digit<0 || (guint)digit>=base || value>(0x10ffffu-(guint)digit)/base) valid=FALSE;
        else value=value*base+digit;
    }
    valid=valid && value && g_unichar_validate(value);
    if(valid) *result=value;
    g_free(text);
    return valid;
}
typedef struct {
    GtkWidget *entry,*radix,*preview,*dialog;
}
CharacterDialog;
/* EN: Read a valid Unicode value from dialog into value. dialog: dialog state; value: Unicode output.
* RU: Читает корректный код Unicode из dialog в value. dialog: состояние диалога; value: выходной Unicode-
* код. */
static gboolean character_value(CharacterDialog *dialog,gunichar *value)
{
    const guint bases[]={
        0,10,16,8,2
    };
    gint index=gtk_combo_box_get_active(GTK_COMBO_BOX(dialog->radix));
    return index>=0 && index<(gint)G_N_ELEMENTS(bases) && character_code(gtk_entry_get_text(GTK_ENTRY(dialog->entry)),bases[index],value);
}
/* EN: Update the Unicode character preview after an entry change. widget: source widget; data: callback
* context or encoded action.
* RU: Обновляет предпросмотр символа Unicode после изменения поля. widget: виджет события; data: контекст
* либо код команды. */
static void character_changed(GtkWidget *widget,gpointer data)
{
    (void)widget;
    CharacterDialog *dialog=data;
    gunichar value;
    gboolean valid=character_value(dialog,&value);
    gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog->dialog),GTK_RESPONSE_ACCEPT,valid);
    gchar *preview=NULL;
    if(valid) {
        gchar utf8[7]={
            0
        };
        g_unichar_to_utf8(value,utf8);
        preview=g_unichar_isprint(value) ? g_strdup_printf("%s    U+%04X",utf8,value) : g_strdup_printf("U+%04X",value);
    }
    gtk_label_set_text(GTK_LABEL(dialog->preview),preview ? preview : insight_tr("Enter a character code from U+0001 to U+10FFFF, excluding surrogates."));
    g_free(preview);
}
/* EN: Review and insert one Unicode character into the captured editor. item: source menu item; data:
* callback context or encoded action.
* RU: Показывает и вставляет один символ Unicode в зафиксированный редактор. item: пункт меню; data:
* контекст либо код команды. */
static void insert_character(GtkMenuItem *item,gpointer data)
{
    (void)item;
    (void)data;
    Request request={
        0
    };
    if(!tools_capture(&request)) return;
    CharacterDialog dialog={
        0
    };
    dialog.dialog=gtk_dialog_new_with_buttons(insight_tr("Insert ASCII / Unicode"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Insert"),GTK_RESPONSE_ACCEPT,NULL);
    g_object_ref_sink(dialog.dialog);
    gtk_widget_set_name(dialog.dialog,"insight-insert-character-dialog");
    gtk_window_set_default_size(GTK_WINDOW(dialog.dialog),480,-1);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog.dialog));
    dialog.radix=gtk_combo_box_text_new();
    gtk_widget_set_name(dialog.radix,"insight-character-radix");
    const gchar *bases[]={
        "Auto","Decimal","Hexadecimal","Octal","Binary"
    };
    for(guint i=0;i<G_N_ELEMENTS(bases);i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(dialog.radix),insight_tr(bases[i]));
    gtk_combo_box_set_active(GTK_COMBO_BOX(dialog.radix),0);
    dialog.entry=gtk_entry_new();
    gtk_widget_set_name(dialog.entry,"insight-character-code");
    gtk_entry_set_placeholder_text(GTK_ENTRY(dialog.entry),"65 / 0x41 / U+03B1 / 0o101 / 0b1000001");
    gtk_entry_set_max_length(GTK_ENTRY(dialog.entry),32);
    gtk_entry_set_activates_default(GTK_ENTRY(dialog.entry),TRUE);
    dialog.preview=gtk_label_new("");
    gtk_widget_set_name(dialog.preview,"insight-character-preview");
    gtk_label_set_line_wrap(GTK_LABEL(dialog.preview),TRUE);
    GtkWidget *grid=gtk_grid_new(),*radix_label=gtk_label_new(insight_tr("Radix")),*code_label=gtk_label_new(insight_tr("Character code"));
    gtk_grid_set_row_spacing(GTK_GRID(grid),8);
    gtk_grid_set_column_spacing(GTK_GRID(grid),8);
    gtk_container_set_border_width(GTK_CONTAINER(grid),8);
    gtk_label_set_xalign(GTK_LABEL(radix_label),0);
    gtk_label_set_xalign(GTK_LABEL(code_label),0);
    gtk_widget_set_hexpand(dialog.entry,TRUE);
    gtk_grid_attach(GTK_GRID(grid),radix_label,0,0,1,1);
    gtk_grid_attach(GTK_GRID(grid),dialog.radix,1,0,1,1);
    gtk_grid_attach(GTK_GRID(grid),code_label,0,1,1,1);
    gtk_grid_attach(GTK_GRID(grid),dialog.entry,1,1,1,1);
    gtk_grid_attach(GTK_GRID(grid),dialog.preview,0,2,2,1);
    gtk_box_pack_start(GTK_BOX(box),grid,FALSE,FALSE,0);
    g_signal_connect(dialog.entry,"changed",G_CALLBACK(character_changed),&dialog);
    g_signal_connect(dialog.radix,"changed",G_CALLBACK(character_changed),&dialog);
    gtk_dialog_set_default_response(GTK_DIALOG(dialog.dialog),GTK_RESPONSE_ACCEPT);
    character_changed(NULL,&dialog);
    gtk_widget_show_all(dialog.dialog);
    gtk_widget_grab_focus(dialog.entry);
    for(;;) {
        if(gtk_dialog_run(GTK_DIALOG(dialog.dialog))!=GTK_RESPONSE_ACCEPT) break;
        gunichar value;
        if(!character_value(&dialog,&value)) continue;
        gchar utf8[7]={
            0
        };
        g_unichar_to_utf8(value,utf8);
        if(!tools_insert(&request,utf8)) ui_set_statusbar(TRUE,"Insertion cancelled because the document or selection changed");
        break;
    }
    gtk_widget_destroy(dialog.dialog);
    g_object_unref(dialog.dialog);
}
