/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-list-tools.h"
#include "insight-i18n.h"
#include <string.h>
#include <gdk/gdkkeysyms.h>
#ifdef GEANY_PRIVATE
#include "geany.h"
#include "ui_utils.h"
#endif

/* GTK model/cell access stays on the main thread, in short idle slices. No
 * source files are read and no unloaded page or tree branch is requested. */
#define MAX_ROWS 20000
#define MAX_BYTES (8 * 1024 * 1024)
#define MAX_CELL 65536
typedef struct ListTools ListTools;
typedef struct {
    ListTools *owner;
    GtkTreeModel *model;
    GList *columns, *watched_columns;
    GtkTreePath *path, *selection, *best, *fallback;
    GString *output;
    GRegex *regex;
    gchar *needle;
    guint rows;
    guint64 clipboard_generation;
    gsize bytes;
    gboolean copy, backward, beginning, dirty;
} ListJob;
static guint64 clipboard_generation;
struct ListTools {
    GtkTreeView *view;
    GtkWidget *dialog, *entry, *sensitive, *regex, *beginning, *wrap, *status;
    gchar *pattern;
    gboolean match_case, use_regex, do_wrap, destroyed;
    guint idle;
    ListJob *job;
};
static void status(ListTools *s, const gchar *message) {
    g_object_set_data_full(G_OBJECT(s->view), "insight-list-tools-status", g_strdup(message), g_free);
#ifdef GEANY_PRIVATE
    ui_set_statusbar(FALSE,"%s",insight_tr(message));
#endif
    if(s->dialog) gtk_label_set_text(GTK_LABEL(s->status), insight_tr(message));
}
static void job_free(ListJob *j) {
    g_signal_handlers_disconnect_by_data(j->model, j);
    g_signal_handlers_disconnect_by_data(j->owner->view, j);
    for(GList *p=j->watched_columns;p;p=p->next) { g_signal_handlers_disconnect_by_data(p->data,j);g_object_unref(p->data); }
    g_list_free(j->watched_columns);g_list_free(j->columns);g_object_unref(j->model);
    if(j->path) gtk_tree_path_free(j->path);
    if(j->selection) gtk_tree_path_free(j->selection);
    if(j->best) gtk_tree_path_free(j->best);
    if(j->fallback) gtk_tree_path_free(j->fallback);
    g_string_free(j->output,TRUE);if(j->regex) g_regex_unref(j->regex);g_free(j->needle);g_free(j);
}
static void cancel(ListTools *s) {
    if(s->idle) { g_source_remove(s->idle);s->idle=0; }
    if(s->job) { job_free(s->job);s->job=NULL; }
    g_object_set_data(G_OBJECT(s->view),"insight-list-tools-busy",NULL);
}
static void changed(GtkTreeModel *m,GtkTreePath *p,GtkTreeIter *i,gpointer data) { (void)m;(void)p;(void)i;((ListJob *)data)->dirty=TRUE; }
static void deleted(GtkTreeModel *m,GtkTreePath *p,gpointer data) { (void)m;(void)p;((ListJob *)data)->dirty=TRUE; }
static void reordered(GtkTreeModel *m,GtkTreePath *p,GtkTreeIter *i,gint *order,gpointer data) { (void)order;changed(m,p,i,data); }
static void notify(GObject *o,GParamSpec *ps,gpointer data) { (void)o;(void)ps;((ListJob *)data)->dirty=TRUE; }
static void columns_changed(GtkTreeView *v,gpointer data) { (void)v;((ListJob *)data)->dirty=TRUE; }
static gboolean selection_same(ListJob *j) {
    GtkTreeModel *model=NULL;GtkTreeIter iter;GtkTreePath *path=NULL;
    if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(j->owner->view),&model,&iter)) path=gtk_tree_model_get_path(model,&iter);
    gboolean same=(!path && !j->selection) || (path && j->selection && !gtk_tree_path_compare(path,j->selection));
    if(path) gtk_tree_path_free(path);return same;
}
static void escaped(GString *out,const gchar *text) {
    for(const guchar *p=(const guchar *)text;*p;p++) {
        if(*p=='\\') g_string_append(out,"\\\\");
        else if(*p=='\t') g_string_append(out,"\\t");
        else if(*p=='\r') g_string_append(out,"\\r");
        else if(*p=='\n') g_string_append(out,"\\n");
        else if(*p<32 || *p==127) g_string_append_printf(out,"\\x%02x",*p);
        else g_string_append_c(out,(gchar)*p);
    }
}
static gchar *row_text(ListJob *j,GtkTreeIter *iter) {
    GString *row=g_string_new(NULL);
    for(GList *p=j->columns;p;p=p->next) {
        GtkTreeViewColumn *column=p->data;
        gtk_tree_view_column_cell_set_cell_data(column,j->model,iter,FALSE,FALSE);
        GList *cells=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(column));gboolean first=TRUE;
        if(p!=j->columns) g_string_append_c(row,'\t');
        for(GList *c=cells;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) {
            gchar *text=NULL;g_object_get(c->data,"text",&text,NULL);
            if(text) {
                if(strlen(text)>MAX_CELL || !g_utf8_validate(text,-1,NULL)) { g_free(text);g_list_free(cells);g_string_free(row,TRUE);return NULL; }
                if(!first) g_string_append_c(row,' ');first=FALSE;
                if(j->copy) escaped(row,text);else g_string_append(row,text);
                g_free(text);
            }
        }
        g_list_free(cells);
        if(row->len>MAX_CELL) { g_string_free(row,TRUE);return NULL; }
    }
    return g_string_free(row,FALSE);
}
static void advance(ListJob *j,GtkTreeIter *iter) {
    GtkTreeIter child;
    if(gtk_tree_model_iter_children(j->model,&child,iter)) { gtk_tree_path_down(j->path);return; }
    while(TRUE) {
        gtk_tree_path_next(j->path);
        if(gtk_tree_model_get_iter(j->model,&child,j->path)) return;
        if(!gtk_tree_path_up(j->path) || !gtk_tree_path_get_depth(j->path)) { gtk_tree_path_free(j->path);j->path=NULL;return; }
    }
}
static gboolean finish(ListTools *s,const gchar *error) {
    ListJob *j=s->job;s->idle=0;
    if(!error && j->copy && j->clipboard_generation!=clipboard_generation) error="A newer Copy List command superseded this copy";
    if(!error && (j->dirty || gtk_tree_view_get_model(s->view)!=j->model || (!j->copy && !selection_same(j)))) error="List changed; run the command again";
    if(error) status(s,error);
    else if(j->copy) {
        gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),j->output->str,(gint)j->output->len);
        status(s,"List copied (loaded rows and visible columns only)");
    } else {
        GtkTreePath *match=j->best ? j->best : (s->do_wrap ? j->fallback:NULL);
        /* Disconnect guards before intentional selection/expansion signals. */
        if(match) {
            GtkTreePath *target=gtk_tree_path_copy(match);s->job=NULL;job_free(j);
            g_object_set_data(G_OBJECT(s->view),"insight-list-tools-busy",NULL);
            gtk_tree_view_expand_to_path(s->view,target);gtk_tree_view_set_cursor(s->view,target,NULL,FALSE);
            gtk_tree_view_scroll_to_cell(s->view,target,NULL,TRUE,0.5f,0);gtk_tree_path_free(target);
            status(s,"List match selected; F4 next, Shift+F4 previous");return G_SOURCE_REMOVE;
        }
        status(s,"No matching list item in this direction");
    }
    s->job=NULL;job_free(j);g_object_set_data(G_OBJECT(s->view),"insight-list-tools-busy",NULL);return G_SOURCE_REMOVE;
}
static gboolean scan(gpointer data) {
    ListTools *s=data;ListJob *j=s->job;gint64 deadline=g_get_monotonic_time()+3000;
    if(j->copy && j->clipboard_generation!=clipboard_generation) return finish(s,"A newer Copy List command superseded this copy");
    if(j->dirty || gtk_tree_view_get_model(s->view)!=j->model || !gtk_widget_get_sensitive(GTK_WIDGET(s->view))) return finish(s,"List changed; run the command again");
    while(j->path) {
        if(j->rows>=MAX_ROWS || gtk_tree_path_get_depth(j->path)>64) return finish(s,"List exceeds 20000 rows or 64 levels; narrow the filter");
        GtkTreeIter iter;if(!gtk_tree_model_get_iter(j->model,&iter,j->path)) return finish(s,"List changed; run the command again");
        gchar *text=row_text(j,&iter);if(!text) return finish(s,"List cell exceeds 64 KiB or is not valid UTF-8");
        /* Cell data callbacks are allowed to invalidate/rebuild a model. */
        if(j->dirty) { g_free(text);return finish(s,"List changed; run the command again"); }
        j->bytes+=strlen(text)+1;if(j->bytes>MAX_BYTES) { g_free(text);return finish(s,"List exceeds 8 MiB; narrow the filter"); }
        j->rows++;
        if(j->copy) { for(gint i=1;i<gtk_tree_path_get_depth(j->path);i++) g_string_append(j->output,"  ");g_string_append(j->output,text);g_string_append_c(j->output,'\n'); }
        else {
            GError *error=NULL;gboolean match=FALSE;
            if(j->regex) match=g_regex_match_full(j->regex,text,-1,0,0,NULL,&error);
            else { gchar *hay=s->match_case ? g_strdup(text):g_utf8_casefold(text,-1);match=strstr(hay,j->needle)!=NULL;g_free(hay); }
            if(error) { g_clear_error(&error);g_free(text);return finish(s,"List regular expression exceeded its work limit"); }
            if(match) {
                gint compare=j->selection ? gtk_tree_path_compare(j->path,j->selection):0;
                gboolean eligible=j->beginning || !j->selection || (j->backward ? compare<0:compare>0);
                if(eligible && (!j->best || j->backward)) { if(j->best) gtk_tree_path_free(j->best);j->best=gtk_tree_path_copy(j->path); }
                if(!j->fallback || j->backward) { if(j->fallback) gtk_tree_path_free(j->fallback);j->fallback=gtk_tree_path_copy(j->path); }
            }
        }
        g_free(text);if(j->output->len>MAX_BYTES) return finish(s,"List exceeds 8 MiB; narrow the filter");advance(j,&iter);
        if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE;
    }
    if(j->output->len>MAX_BYTES) return finish(s,"List exceeds 8 MiB; narrow the filter");
    return finish(s,NULL);
}
static void start(ListTools *s,gboolean copy,gboolean backward,gboolean beginning) {
    cancel(s);if(s->destroyed) return;
    if(copy) clipboard_generation++;
    if(!gtk_widget_get_sensitive(GTK_WIDGET(s->view)) || !gtk_tree_view_get_model(s->view)) { status(s,"List is still loading; try again");return; }
    ListJob *j=g_new0(ListJob,1);j->owner=s;j->copy=copy;j->clipboard_generation=clipboard_generation;j->backward=backward;j->beginning=beginning;j->output=g_string_new(NULL);j->model=g_object_ref(gtk_tree_view_get_model(s->view));
    s->job=j;
    if(!copy) {
        if(!s->pattern || !*s->pattern || strlen(s->pattern)>512 || !g_utf8_validate(s->pattern,-1,NULL)) { finish(s,"Enter a list search pattern (up to 512 UTF-8 bytes)");return; }
        if(s->use_regex) {
            gchar *pattern=g_strconcat("(*LIMIT_MATCH=10000)(*LIMIT_RECURSION=100)",s->pattern,NULL);GError *error=NULL;
            j->regex=g_regex_new(pattern,G_REGEX_OPTIMIZE|(s->match_case ? 0:G_REGEX_CASELESS),0,&error);g_free(pattern);
            if(!j->regex) { g_clear_error(&error);finish(s,"Invalid list regular expression");return; }
        } else j->needle=s->match_case ? g_strdup(s->pattern):g_utf8_casefold(s->pattern,-1);
    }
    GtkTreeIter selected;GtkTreeModel *model;
    if(gtk_tree_selection_get_selected(gtk_tree_view_get_selection(s->view),&model,&selected)) j->selection=gtk_tree_model_get_path(model,&selected);
    GList *columns=gtk_tree_view_get_columns(s->view);
    for(GList *p=columns;p;p=p->next) {
        j->watched_columns=g_list_append(j->watched_columns,g_object_ref(p->data));
        g_signal_connect(p->data,"notify::visible",G_CALLBACK(notify),j);g_signal_connect(p->data,"notify::title",G_CALLBACK(notify),j);
        if(!gtk_tree_view_column_get_visible(p->data)) continue;
        j->columns=g_list_append(j->columns,p->data);
        if(copy) { if(j->columns->next) g_string_append_c(j->output,'\t');escaped(j->output,gtk_tree_view_column_get_title(p->data) ? gtk_tree_view_column_get_title(p->data):""); }
    }
    g_list_free(columns);if(copy) g_string_append_c(j->output,'\n');
    if(!j->columns) { finish(s,"No visible list columns");return; }
    g_signal_connect(j->model,"row-inserted",G_CALLBACK(changed),j);g_signal_connect(j->model,"row-changed",G_CALLBACK(changed),j);g_signal_connect(j->model,"row-deleted",G_CALLBACK(deleted),j);g_signal_connect(j->model,"rows-reordered",G_CALLBACK(reordered),j);
    g_signal_connect(s->view,"notify::model",G_CALLBACK(notify),j);g_signal_connect(s->view,"columns-changed",G_CALLBACK(columns_changed),j);
    if(gtk_tree_model_get_iter_first(j->model,&selected)) j->path=gtk_tree_model_get_path(j->model,&selected);
    g_object_set_data(G_OBJECT(s->view),"insight-list-tools-busy",GINT_TO_POINTER(1));status(s,copy ? "Copying loaded list…":"Searching loaded list…");s->idle=g_idle_add(scan,s);
}
static void dialog_destroy(GtkWidget *widget,gpointer data) { (void)widget;ListTools *s=data;s->dialog=NULL;if(s->job && !s->job->copy) cancel(s); }
static void response(GtkDialog *dialog,gint response_id,gpointer data) {
    ListTools *s=data;if(response_id!=GTK_RESPONSE_ACCEPT) { gtk_widget_destroy(GTK_WIDGET(dialog));return; }
    g_free(s->pattern);s->pattern=g_strdup(gtk_entry_get_text(GTK_ENTRY(s->entry)));s->match_case=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->sensitive));s->use_regex=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->regex));s->do_wrap=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->wrap));
    start(s,FALSE,FALSE,gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(s->beginning)));
}
static void search_dialog(ListTools *s) {
    if(s->dialog) { gtk_window_present(GTK_WINDOW(s->dialog));return; }
    GtkWidget *parent=gtk_widget_get_toplevel(GTK_WIDGET(s->view));
    s->dialog=gtk_dialog_new_with_buttons("Search List",GTK_IS_WINDOW(parent) ? GTK_WINDOW(parent):NULL,GTK_DIALOG_DESTROY_WITH_PARENT,"Close",GTK_RESPONSE_CLOSE,"Find",GTK_RESPONSE_ACCEPT,NULL);
    gtk_widget_set_name(s->dialog,"insight-list-search");gtk_window_set_default_size(GTK_WINDOW(s->dialog),440,-1);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(s->dialog));gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_box_set_spacing(GTK_BOX(box),5);
    s->entry=gtk_entry_new();gtk_widget_set_name(s->entry,"insight-list-search-pattern");g_object_set_data(G_OBJECT(s->entry),"insight-no-i18n",GINT_TO_POINTER(1));gtk_entry_set_max_length(GTK_ENTRY(s->entry),512);gtk_entry_set_text(GTK_ENTRY(s->entry),s->pattern ? s->pattern:"");gtk_entry_set_activates_default(GTK_ENTRY(s->entry),TRUE);gtk_box_pack_start(GTK_BOX(box),s->entry,FALSE,FALSE,0);
    const gchar *labels[]={"Match case","Use regular expressions","Start at beginning","Wrap list search"};const gchar *names[]={"insight-list-search-case","insight-list-search-regex","insight-list-search-beginning","insight-list-search-wrap"};GtkWidget **fields[]={&s->sensitive,&s->regex,&s->beginning,&s->wrap};gboolean values[]={s->match_case,s->use_regex,TRUE,s->do_wrap};
    for(guint i=0;i<G_N_ELEMENTS(labels);i++) { *fields[i]=gtk_check_button_new_with_label(labels[i]);gtk_widget_set_name(*fields[i],names[i]);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(*fields[i]),values[i]);gtk_box_pack_start(GTK_BOX(box),*fields[i],FALSE,FALSE,0); }
    s->status=gtk_label_new(insight_tr("Search visible columns of loaded rows; no files are opened"));gtk_widget_set_name(s->status,"insight-list-search-status");gtk_label_set_line_wrap(GTK_LABEL(s->status),TRUE);gtk_box_pack_start(GTK_BOX(box),s->status,FALSE,FALSE,0);
    gtk_dialog_set_default_response(GTK_DIALOG(s->dialog),GTK_RESPONSE_ACCEPT);g_signal_connect(s->dialog,"response",G_CALLBACK(response),s);g_signal_connect(s->dialog,"destroy",G_CALLBACK(dialog_destroy),s);insight_i18n_apply(s->dialog);gtk_widget_show_all(s->dialog);gtk_widget_grab_focus(s->entry);
}
static void action(GtkWidget *item,gpointer data) {
    ListTools *s=g_object_get_data(G_OBJECT(data),"insight-list-tools");if(!s || s->destroyed) return;guint mode=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(item),"insight-list-action"));
    if(mode==1 || (mode>=2 && (!s->pattern || !*s->pattern))) search_dialog(s);
    else start(s,mode==0,mode==3,FALSE);
}
static void destroyed(GtkWidget *widget,gpointer data) { (void)widget;ListTools *s=data;s->destroyed=TRUE;cancel(s);if(s->dialog) gtk_widget_destroy(s->dialog); }
static void freed(gpointer data) { ListTools *s=data;g_free(s->pattern);g_free(s); }
static gboolean widget_key(GtkWidget *widget,GdkEventKey *event,gpointer data) { (void)data;return insight_list_tools_key(widget,event); }
void insight_list_tools_attach(GtkTreeView *view) {
    if(g_object_get_data(G_OBJECT(view),"insight-list-tools")) return;
    ListTools *s=g_new0(ListTools,1);s->view=view;
    g_object_set_data_full(G_OBJECT(view),"insight-list-tools",s,freed);g_signal_connect(view,"destroy",G_CALLBACK(destroyed),s);g_signal_connect(view,"key-press-event",G_CALLBACK(widget_key),NULL);
}
void insight_list_tools_menu(GtkTreeView *view,GtkWidget *menu) {
    insight_list_tools_attach(view);if(g_object_get_data(G_OBJECT(menu),"insight-list-tools-added")) return;
    g_object_set_data(G_OBJECT(menu),"insight-list-tools-added",GINT_TO_POINTER(1));
    gtk_menu_shell_append(GTK_MENU_SHELL(menu),gtk_separator_menu_item_new());
    const gchar *labels[]={"Copy List","Search List…","Next list match (F4)","Previous list match (Shift+F4)"};const gchar *names[]={"insight-list-copy","insight-list-search-command","insight-list-next","insight-list-previous"};
    for(guint i=0;i<G_N_ELEMENTS(labels);i++) { GtkWidget *item=gtk_menu_item_new_with_label(labels[i]);gtk_widget_set_name(item,names[i]);g_object_set_data(G_OBJECT(item),"insight-list-action",GUINT_TO_POINTER(i));g_signal_connect_object(item,"activate",G_CALLBACK(action),view,0);gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
}
static void menu_free(gpointer data) { gtk_widget_destroy(GTK_WIDGET(data));g_object_unref(data); }
static GtkWidget *default_menu(GtkTreeView *view) {
    GtkWidget *menu=g_object_get_data(G_OBJECT(view),"insight-list-popup");
    if(!menu) {
        menu=gtk_menu_new();g_object_ref_sink(menu);gtk_menu_attach_to_widget(GTK_MENU(menu),GTK_WIDGET(view),NULL);
        insight_list_tools_menu(view,menu);
        /* The shared appender separates commands from an existing menu. */
        GList *children=gtk_container_get_children(GTK_CONTAINER(menu));if(children) gtk_widget_destroy(children->data);g_list_free(children);
        g_object_set_data_full(G_OBJECT(view),"insight-list-popup",menu,menu_free);
    }
    insight_i18n_apply(menu);gtk_widget_show_all(menu);return menu;
}
static gboolean popup(GtkWidget *widget,gpointer data) {
    (void)data;gtk_menu_popup_at_widget(GTK_MENU(default_menu(GTK_TREE_VIEW(widget))),widget,GDK_GRAVITY_SOUTH_WEST,GDK_GRAVITY_NORTH_WEST,NULL);return TRUE;
}
static gboolean popup_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
    (void)data;if(event->type!=GDK_BUTTON_PRESS || event->button!=3) return FALSE;
    gtk_widget_grab_focus(widget);gtk_menu_popup_at_pointer(GTK_MENU(default_menu(GTK_TREE_VIEW(widget))),(GdkEvent *)event);return TRUE;
}
void insight_list_tools_popup_attach(GtkTreeView *view) {
    insight_list_tools_attach(view);if(g_object_get_data(G_OBJECT(view),"insight-list-default-menu")) return;
    g_object_set_data(G_OBJECT(view),"insight-list-default-menu",GINT_TO_POINTER(1));
    g_signal_connect(view,"popup-menu",G_CALLBACK(popup),NULL);g_signal_connect(view,"button-press-event",G_CALLBACK(popup_button),NULL);
}
gboolean insight_list_tools_key(GtkWidget *widget,GdkEventKey *event) {
    if(!widget || !GTK_IS_TREE_VIEW(widget) || event->keyval!=GDK_KEY_F4 || (event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK|GDK_META_MASK))) return FALSE;
    ListTools *s=g_object_get_data(G_OBJECT(widget),"insight-list-tools");if(!s) return FALSE;
    if(!s->pattern || !*s->pattern) search_dialog(s);else start(s,FALSE,(event->state&GDK_SHIFT_MASK)!=0,FALSE);return TRUE;
}
