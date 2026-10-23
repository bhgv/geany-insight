/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-recording.h"
#include "document.h"
#include "geanyobject.h"
#include "keybindings.h"
#include "main.h"
#include "project.h"
#include "utils.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-completion.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define ACTION_LIMIT 256u
#define TEXT_LIMIT 65536u
#define DOCUMENT_LIMIT 1048576u
#define WORK_LIMIT 16777216u
/* The whitelist deliberately excludes clipboard, absolute positions, search
 * strings, rectangle modes, and any opaque pointer-bearing messages. */
#define MOVEMENTS(X) \
 X(SCI_CHARLEFT) X(SCI_CHARRIGHT) X(SCI_CHARLEFTEXTEND) X(SCI_CHARRIGHTEXTEND) \
 X(SCI_WORDLEFT) X(SCI_WORDRIGHT) X(SCI_WORDLEFTEXTEND) X(SCI_WORDRIGHTEXTEND) \
 X(SCI_WORDPARTLEFT) X(SCI_WORDPARTRIGHT) X(SCI_WORDPARTLEFTEXTEND) X(SCI_WORDPARTRIGHTEXTEND) \
 X(SCI_LINEUP) X(SCI_LINEDOWN) X(SCI_LINEUPEXTEND) X(SCI_LINEDOWNEXTEND) \
 X(SCI_HOME) X(SCI_LINEEND) X(SCI_HOMEEXTEND) X(SCI_LINEENDEXTEND) \
 X(SCI_VCHOME) X(SCI_VCHOMEEXTEND) X(SCI_DOCUMENTSTART) X(SCI_DOCUMENTEND) \
 X(SCI_DOCUMENTSTARTEXTEND) X(SCI_DOCUMENTENDEXTEND) X(SCI_SELECTALL)
#define EDITS(X) \
 X(SCI_CLEAR) X(SCI_DELETEBACK) X(SCI_DELETEBACKNOTLINE) X(SCI_DELWORDLEFT) X(SCI_DELWORDRIGHT) \
 X(SCI_DELLINELEFT) X(SCI_DELLINERIGHT) X(SCI_LINEDELETE) X(SCI_LINEDUPLICATE) \
 X(SCI_LOWERCASE) X(SCI_UPPERCASE)
typedef struct { guint message; gchar *text; } RecordedAction;
static GPtrArray *actions;
static guint recording_document, payload_bytes;
static gboolean playing;
static GtkWidget *items[5];
static GtkWidget *remember_item;
static gboolean remember, project_scope, syncing;
static GKeyFile *global_recording;
#define RECORD_GROUP "insight-command-recording"
static const gchar *message_name(guint message)
{
    switch(message) {
#define NAME(m) case m: return #m;
        MOVEMENTS(NAME) EDITS(NAME) NAME(SCI_REPLACESEL)
#undef NAME
        default: return NULL;
    }
}
static gboolean ordinary(GeanyDocument *doc)
{
    if(!DOC_VALID(doc)) return FALSE;
    ScintillaObject *sci=doc->editor->sci;
    return !doc->readonly && !SEND(sci,SCI_GETREADONLY,0,0) &&
        SEND(sci,SCI_GETSELECTIONS,0,0)==1 && SEND(sci,SCI_GETSELECTIONMODE,0,0)==SC_SEL_STREAM &&
        !SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) &&
        SEND(sci,SCI_GETLENGTH,0,0)<=DOCUMENT_LIMIT;
}
static void action_free(gpointer data) { RecordedAction *a=data;g_free(a->text);g_free(a); }
static void stop(void)
{
    GeanyDocument *doc=document_find_by_id(recording_document);recording_document=0;
    if(doc) SEND(doc->editor->sci,SCI_STOPRECORD,0,0);
}
static void discard(void) { stop();g_ptr_array_set_size(actions,0);payload_bytes=0; }
/* Persist symbolic, whitelisted messages only, never raw messages/pointers. */
static void write_recording(GKeyFile *key,gboolean snapshot)
{
    g_key_file_remove_group(key,RECORD_GROUP,NULL);
    g_key_file_set_integer(key,RECORD_GROUP,"version",1);
    g_key_file_set_boolean(key,RECORD_GROUP,"remember",remember);
    if((!remember && !snapshot) || !actions->len) return;
    const gchar *commands[ACTION_LIMIT],*texts[ACTION_LIMIT];
    for(guint i=0;i<actions->len;i++) {
        RecordedAction *a=g_ptr_array_index(actions,i);commands[i]=message_name(a->message);texts[i]=a->text ? a->text:"";
    }
    g_key_file_set_string_list(key,RECORD_GROUP,"commands",commands,actions->len);
    g_key_file_set_string_list(key,RECORD_GROUP,"texts",texts,actions->len);
}
static guint message_from_name(const gchar *name)
{
#define MATCH(m) if(!strcmp(name,#m)) return m;
    MOVEMENTS(MATCH) EDITS(MATCH) MATCH(SCI_REPLACESEL)
#undef MATCH
    return 0;
}
static void read_recording(GKeyFile *key,gboolean snapshot)
{
    discard();remember=FALSE;
    if(g_key_file_get_integer(key,RECORD_GROUP,"version",NULL)!=1) return;
    remember=g_key_file_get_boolean(key,RECORD_GROUP,"remember",NULL);
    if(!remember && !snapshot) return;
    if(!g_key_file_has_key(key,RECORD_GROUP,"commands",NULL) && !g_key_file_has_key(key,RECORD_GROUP,"texts",NULL)) return;
    gsize n=0,t=0;gchar **commands=g_key_file_get_string_list(key,RECORD_GROUP,"commands",&n,NULL),**texts=g_key_file_get_string_list(key,RECORD_GROUP,"texts",&t,NULL);
    gboolean valid=commands && texts && n>0 && n==t && n<=ACTION_LIMIT;
    for(gsize i=0;valid && i<n;i++) {
        guint message=message_from_name(commands[i]);gsize length=strlen(texts[i]);
        if(!message || length>TEXT_LIMIT-payload_bytes || !g_utf8_validate(texts[i],length,NULL) || (message!=SCI_REPLACESEL && length)) { valid=FALSE;break; }
        RecordedAction *a=g_new0(RecordedAction,1);a->message=message;a->text=message==SCI_REPLACESEL ? g_strdup(texts[i]):NULL;g_ptr_array_add(actions,a);payload_bytes+=length;
    }
    g_strfreev(commands);g_strfreev(texts);
    if(!valid) { discard();remember=FALSE;ui_set_statusbar(TRUE,"%s",insight_tr("Saved command recording rejected: unsupported format, action or limit")); }
}
static void copy_recording(GKeyFile *from,GKeyFile *to)
{
    g_key_file_remove_group(to,RECORD_GROUP,NULL);
    gsize n=0;gchar **keys=g_key_file_get_keys(from,RECORD_GROUP,&n,NULL);
    for(gsize i=0;i<n;i++) {
        if(!g_key_file_get_boolean(from,RECORD_GROUP,"remember",NULL) && (!strcmp(keys[i],"commands") || !strcmp(keys[i],"texts"))) continue;
        gchar *value=g_key_file_get_value(from,RECORD_GROUP,keys[i],NULL);g_key_file_set_value(to,RECORD_GROUP,keys[i],value);g_free(value);
    }
    g_strfreev(keys);
}
static void settings_saved(GObject *object,GKeyFile *key,gpointer data)
{
    (void)object;(void)data;
    if(project_scope) copy_recording(global_recording,key);else write_recording(key,FALSE);
}
static void project_opened(GObject *object,GKeyFile *key,gpointer data)
{
    (void)object;(void)data;stop();
    if(!project_scope) write_recording(global_recording,TRUE);
    project_scope=TRUE;read_recording(key,FALSE);
}
static void project_saved(GObject *object,GKeyFile *key,gpointer data)
{
    (void)object;(void)data;
    /* New projects have no project-open signal: retain the global recording
     * separately, never silently copy its source text into a project file. */
    if(!project_scope) { stop();write_recording(global_recording,TRUE);discard();remember=FALSE;project_scope=TRUE; }
    write_recording(key,FALSE);
}
static void project_closed(GObject *object,gpointer data)
{
    (void)object;(void)data;if(!project_scope) return;
    project_scope=FALSE;read_recording(global_recording,TRUE);
}
static void remember_toggled(GtkCheckMenuItem *item,gpointer data)
{
    (void)data;if(!syncing) remember=gtk_check_menu_item_get_active(item);
}
static void failed(void)
{
    discard();ui_set_statusbar(TRUE,"%s",insight_tr("Recording discarded: unsupported action, target or limit"));
}
static void changed_document(GObject *object,GeanyDocument *doc,gpointer data)
{
    (void)object;(void)data;
    if(recording_document && (!doc || doc->id!=recording_document)) stop();
}
static void closing_document(GObject *object,GeanyDocument *doc,gpointer data)
{
    (void)object;(void)data;if(doc && doc->id==recording_document) stop();
}
/* Mouse moves cannot be represented by relative editor commands. Stop before
 * the native handler changes the caret; the completed prefix remains playable. */
static gboolean mouse_pressed(GtkWidget *widget,GdkEventButton *event,gpointer data)
{
    (void)widget;(void)event;(void)data;if(recording_document) stop();return FALSE;
}
void insight_recording_notify(GeanyEditor *editor,SCNotification *nt)
{
    if(!recording_document || playing || editor->document->id!=recording_document) return;
    if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType&(SC_PERFORMED_UNDO|SC_PERFORMED_REDO))) { failed();return; }
    if(nt->nmhdr.code!=SCN_MACRORECORD) return;
    if(!ordinary(editor->document) || !message_name(nt->message) || actions->len>=ACTION_LIMIT) { failed();return; }
    gchar *text=NULL;
    if(nt->message==SCI_REPLACESEL) {
        const gchar *source=(const gchar *)nt->lParam;
        if(!source) { failed();return; }
        gsize length=0;while(length<=TEXT_LIMIT && source[length]) length++;
        if(length>TEXT_LIMIT || payload_bytes+length>TEXT_LIMIT || !g_utf8_validate(source,length,NULL)) { failed();return; }
        text=g_strndup(source,length);payload_bytes+=length;
    }
    RecordedAction *action=g_new0(RecordedAction,1);action->message=nt->message;action->text=text;g_ptr_array_add(actions,action);
}
static void play(void)
{
    stop();GeanyDocument *doc=document_get_current();
    if(!actions->len || !ordinary(doc) || gtk_window_get_focus(GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(doc->editor->sci))))!=GTK_WIDGET(doc->editor->sci)) return;
    ScintillaObject *sci=doc->editor->sci;guint work=0,done=0;playing=TRUE;
    insight_completion_cancel(doc->editor);SEND(sci,SCI_AUTOCCANCEL,0,0);sci_start_undo_action(sci);
    for(guint i=0;i<actions->len;i++) {
        RecordedAction *a=g_ptr_array_index(actions,i);gsize length=SEND(sci,SCI_GETLENGTH,0,0),growth=a->text ? strlen(a->text):0;
        if(a->message==SCI_LINEDUPLICATE || a->message==SCI_UPPERCASE || a->message==SCI_LOWERCASE) growth=length*3;
        if(!ordinary(doc) || length+growth>DOCUMENT_LIMIT || work+length+growth>WORK_LIMIT) break;
        work+=length+growth;SEND(sci,a->message,0,(sptr_t)a->text);done++;
    }
    sci_end_undo_action(sci);playing=FALSE;SEND(sci,SCI_SCROLLCARET,0,0);
    ui_set_statusbar(TRUE,"%s",insight_tr(done==actions->len ? "Recording played; one Undo restores edits":"Recording stopped at its work limit; completed edits can be undone"));
}
static void preview(void)
{
    stop();GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Recorded Editor Commands"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,
        insight_tr("Close"),GTK_RESPONSE_CLOSE,NULL);
    g_object_ref_sink(dialog);gtk_widget_set_name(dialog,"insight-recording-dialog");gtk_window_set_default_size(GTK_WINDOW(dialog),720,420);
    GtkWidget *box=gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget *hint=gtk_label_new(insight_tr("Relative native editor commands and literal input. Start replaces the previous recording. Remember Recording saves literal source text in the current project file or global preferences; playback is always explicit."));
    gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);gtk_box_pack_start(GTK_BOX(box),hint,FALSE,FALSE,6);
    GtkListStore *store=gtk_list_store_new(3,G_TYPE_UINT,G_TYPE_STRING,G_TYPE_STRING);GtkWidget *tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_widget_set_name(tree,"insight-recording-list");
    g_object_set_data(G_OBJECT(tree),"insight-no-i18n",GINT_TO_POINTER(1));const gchar *titles[]={"Step","Command","Text preview"};
    for(guint i=0;i<3;i++) gtk_tree_view_append_column(GTK_TREE_VIEW(tree),gtk_tree_view_column_new_with_attributes(insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",i,NULL));
    for(guint i=0;i<actions->len;i++) {
        RecordedAction *a=g_ptr_array_index(actions,i);gchar *excerpt=g_utf8_substring(a->text ? a->text:"",0,MIN(100,g_utf8_strlen(a->text ? a->text:"",-1)));g_strdelimit(excerpt,"\r\n\t",' ');
        GtkTreeIter iter;gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,i+1,1,message_name(a->message),2,excerpt,-1);g_free(excerpt);
    }
    GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,4);g_object_unref(store);
    gtk_widget_show_all(dialog);gtk_dialog_run(GTK_DIALOG(dialog));gtk_widget_destroy(dialog);g_object_unref(dialog);
}
void insight_recording_command(guint action)
{
    if(!actions || playing) return;
    if(action==0) {
        GeanyDocument *doc=document_get_current();if(!ordinary(doc)) return;
        discard();recording_document=doc->id;
        if(!g_object_get_data(G_OBJECT(doc->editor->sci),"insight-recorder-mouse")) {
            g_object_set_data(G_OBJECT(doc->editor->sci),"insight-recorder-mouse",GINT_TO_POINTER(1));
            g_signal_connect(doc->editor->sci,"button-press-event",G_CALLBACK(mouse_pressed),NULL);
        }
        SEND(doc->editor->sci,SCI_STARTRECORD,0,0);ui_set_statusbar(TRUE,"%s",insight_tr("Recording editor commands — Stop Recording finishes the sequence"));
    } else if(action==1) { stop();ui_set_statusbar(TRUE,"%s",insight_tr("Editor recording stopped")); }
    else if(action==2) play();else if(action==3) preview();else if(action==4) discard();
}
static void activate(GtkMenuItem *item,gpointer data) { (void)item;insight_recording_command(GPOINTER_TO_UINT(data)); }
static void shown(GtkWidget *menu,gpointer data)
{
    (void)menu;(void)data;gtk_widget_set_sensitive(items[0],!recording_document && ordinary(document_get_current()));
    gtk_widget_set_sensitive(items[1],recording_document!=0);gtk_widget_set_sensitive(items[2],actions->len && ordinary(document_get_current()));
    gtk_widget_set_sensitive(items[3],actions->len>0);gtk_widget_set_sensitive(items[4],actions->len || recording_document);
    syncing=TRUE;gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(remember_item),remember);syncing=FALSE;
}
void insight_recording_init(GtkWidget *edit_menu)
{
    if(actions) return;actions=g_ptr_array_new_with_free_func(action_free);
    global_recording=g_key_file_new();
    gchar *path=g_build_filename(app->configdir,"geany.conf",NULL);GKeyFile *key=g_key_file_new();g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL);read_recording(key,FALSE);g_key_file_unref(key);g_free(path);
    /* Startup projects are loaded before the workbench/menu is initialized. */
    if(app->project && app->project->file_name) {
        path=utils_get_locale_from_utf8(app->project->file_name);key=g_key_file_new();g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL);project_opened(NULL,key,NULL);g_key_file_unref(key);g_free(path);
    }
    g_signal_connect(geany_object,"save-settings",G_CALLBACK(settings_saved),NULL);
    g_signal_connect(geany_object,"project-open",G_CALLBACK(project_opened),NULL);
    g_signal_connect(geany_object,"project-save",G_CALLBACK(project_saved),NULL);
    g_signal_connect(geany_object,"project-close",G_CALLBACK(project_closed),NULL);
    g_signal_connect(geany_object,"document-activate",G_CALLBACK(changed_document),NULL);g_signal_connect(geany_object,"document-close",G_CALLBACK(closing_document),NULL);
    GtkWidget *parent=gtk_menu_item_new_with_label("Command Recording"),*menu=gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),menu);
    const gchar *labels[]={"Start Recording","Stop Recording","Play Recording","Recorded Editor Commands…","Clear Recording"};
    const gchar *names[]={"start","stop","play","preview","clear"};
    for(guint i=0;i<5;i++) {
        items[i]=gtk_menu_item_new_with_label(labels[i]);gchar *name=g_strconcat("insight-recording-",names[i],NULL);gtk_widget_set_name(items[i],name);g_free(name);
        keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_RECORDSTART+i,items[i]);g_signal_connect(items[i],"activate",G_CALLBACK(activate),GUINT_TO_POINTER(i));gtk_menu_shell_append(GTK_MENU_SHELL(menu),items[i]);
    }
    GtkWidget *help=gtk_menu_item_new_with_label("Command Recording Help");g_signal_connect(help,"activate",G_CALLBACK(insight_help_show),"command-recording.md");gtk_menu_shell_append(GTK_MENU_SHELL(menu),help);
    remember_item=gtk_check_menu_item_new_with_label("Remember Recording in Workspace");gtk_widget_set_name(remember_item,"insight-recording-remember");
    gtk_widget_set_tooltip_text(remember_item,"Saves literal recorded text on the next normal project/preferences save. Disable to remove saved steps on the next save. Never plays automatically.");
    gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(remember_item),remember);g_signal_connect(remember_item,"toggled",G_CALLBACK(remember_toggled),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(menu),remember_item);
    g_signal_connect(menu,"show",G_CALLBACK(shown),NULL);gtk_menu_shell_append(GTK_MENU_SHELL(edit_menu),parent);gtk_widget_show_all(parent);
}
