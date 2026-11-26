/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared paste editing tools, split by responsibility without changing their operations.
 * RU: Общие инструменты правки, разделённые по ответственности без изменения операций. */
typedef struct {
    GtkWidget *window,*text,*status;
    guint refs;
    guint64 serial;
    gboolean closed;
}
ClipboardView;
typedef struct {
    ClipboardView *view;
    guint64 serial;
}
ClipboardRead;
static ClipboardView *clipboard_view;
/* EN: Release a retained clipboard view reference. view: review state.
* RU: Освобождает удерживаемую ссылку на просмотр буфера. view: состояние просмотра. */
static void clipboard_unref(ClipboardView *view) {
    if(!--view->refs) g_free(view);
}
/* EN: Show asynchronously received clipboard text only in its still-live view. clipboard: clipboard owner;
* text: input text; data: callback context or encoded action.
* RU: Показывает полученный текст буфера только в ещё действующем окне. clipboard: владелец буфера; text:
* входной текст; data: контекст либо код команды. */
static void clipboard_received(GtkClipboard *clipboard,const gchar *text,gpointer data)
{
    (void)clipboard;
    ClipboardRead *read=data;
    ClipboardView *view=read->view;
    if(!view->closed && read->serial==view->serial) {
        const gchar *status=NULL;
        if(!text) status=insight_tr("Clipboard has no text.");
        else if(strlen(text)>256*1024) status=insight_tr("Clipboard text exceeds the 256 KiB display limit.");
        else if(!g_utf8_validate(text,-1,NULL)) status=insight_tr("Clipboard text is not valid UTF-8.");
        else if(!*text) status=insight_tr("Clipboard text is empty.");
        gtk_label_set_text(GTK_LABEL(view->text),status ? "" : text);
        gtk_label_set_text(GTK_LABEL(view->status),status ? status : insight_tr("Clipboard text snapshot. Refresh to read again."));
    }
    clipboard_unref(view);
    g_free(read);
}
/* EN: Request a fresh clipboard snapshot for the view. widget: source widget; data: callback context or
* encoded action.
* RU: Запрашивает свежий снимок буфера для окна просмотра. widget: виджет события; data: контекст либо код
* команды. */
static void clipboard_refresh(GtkWidget *widget,gpointer data)
{
    (void)widget;
    ClipboardView *view=data;
    view->serial++;
    gtk_label_set_text(GTK_LABEL(view->text),"");
    gtk_label_set_text(GTK_LABEL(view->status),insight_tr("Reading clipboard…"));
    ClipboardRead *read=g_new(ClipboardRead,1);
    read->view=view;
    read->serial=view->serial;
    view->refs++;
    gtk_clipboard_request_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),clipboard_received,read);
}
/* EN: Mark the clipboard view closed before pending callbacks complete. widget: source widget; data:
* callback context or encoded action.
* RU: Помечает окно буфера закрытым до завершения отложенных ответов. widget: виджет события; data:
* контекст либо код команды. */
static void clipboard_destroy(GtkWidget *widget,gpointer data)
{
    (void)widget;
    ClipboardView *view=data;
    view->closed=TRUE;
    view->serial++;
    if(clipboard_view==view) clipboard_view=NULL;
    clipboard_unref(view);
}
/* EN: Close the clipboard snapshot window owned by data. widget: source widget; data: callback context or
* encoded action.
* RU: Закрывает окно снимка буфера из data. widget: виджет события; data: контекст либо код команды. */
static void clipboard_close(GtkWidget *widget,gpointer data) {
    (void)widget;
    gtk_widget_destroy(((ClipboardView*)data)->window);
}
/* EN: Open or raise the bounded clipboard snapshot viewer. item: source menu item; data: callback context
* or encoded action.
* RU: Открывает либо поднимает ограниченный просмотр буфера обмена. item: пункт меню; data: контекст либо
* код команды. */
static void show_clipboard(GtkMenuItem *item,gpointer data)
{
    (void)item;
    (void)data;
    if(clipboard_view) {
        clipboard_refresh(NULL,clipboard_view);
        gtk_window_present(GTK_WINDOW(clipboard_view->window));
        return;
    }
    ClipboardView *view=g_new0(ClipboardView,1);
    view->refs=1;
    clipboard_view=view;
    view->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(view->window),insight_tr("Clipboard"));
    gtk_widget_set_name(view->window,"insight-clipboard-window");
    gtk_window_set_default_size(GTK_WINDOW(view->window),720,360);
    gtk_window_set_transient_for(GTK_WINDOW(view->window),GTK_WINDOW(main_widgets.window));
    gtk_window_set_destroy_with_parent(GTK_WINDOW(view->window),TRUE);
    GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6),*scroll=gtk_scrolled_window_new(NULL,NULL),*bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6);
    gtk_container_add(GTK_CONTAINER(view->window),box);
    view->text=gtk_label_new("");
    gtk_widget_set_name(view->text,"insight-clipboard-text");
    g_object_set_data(G_OBJECT(view->text),"insight-no-i18n",GINT_TO_POINTER(1));
    gtk_label_set_xalign(GTK_LABEL(view->text),0);
    gtk_label_set_yalign(GTK_LABEL(view->text),0);
    gtk_label_set_selectable(GTK_LABEL(view->text),FALSE);
    PangoAttrList *attributes=pango_attr_list_new();
    pango_attr_list_insert(attributes,pango_attr_family_new("monospace"));
    gtk_label_set_attributes(GTK_LABEL(view->text),attributes);
    pango_attr_list_unref(attributes);
    gtk_container_add(GTK_CONTAINER(scroll),view->text);
    gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
    view->status=gtk_label_new("");
    gtk_widget_set_name(view->status,"insight-clipboard-status");
    gtk_label_set_line_wrap(GTK_LABEL(view->status),TRUE);
    gtk_box_pack_start(GTK_BOX(box),view->status,FALSE,FALSE,0);
    GtkWidget *refresh=gtk_button_new_with_label(insight_tr("Refresh")),*close=gtk_button_new_with_label(insight_tr("Close"));
    gtk_widget_set_name(refresh,"insight-clipboard-refresh");
    gtk_box_pack_end(GTK_BOX(bar),close,FALSE,FALSE,0);
    gtk_box_pack_end(GTK_BOX(bar),refresh,FALSE,FALSE,0);
    gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);
    g_signal_connect(refresh,"clicked",G_CALLBACK(clipboard_refresh),view);
    g_signal_connect(close,"clicked",G_CALLBACK(clipboard_close),view);
    g_signal_connect(view->window,"destroy",G_CALLBACK(clipboard_destroy),view);
    gtk_widget_show_all(view->window);
    clipboard_refresh(NULL,view);
}
