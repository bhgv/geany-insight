/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-custom-commands.h"
#include "app.h"
#include "document.h"
#include "project.h"
#include "main.h"
#include "keybindings.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "insight-compare-ui.h"
#include "insight-help.h"
#include "../insight/desktop/custom_command.h"
#include "../insight/desktop/project_files.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define OUTPUT_LIMIT (4u*1024u*1024u)
typedef struct {
 gint refs;gboolean closed,loading,reading,waiting,limited,stopped;
 GtkWidget *window,*tabs,*profiles,*name,*command,*directory,*run,*stop,*status,*context,*output,*title,*arguments[9];
 GPtrArray *commands,*locked;gchar *path,*status_key,*context_argv,*context_directory,*context_variables,*context_error;gint exit_code;
 GSubprocess *process;GCancellable *cancel;GString *pending;gsize received;guint lines,line_bytes,follow_idle;gdouble follow_baseline;
} CommandView;
static GtkWidget *command_window;
static CommandView *view_ref(CommandView *v) {v->refs++;return v;}
static void view_unref(gpointer data) {
 CommandView *v=data;if(--v->refs) return;g_ptr_array_unref(v->commands);g_ptr_array_unref(v->locked);g_free(v->path);g_free(v->status_key);g_free(v->context_argv);g_free(v->context_directory);g_free(v->context_variables);g_free(v->context_error);g_clear_object(&v->process);g_clear_object(&v->cancel);g_string_free(v->pending,TRUE);g_free(v);
}
static void note(CommandView *v,const gchar *key) {
 g_free(v->status_key);v->status_key=g_strdup(key);if(!v->closed) {gchar *formatted=!strcmp(key,"Command exited with status %d") ? g_strdup_printf(insight_tr(key),v->exit_code):g_strdup(insight_tr(key));gtk_label_set_text(GTK_LABEL(v->status),formatted);g_free(formatted);}
}
static void controls(CommandView *v) {
 if(v->closed) return;gboolean busy=v->process!=NULL;for(guint i=0;i<v->locked->len;i++) gtk_widget_set_sensitive(g_ptr_array_index(v->locked,i),!busy);
 gtk_widget_set_sensitive(v->run,!busy);gtk_widget_set_sensitive(v->stop,busy && !v->stopped);g_object_set_data(G_OBJECT(v->window),"insight-custom-command-busy",GINT_TO_POINTER(busy));
}
static void cancel_follow(CommandView *v) {if(v->follow_idle) {guint id=v->follow_idle;v->follow_idle=0;g_source_remove(id);}}
static void stop_scroll(GtkWidget *view) {GtkAdjustment *a=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(view));if(a) gtk_adjustment_set_value(a,gtk_adjustment_get_value(a));}
static void destroyed(GtkWidget *widget,gpointer data) {
 CommandView *v=data;if(command_window==widget) command_window=NULL;v->closed=TRUE;cancel_follow(v);if(v->process) g_subprocess_force_exit(v->process);if(v->cancel) g_cancellable_cancel(v->cancel);
}
static void value(GHashTable *map,const gchar *key,const gchar *text) {if(text && g_utf8_validate(text,-1,NULL) && strlen(text)<=16384 && !strpbrk(text,"\r\n")) g_hash_table_replace(map,g_strdup(key),g_strdup(text));}
static GHashTable *context_values(void) {
 GHashTable *map=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);GeanyDocument *doc=document_get_current();
 const gchar *file=DOC_VALID(doc) ? doc->file_name:NULL;gchar *dir=file ? g_path_get_dirname(file):g_strdup(g_get_home_dir());
 const gchar *project=app->project ? app->project->file_name:NULL;gchar *root=app->project ? project_get_base_path():g_strdup(dir);
 if(!root) root=g_strdup(dir);
 value(map,"j",root);value(map,"PROJECT_SOURCE_DIR",root);value(map,"d",dir);
 if(file) {gchar *name=g_path_get_basename(file),*stem=g_strdup(name),*dot=strrchr(stem,'.');value(map,"f",file);value(map,"n",name);if(dot && dot!=stem) {*dot=0;value(map,"e",dot+1);}else value(map,"e","");value(map,"b",stem);
  GFile *base=g_file_new_for_path(root),*source=g_file_new_for_path(file);gchar *relative=g_file_get_relative_path(base,source);value(map,"r",relative ? relative:file);g_free(relative);g_object_unref(base);g_object_unref(source);g_free(name);g_free(stem);}
 if(project) {gchar *pdir=g_path_get_dirname(project),*name=g_path_get_basename(project),*dot=strrchr(name,'.');if(dot && dot!=name) *dot=0;value(map,"J",pdir);value(map,"o",name);value(map,"PROJECT_DATA_DIR",pdir);value(map,"PROJECT_NAME",name);gchar *project_stem=g_build_filename(pdir,name,NULL);value(map,"p",project_stem);g_free(project_stem);g_free(name);g_free(pdir);}
 if(DOC_VALID(doc)) {ScintillaObject *s=doc->editor->sci;gint pos=SEND(s,SCI_GETCURRENTPOS,0,0),start=SEND(s,SCI_GETSELECTIONSTART,0,0),end=SEND(s,SCI_GETSELECTIONEND,0,0);gchar *line=g_strdup_printf("%d",sci_get_line_from_position(s,pos)+1);value(map,"l",line);g_free(line);
  if(start==end) {start=SEND(s,SCI_WORDSTARTPOSITION,pos,TRUE);end=SEND(s,SCI_WORDENDPOSITION,pos,TRUE);}if(end-start<=4096 && SEND(s,SCI_GETSELECTIONS,0,0)==1 && SEND(s,SCI_GETSELECTIONMODE,0,0)==SC_SEL_STREAM && !SEND(s,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0) && !SEND(s,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0)) {gchar *word=sci_get_contents_range(s,start,end);if(strlen(word)==(gsize)(end-start) && g_utf8_validate(word,-1,NULL)) {gchar *first=word;while(*first && g_unichar_isspace(g_utf8_get_char(first))) first=g_utf8_next_char(first);gchar *last=first;while(*last && !g_unichar_isspace(g_utf8_get_char(last))) last=g_utf8_next_char(last);*last=0;value(map,"w",first);}g_free(word);}}
 GDateTime *now=g_date_time_new_now_local();gchar *date=g_date_time_format(now,"%Y-%m-%d"),*time=g_date_time_format(now,"%H:%M:%S");value(map,"a",date);value(map,"t",time);g_free(date);g_free(time);g_date_time_unref(now);
 const gchar *names[]={"APPDATA_DIR","LOCAL_APPDATA_DIR","DESKTOP_DIR","MYDOCUMENTS_DIR","PROGRAM_DIR","TEMP_DIR","PROGRAMFILES_DIR","PROGRAMFILESX86_DIR","WINDOWS_DIR"};
 for(guint i=0;i<G_N_ELEMENTS(names);i++) {gchar *path=insight_project_path_variable(names[i],root,project,NULL);value(map,names[i],path);g_free(path);}
 g_free(root);g_free(dir);return map;
}
static gboolean prepare(CommandView *v,gchar ***argv,gchar **directory,GHashTable **context,GError **error) {
 *context=context_values();for(guint i=0;i<9;i++) {gchar key[2]={(gchar)('1'+i),0};value(*context,key,gtk_entry_get_text(GTK_ENTRY(v->arguments[i])));}
 *argv=insight_custom_argv(gtk_entry_get_text(GTK_ENTRY(v->command)),*context,error);if(!*argv) return FALSE;
 gchar *expanded=insight_custom_expand(gtk_entry_get_text(GTK_ENTRY(v->directory)),*context,error);if(!expanded) return FALSE;
 if(!*expanded) {g_free(expanded);expanded=g_strdup(g_hash_table_lookup(*context,"j"));}
 if(!expanded || !g_path_is_absolute(expanded)) {g_free(expanded);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Working directory must be an absolute path");return FALSE;}
 *directory=g_filename_from_utf8(expanded,-1,NULL,NULL,error);g_free(expanded);if(!*directory) return FALSE;
 if(!g_file_test(*directory,G_FILE_TEST_IS_DIR)) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_DIRECTORY,"Working directory does not exist");return FALSE;}return TRUE;
}
static gchar *display_argv(gchar **argv) {GString *s=g_string_new(NULL);for(guint i=0;argv && argv[i];i++) {gchar *q=g_shell_quote(argv[i]);if(i) g_string_append_c(s,' ');g_string_append(s,q);g_free(q);}if(s->len>8192) {const gchar *end=g_utf8_find_prev_char(s->str,s->str+8192);g_string_truncate(s,end ? (gsize)(end-s->str):0);g_string_append(s,"\n…");}return g_string_free(s,FALSE);}
/* Retain captured values so language changes only repaint headings, without
 * resampling another document, touching the filesystem or changing tabs. */
static void render_context(CommandView *v) {
 if(v->closed || !v->context_variables) return;
 GString *text=g_string_new(insight_tr("Expanded arguments (display only):"));g_string_append_c(text,'\n');
 g_string_append(text,v->context_argv ? v->context_argv:insight_tr(v->context_error ? v->context_error:"Invalid custom command template"));
 if(v->context_directory) g_string_append_printf(text,"\n\n%s\n%s",insight_tr("Working directory"),v->context_directory);
 g_string_append_printf(text,"\n\n%s\n%s",insight_tr("Available context variables:"),v->context_variables);
 stop_scroll(v->context);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->context)),text->str,-1);g_string_free(text,TRUE);
}
static void preview(GtkWidget *button,gpointer data) {
 (void)button;CommandView *v=data;gchar **argv=NULL,*directory=NULL;GHashTable *map=NULL;GError *error=NULL;gboolean ok=prepare(v,&argv,&directory,&map,&error);
 g_free(v->context_argv);g_free(v->context_directory);g_free(v->context_variables);g_free(v->context_error);
 v->context_argv=ok ? display_argv(argv):NULL;v->context_directory=ok ? g_filename_display_name(directory):NULL;v->context_error=ok ? NULL:g_strdup(error ? error->message:"Invalid custom command template");
 GString *variables=g_string_new(NULL);GList *keys=g_hash_table_get_keys(map);keys=g_list_sort(keys,(GCompareFunc)g_strcmp0);
 for(GList *p=keys;p;p=p->next) {const gchar *key=p->data;g_string_append_printf(variables,strlen(key)==1 ? "%%%s = %s\n":"%%%s%% = %s\n",key,(gchar *)g_hash_table_lookup(map,key));}g_list_free(keys);v->context_variables=g_string_free(variables,FALSE);
 render_context(v);insight_compare_ui_reveal(v->context);g_hash_table_unref(map);g_strfreev(argv);g_free(directory);g_clear_error(&error);
}
static gboolean follow_output(gpointer data) {
 CommandView *v=data;v->follow_idle=0;if(v->closed) return G_SOURCE_REMOVE;
 GtkAdjustment *a=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v->output));
 /* Preserve a manual scroll made since this follow request was queued. */
 if(ABS(gtk_adjustment_get_value(a)-v->follow_baseline)<1)
  gtk_adjustment_set_value(a,MAX(gtk_adjustment_get_lower(a),gtk_adjustment_get_upper(a)-gtk_adjustment_get_page_size(a)));
 return G_SOURCE_REMOVE;
}
static void append(CommandView *v,const gchar *text,gint length) {
 if(v->closed) return;GtkAdjustment *a=gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(v->output));
 gboolean follow=gtk_adjustment_get_value(a)+gtk_adjustment_get_page_size(a)>=gtk_adjustment_get_upper(a)-16;
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output));GtkTextIter end;gtk_text_buffer_get_end_iter(buffer,&end);gtk_text_buffer_insert(buffer,&end,text,length);
 /* Coalesce after GTK layout, without animated scroll-to-mark requests. GTK 3
  * animation teardown can reenter adjustment callbacks while unmapping after
  * replacing a large log. Direct updates have no pending animation to finish. */
 if(follow && !v->follow_idle) {v->follow_baseline=gtk_adjustment_get_value(a);v->follow_idle=g_idle_add_full(G_PRIORITY_LOW,follow_output,view_ref(v),view_unref);}
}
/* Keep incomplete UTF-8 across pipe reads, and display embedded NUL safely. */
static void decode(CommandView *v,const guint8 *bytes,gsize size,gboolean eof) {
 g_string_append_len(v->pending,(const gchar *)bytes,size);GString *text=g_string_new(NULL);gsize i=0;
 while(i<v->pending->len) {const gchar *p=v->pending->str+i;if(!*p) {g_string_append(text,"␀");i++;continue;}gunichar c=g_utf8_get_char_validated(p,v->pending->len-i);if(c==(gunichar)-2 && !eof) break;if(c==(gunichar)-1 || c==(gunichar)-2) {g_string_append(text,"�");i++;}else if(c==0) {g_string_append(text,"␀");i++;}else {gsize n=g_utf8_next_char(p)-p;g_string_append_len(text,p,n);i+=n;}}
 g_string_erase(v->pending,0,i);append(v,text->str,text->len);g_string_free(text,TRUE);
}
static void finished(CommandView *v) {
 if(v->reading || v->waiting) return;g_clear_object(&v->process);g_clear_object(&v->cancel);if(!v->closed) {if(v->limited) note(v,"Output limit reached; command stopped");else if(v->stopped) note(v,"Command stopped");else note(v,"Command exited with status %d");controls(v);}
}
static void waited(GObject *process,GAsyncResult *result,gpointer data) {
 CommandView *v=data;GError *error=NULL;gboolean ok=g_subprocess_wait_finish(G_SUBPROCESS(process),result,&error);v->exit_code=ok && g_subprocess_get_if_exited(G_SUBPROCESS(process)) ? g_subprocess_get_exit_status(G_SUBPROCESS(process)):-1;g_clear_error(&error);v->waiting=FALSE;finished(v);view_unref(v);
}
static void read_ready(GObject *stream,GAsyncResult *result,gpointer data) {
 CommandView *v=data;GError *error=NULL;GBytes *bytes=g_input_stream_read_bytes_finish(G_INPUT_STREAM(stream),result,&error);gsize size=0;const guint8 *p=bytes ? g_bytes_get_data(bytes,&size):NULL;
 if(!v->closed && !v->stopped && size) {gsize keep=MIN(size,OUTPUT_LIMIT-v->received);for(gsize i=0;i<keep;i++) {if(p[i]=='\n') {v->line_bytes=0;v->lines++;}else v->line_bytes++;if(v->lines>=50000 || v->line_bytes>=65536) {keep=i+1;v->limited=TRUE;break;}}decode(v,p,keep,FALSE);v->received+=keep;if(keep<size || v->limited) {v->limited=v->stopped=TRUE;g_subprocess_force_exit(v->process);g_cancellable_cancel(v->cancel);}}
 if(bytes) g_bytes_unref(bytes);
 if(size && !v->closed && !v->stopped && !error) {g_input_stream_read_bytes_async(G_INPUT_STREAM(stream),8192,G_PRIORITY_DEFAULT,v->cancel,read_ready,v);return;}
 if(error && !g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED) && v->process) {v->stopped=TRUE;g_subprocess_force_exit(v->process);}
 if(!v->closed) decode(v,(const guint8 *)"",0,TRUE);g_clear_error(&error);v->reading=FALSE;finished(v);view_unref(v);
}
static void run(GtkWidget *button,gpointer data) {
 (void)button;CommandView *v=data;if(v->process) return;gchar **argv=NULL,*directory=NULL;GHashTable *map=NULL;GError *error=NULL;
 if(!prepare(v,&argv,&directory,&map,&error)) {note(v,error ? error->message:"Invalid custom command template");goto done;}
 GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE);g_subprocess_launcher_set_cwd(launcher,directory);v->process=g_subprocess_launcher_spawnv(launcher,(const gchar *const *)argv,&error);g_object_unref(launcher);
 if(!v->process) {note(v,error->message);goto done;}
 cancel_follow(v);stop_scroll(v->output);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output)),"",0);gchar *shown=display_argv(argv);append(v,shown,-1);append(v,"\n",1);g_free(shown);v->received=0;v->lines=v->line_bytes=0;v->limited=v->stopped=FALSE;g_string_truncate(v->pending,0);v->cancel=g_cancellable_new();v->reading=v->waiting=TRUE;note(v,"Command running");controls(v);
 g_subprocess_wait_async(v->process,NULL,waited,view_ref(v));g_input_stream_read_bytes_async(g_subprocess_get_stdout_pipe(v->process),8192,G_PRIORITY_DEFAULT,v->cancel,read_ready,view_ref(v));
done:g_clear_error(&error);g_strfreev(argv);g_free(directory);if(map) g_hash_table_unref(map);
}
static void stop(GtkWidget *button,gpointer data) {(void)button;CommandView *v=data;if(v->process && !v->stopped) {v->stopped=TRUE;g_subprocess_force_exit(v->process);g_cancellable_cancel(v->cancel);note(v,"Stopping command");controls(v);}}
static void select_profile(GtkComboBox *box,gpointer data) {CommandView *v=data;if(v->loading) return;gint at=gtk_combo_box_get_active(box);if(at<0 || at>=(gint)v->commands->len) return;InsightCustomCommand *c=g_ptr_array_index(v->commands,at);gtk_entry_set_text(GTK_ENTRY(v->name),c->name);gtk_entry_set_text(GTK_ENTRY(v->command),c->command);gtk_entry_set_text(GTK_ENTRY(v->directory),c->directory);}
static GPtrArray *clone_commands(CommandView *v) {GPtrArray *list=g_ptr_array_new_with_free_func(insight_custom_command_free);for(guint i=0;i<v->commands->len;i++) {InsightCustomCommand *old=g_ptr_array_index(v->commands,i),*c=g_new0(InsightCustomCommand,1);c->name=g_strdup(old->name);c->command=g_strdup(old->command);c->directory=g_strdup(old->directory);g_ptr_array_add(list,c);}return list;}
static void rebuild(CommandView *v,gint at) {v->loading=TRUE;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(v->profiles));for(guint i=0;i<v->commands->len;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(v->profiles),((InsightCustomCommand *)g_ptr_array_index(v->commands,i))->name);gtk_combo_box_set_active(GTK_COMBO_BOX(v->profiles),at);v->loading=FALSE;if(at>=0) select_profile(GTK_COMBO_BOX(v->profiles),v);}
static gboolean persist(CommandView *v,GPtrArray *list,gint at) {
 GError *error=NULL;gchar *dir=g_path_get_dirname(v->path);gint made=g_mkdir_with_parents(dir,0700);g_free(dir);if(made || !insight_custom_commands_save(v->path,list,&error)) {note(v,error ? error->message:"Cannot create custom command settings directory");g_clear_error(&error);g_ptr_array_unref(list);return FALSE;}
 g_ptr_array_unref(v->commands);v->commands=list;rebuild(v,at);note(v,"Custom commands saved");return TRUE;
}
static void edit_profile(GtkWidget *button,gpointer data) {
 CommandView *v=data;if(v->process) return;gint action=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"custom-action")),at=gtk_combo_box_get_active(GTK_COMBO_BOX(v->profiles));
 if(action==0) {gtk_combo_box_set_active(GTK_COMBO_BOX(v->profiles),-1);gtk_entry_set_text(GTK_ENTRY(v->name),"");gtk_entry_set_text(GTK_ENTRY(v->command),"");gtk_entry_set_text(GTK_ENTRY(v->directory),"%j");gtk_widget_grab_focus(v->name);return;}
 GPtrArray *list=clone_commands(v);
 if(action==1) {InsightCustomCommand *c=g_new0(InsightCustomCommand,1);c->name=g_strdup(gtk_entry_get_text(GTK_ENTRY(v->name)));c->command=g_strdup(gtk_entry_get_text(GTK_ENTRY(v->command)));c->directory=g_strdup(gtk_entry_get_text(GTK_ENTRY(v->directory)));if(at>=0) {insight_custom_command_free(g_ptr_array_index(list,at));g_ptr_array_index(list,at)=c;}else {at=list->len;g_ptr_array_add(list,c);}}
 else if(at<0) {g_ptr_array_unref(list);return;}
 else if(action==2) {g_ptr_array_remove_index(list,at);at=MIN(at,(gint)list->len-1);}
 else {gint next=at+(action==3 ? -1:1);if(next<0 || next>=(gint)list->len) {g_ptr_array_unref(list);return;}gpointer tmp=g_ptr_array_index(list,at);g_ptr_array_index(list,at)=g_ptr_array_index(list,next);g_ptr_array_index(list,next)=tmp;at=next;}
 if(persist(v,list,at) && at<0) {gtk_entry_set_text(GTK_ENTRY(v->name),"");gtk_entry_set_text(GTK_ENTRY(v->command),"");}
}
static void output_action(GtkWidget *button,gpointer data) {
 CommandView *v=data;gint action=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(button),"output-action"));GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(v->output));
 if(action==0) {cancel_follow(v);stop_scroll(v->output);gtk_text_buffer_set_text(buffer,"",0);return;}GtkTextIter a,b;gtk_text_buffer_get_bounds(buffer,&a,&b);gchar *text=gtk_text_buffer_get_text(buffer,&a,&b,FALSE);
 if(action==1) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),text,-1);
 else {view_ref(v);GtkWidget *dialog=gtk_file_chooser_dialog_new(insight_tr("Save command output"),GTK_WINDOW(v->window),GTK_FILE_CHOOSER_ACTION_SAVE,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Save"),GTK_RESPONSE_ACCEPT,NULL);g_object_ref_sink(dialog);gtk_window_set_destroy_with_parent(GTK_WINDOW(dialog),TRUE);gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),TRUE);gint response=gtk_dialog_run(GTK_DIALOG(dialog));if(!v->closed && response==GTK_RESPONSE_ACCEPT) {gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));GError *error=NULL;GFile *f=g_file_new_for_path(path);if(!g_file_replace_contents(f,text,strlen(text),NULL,FALSE,G_FILE_CREATE_NONE,NULL,NULL,&error)) {note(v,error->message);g_clear_error(&error);}g_object_unref(f);g_free(path);}gtk_widget_destroy(dialog);g_object_unref(dialog);view_unref(v);}g_free(text);
}
static GtkWidget *button(GtkWidget *row,const gchar *label,const gchar *name,GCallback action,CommandView *v,gboolean lock) {GtkWidget *b=gtk_button_new_with_label("");gtk_widget_set_name(b,name);insight_i18n_key(G_OBJECT(b),"label",label);gtk_box_pack_start(GTK_BOX(row),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",action,v);if(lock) g_ptr_array_add(v->locked,b);return b;}
static GtkWidget *entry(GtkWidget *page,const gchar *label,const gchar *name,const gchar *tip,CommandView *v) {GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,6),*l=gtk_label_new(NULL),*e=gtk_entry_new();insight_i18n_key(G_OBJECT(l),"label",label);gtk_widget_set_name(e,name);insight_ai_ui_entry(e,tip,1);insight_i18n_key(G_OBJECT(e),"tooltip-text",tip);gtk_box_pack_start(GTK_BOX(row),l,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),e,TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(page),row,FALSE,FALSE,0);g_ptr_array_add(v->locked,e);return e;}
static GtkWidget *text_view(GtkWidget *page,const gchar *name,gboolean expand) {GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL),*view=gtk_text_view_new();gtk_widget_set_name(view,name);gtk_text_view_set_editable(GTK_TEXT_VIEW(view),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(view),TRUE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view),GTK_WRAP_WORD_CHAR);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(page),scroll,expand,expand,0);return view;}
static void locale(gpointer data) {CommandView *v=data;if(!v->closed) {render_context(v);insight_i18n_key(G_OBJECT(v->window),"title","Custom Commands");if(v->title) insight_i18n_key(G_OBJECT(v->title),"label","Custom Commands");}if(!v->closed && v->status_key) {gchar *key=g_strdup(v->status_key);note(v,key);g_free(key);}}
static void show(GtkMenuItem *item,gpointer unused) {
 (void)item;(void)unused;if(command_window) {gtk_window_present(GTK_WINDOW(command_window));return;}
 CommandView *v=g_new0(CommandView,1);v->refs=1;v->locked=g_ptr_array_new();v->pending=g_string_new(NULL);v->path=g_build_filename(app->configdir,"insight","custom-commands.conf",NULL);GError *error=NULL;v->commands=insight_custom_commands_load(v->path,&error);if(!v->commands) v->commands=g_ptr_array_new_with_free_func(insight_custom_command_free);
 v->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);command_window=v->window;gtk_widget_set_name(v->window,"insight-custom-commands");gtk_window_set_transient_for(GTK_WINDOW(v->window),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(v->window),TRUE);insight_i18n_key(G_OBJECT(v->window),"title","Custom Commands");g_object_set_data_full(G_OBJECT(v->window),"insight-custom-state",v,view_unref);g_signal_connect(v->window,"destroy",G_CALLBACK(destroyed),v);
 GtkWidget *root=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_container_add(GTK_CONTAINER(v->window),root);v->tabs=insight_compare_ui_tabs(root,"insight-custom-command-tabs");GtkWidget *commands=insight_compare_ui_page(v->tabs,"Commands"),*context=insight_compare_ui_page(v->tabs,"Context"),*output=insight_compare_ui_page(v->tabs,"Output"),*arguments=insight_compare_ui_page(v->tabs,"Arguments");
 GtkWidget *arg_grid=gtk_grid_new();gtk_grid_set_column_spacing(GTK_GRID(arg_grid),6);gtk_grid_set_row_spacing(GTK_GRID(arg_grid),3);gtk_box_pack_start(GTK_BOX(arguments),arg_grid,FALSE,FALSE,0);
 for(guint i=0;i<9;i++) {gchar *label=g_strdup_printf("%%%u",i+1),*name=g_strdup_printf("insight-custom-command-argument-%u",i+1);GtkWidget *l=gtk_label_new(label);v->arguments[i]=gtk_entry_new();gtk_widget_set_name(v->arguments[i],name);insight_ai_ui_entry(v->arguments[i],"Value substituted for this numbered command argument",1);insight_i18n_key(G_OBJECT(v->arguments[i]),"tooltip-text","Value substituted for this numbered command argument");gtk_grid_attach(GTK_GRID(arg_grid),l,(i%3)*2,i/3,1,1);gtk_grid_attach(GTK_GRID(arg_grid),v->arguments[i],(i%3)*2+1,i/3,1,1);g_ptr_array_add(v->locked,v->arguments[i]);g_free(label);g_free(name);}
 v->profiles=gtk_combo_box_text_new();gtk_widget_set_name(v->profiles,"insight-custom-command-profiles");gtk_box_pack_start(GTK_BOX(commands),v->profiles,FALSE,FALSE,0);g_ptr_array_add(v->locked,v->profiles);g_signal_connect(v->profiles,"changed",G_CALLBACK(select_profile),v);
 v->name=entry(commands,"Name","insight-custom-command-name","Name of the saved external tool",v);v->command=entry(commands,"Command","insight-custom-command-template","Executable and quoted arguments; context variables expand inside each argument",v);v->directory=entry(commands,"Working directory","insight-custom-command-directory","Absolute working directory; empty uses the current project or file directory",v);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(commands),row,FALSE,FALSE,0);const gchar *labels[]={"New command","Save command","Delete command","Move up","Move down"};for(guint i=0;i<5;i++) {gchar *name=g_strdup_printf("insight-custom-command-edit-%u",i);GtkWidget *b=button(row,labels[i],name,G_CALLBACK(edit_profile),v,TRUE);g_object_set_data(G_OBJECT(b),"custom-action",GINT_TO_POINTER(i));g_free(name);}
 row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(commands),row,FALSE,FALSE,0);v->run=button(row,"Run","insight-custom-command-run",G_CALLBACK(run),v,FALSE);v->stop=button(row,"Stop","insight-custom-command-stop",G_CALLBACK(stop),v,FALSE);button(row,"Preview command","insight-custom-command-preview",G_CALLBACK(preview),v,FALSE);
 GtkWidget *refresh=gtk_button_new_with_label("");insight_i18n_key(G_OBJECT(refresh),"label","Refresh context");gtk_box_pack_start(GTK_BOX(context),refresh,FALSE,FALSE,0);g_signal_connect(refresh,"clicked",G_CALLBACK(preview),v);v->context=text_view(context,"insight-custom-command-context",FALSE);GtkWidget *sc=gtk_widget_get_parent(v->context);gtk_widget_set_size_request(sc,-1,180);
 row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(output),row,FALSE,FALSE,0);const gchar *outs[]={"Clear output","Copy output","Save command output"};for(guint i=0;i<3;i++) {gchar *name=g_strdup_printf("insight-custom-command-output-%u",i);GtkWidget *b=button(row,outs[i],name,G_CALLBACK(output_action),v,FALSE);g_object_set_data(G_OBJECT(b),"output-action",GINT_TO_POINTER(i));g_free(name);}
 v->status=gtk_label_new(NULL);gtk_label_set_xalign(GTK_LABEL(v->status),0);gtk_label_set_selectable(GTK_LABEL(v->status),TRUE);gtk_label_set_ellipsize(GTK_LABEL(v->status),PANGO_ELLIPSIZE_END);gtk_box_pack_start(GTK_BOX(root),v->status,FALSE,FALSE,0);v->output=text_view(root,"insight-custom-command-output",TRUE);insight_panel_help_notebook(GTK_NOTEBOOK(v->tabs),"custom-commands.md");
 rebuild(v,v->commands->len ? 0:-1);if(!v->commands->len) gtk_entry_set_text(GTK_ENTRY(v->directory),"%j");note(v,error ? error->message:"Ready to run an external tool");g_clear_error(&error);insight_i18n_on_refresh(G_OBJECT(v->window),locale,v);controls(v);insight_ai_ui_window(v->window,"Minimize window","Maximize / restore window","Close window");GtkWidget *header=gtk_window_get_titlebar(GTK_WINDOW(v->window));if(GTK_IS_HEADER_BAR(header)) {v->title=gtk_label_new(NULL);gtk_widget_set_name(v->title,"insight-custom-command-title");gtk_style_context_add_class(gtk_widget_get_style_context(v->title),"title");gtk_label_set_ellipsize(GTK_LABEL(v->title),PANGO_ELLIPSIZE_END);gtk_label_set_width_chars(GTK_LABEL(v->title),1);insight_i18n_key(G_OBJECT(v->title),"label","Custom Commands");gtk_header_bar_set_custom_title(GTK_HEADER_BAR(header),v->title);}gtk_widget_show_all(v->window);
}
void insight_custom_commands_show(void) {show(NULL,NULL);}
void insight_custom_commands_menu(GtkWidget *menu) {GtkWidget *item=gtk_menu_item_new_with_label("");gtk_widget_set_name(item,"insight-custom-commands-menu");insight_i18n_key(G_OBJECT(item),"label","Custom Commands…");gtk_menu_shell_append(GTK_MENU_SHELL(menu),item);g_signal_connect(item,"activate",G_CALLBACK(show),NULL);keybindings_attach_menu(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_CUSTOMCOMMANDS,item);gtk_widget_show(item);}
