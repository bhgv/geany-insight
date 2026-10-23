/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "../insight/desktop/glib_compat.h"
#include "insight-remote.h"
#include "insight-ai.h"
#include "insight-help.h"
#include "document.h"
#include "project.h"
#include "ui_utils.h"
#include "utils.h"
#include "msgwindow.h"
#include "geanyobject.h"
#ifdef G_OS_WIN32
#include "win32.h"
#endif
#include "../insight/desktop/remote_io.h"
#include "../insight/desktop/remote_credentials.h"
#include <glib/gstdio.h>
#include <string.h>
typedef struct { InsightRemote *remote; gchar *directory,*config; gboolean auto_upload; } RemoteRoot;
typedef enum { OPEN_FILE,OPEN_FOLDER,PROJECT_READ,PROJECT_FOLDER,UPLOAD } TransferKind;
typedef struct { RemoteRoot *root; TransferKind kind; gchar *target,*snapshot,*auth,*origin; GKeyFile *project; GSubprocess *process; GCancellable *cancel; } Transfer;
typedef struct _ScpJob ScpJob;
typedef struct {
 gint refs;guint number;GtkWidget *page,*output,*input,*overlay,*ai_button,*stack,*chat;
 GSubprocess *process;GCancellable *cancel;gchar *auth,*endpoint;GQueue pending;gboolean writing,carriage,dragging,moved;guint ansi;
 GString *tail;gdouble x,y,press_x,press_y,start_x,start_y;
} ShellSession;
static ShellSession *session_ref(ShellSession *s) { g_atomic_int_inc(&s->refs);return s; }
static void session_unref(gpointer data) { ShellSession *s=data;if(!g_atomic_int_dec_and_test(&s->refs)) return;if(s->chat) g_object_remove_weak_pointer(G_OBJECT(s->chat),(gpointer *)&s->chat);g_clear_object(&s->page);g_string_free(s->tail,TRUE);g_free(s->endpoint);g_free(s); }
static struct {
 GtkWidget *page,*host,*user,*port,*identity,*path,*status,*output,*input,*save_password,*automatic;
 GPtrArray *roots; GHashTable *upload_versions; GCancellable *snapshot_cancel; GQueue pending;Transfer *active;ScpJob *scp;GPtrArray *sessions;ShellSession *selected;GtkWidget *tabs,*connection,*files,*transfer_local,*transfer_remote,*transfer_mode,*transfer_folder,*transfer_status;
 gchar *config,*directory;gboolean closing,loading;
 gulong signals[4]; gboolean (*workspace)(const char *);
} remote_ui;
static guint64 remote_epoch,upload_sequence;
static void profile_save(void);
static void transfer_run(Transfer *);
static void scp_cancel(void);
static void root_free(gpointer data) { RemoteRoot *r=data; if(!r) return; insight_remote_free(r->remote); g_free(r->directory); g_free(r->config); g_free(r); }
static RemoteRoot *root_copy(const RemoteRoot *r) { RemoteRoot *copy=g_new0(RemoteRoot,1); const InsightRemote *s=r->remote; copy->remote=insight_remote_new(s->host,s->user,s->port,s->identity,s->remote,s->local,NULL); copy->directory=g_strdup(r->directory); copy->config=g_strdup(r->config); copy->auto_upload=r->auto_upload; return copy; }
static void auth_cleanup(const char *path) { if(!path) return; GDir *dir=g_dir_open(path,0,NULL); if(dir) { const char *name; while((name=g_dir_read_name(dir))) { gchar *file=g_build_filename(path,name,NULL); g_remove(file); g_free(file); } g_dir_close(dir); } g_rmdir(path); }
static void transfer_free(Transfer *t) { if(!t) return; root_free(t->root); if(t->snapshot) g_remove(t->snapshot); auth_cleanup(t->auth); g_free(t->auth); g_free(t->origin); g_free(t->target); g_free(t->snapshot); if(t->project) g_key_file_unref(t->project); g_clear_object(&t->process); g_clear_object(&t->cancel); g_free(t); }
static void status(const char *message) {
 if(remote_ui.closing) return;
 gchar *short_message=g_utf8_strlen(message,-1)>180 ? g_utf8_substring(message,0,180) : NULL;
 gchar *label=short_message ? g_strconcat(short_message,"…",NULL) : g_strdup(message);
 gtk_label_set_text(GTK_LABEL(remote_ui.status),label);gtk_widget_set_tooltip_text(remote_ui.status,message);g_free(label);g_free(short_message);ui_set_statusbar(TRUE,"SSH: %s",message);
 /* Keep a selectable, persistent explanation even in a narrow panel. */
 if(remote_ui.output) { GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(remote_ui.output));GtkTextIter end;gtk_text_buffer_get_end_iter(buffer,&end);gtk_text_buffer_insert(buffer,&end,"\n[SSH] ",-1);gtk_text_buffer_insert(buffer,&end,message,-1);gtk_text_buffer_insert(buffer,&end,"\n",1);gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(remote_ui.output),&end,0,FALSE,0,0); }
}
static void session_status(ShellSession *s,const gchar *message) {
 if(remote_ui.closing) return;
 if(!s) { status(message);return; }
 if(s==remote_ui.selected) { status(message);return; }
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->output));GtkTextIter end;gtk_text_buffer_get_end_iter(b,&end);gtk_text_buffer_insert(b,&end,"\n[SSH] ",-1);gtk_text_buffer_insert(b,&end,message,-1);gtk_text_buffer_insert(b,&end,"\n",1);
}
static gchar *client_program(const gchar *program) {
 gchar *bin=NULL;
#ifdef G_OS_WIN32
 gchar *prefix=win32_get_installation_dir();bin=g_build_filename(prefix,"bin",NULL);g_free(prefix);
#else
 gchar *executable=g_file_read_link("/proc/self/exe",NULL);if(executable) { bin=g_path_get_dirname(executable);g_free(executable); }
#endif
 gchar *path=insight_remote_program(program,bin);g_free(bin);return path;
}
static gchar *credentials_target(const InsightRemote *r) { gchar *identity=g_strdup_printf("%s:%u:%s",r->host,r->port,r->user),*hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,identity,-1); g_free(identity); return hash; }
static GSubprocessLauncher *launcher(RemoteRoot *root,gchar **auth) {
 GSubprocessLauncher *spawn=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE);
#ifdef G_OS_WIN32
 gchar *installation=win32_get_installation_dir(),*client_bin=g_build_filename(installation,"bin",NULL),*search_path=g_strconcat(client_bin,";",g_getenv("PATH") ? g_getenv("PATH") : "",NULL);
 g_subprocess_launcher_setenv(spawn,"PATH",search_path,TRUE); g_free(search_path); g_free(client_bin); g_free(installation);
#endif
 gchar *helper=client_program("geany-insight-askpass");
 if(helper) { g_subprocess_launcher_setenv(spawn,"SSH_ASKPASS",helper,TRUE);g_subprocess_launcher_setenv(spawn,"SSH_ASKPASS_REQUIRE","force",TRUE); }
 g_object_set_data(G_OBJECT(spawn),"insight-ssh-askpass",GINT_TO_POINTER(helper!=NULL));
 gchar *uuid=g_uuid_string_random(); *auth=g_build_filename(remote_ui.directory,uuid,NULL); g_mkdir_with_parents(*auth,0700); g_free(uuid);
 gchar *target=credentials_target(root->remote); g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_CONFIG",root->config,TRUE); g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_TARGET",target,TRUE); g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_AUTH_SESSION",*auth,TRUE);
 g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_SAVE_PASSWORD",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password)) ? "1" : "0",TRUE); g_free(target); g_free(helper); return spawn;
}
static GSubprocess *spawn_client(GSubprocessLauncher *spawn,RemoteRoot *root,gboolean sftp,GError **error) {
 const gchar *name=sftp ? "sftp" : "ssh";gchar *client=client_program(name);
 if(!client) { g_set_error(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"OpenSSH client '%s' was not found in the application bin directory or PATH. Install OpenSSH or use a package containing its clients.",name);return NULL; }
 gchar **argv=insight_remote_argv(root->remote,sftp);g_free(argv[0]);argv[0]=client;
 /* Without a GUI helper there must be no invisible /dev/tty password read. */
 if(!g_object_get_data(G_OBJECT(spawn),"insight-ssh-askpass")) { g_free(argv[1]);argv[1]=g_strdup("-oBatchMode=yes"); }
 GSubprocess *process=g_subprocess_launcher_spawnv(spawn,(const gchar *const *)argv,error);g_strfreev(argv);return process;
}
static void root_metadata(RemoteRoot *root) {
 GKeyFile *key=g_key_file_new(); InsightRemote *r=root->remote;
 g_key_file_set_string(key,"ssh","host",r->host); g_key_file_set_string(key,"ssh","user",r->user); g_key_file_set_integer(key,"ssh","port",(gint)r->port); g_key_file_set_string(key,"ssh","identity",r->identity); g_key_file_set_string(key,"ssh","remote",r->remote); g_key_file_set_string(key,"ssh","local",r->local); g_key_file_set_string(key,"ssh","config",root->config); g_key_file_set_boolean(key,"ssh","auto-upload",root->auto_upload);
 gsize length; gchar *data=g_key_file_to_data(key,&length,NULL),*path=g_build_filename(root->directory,"connection.conf",NULL); GFile *file=g_file_new_for_path(path); g_file_replace_contents(file,data,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,NULL); g_object_unref(file); g_free(path); g_free(data); g_key_file_unref(key);
}
static void root_register(RemoteRoot *root) { root_metadata(root); g_ptr_array_add(remote_ui.roots,root); }
static void restore_roots(void) {
 GDir *dir=g_dir_open(remote_ui.directory,0,NULL); if(!dir) return; const gchar *name;
 while((name=g_dir_read_name(dir)) && remote_ui.roots->len<128) {
  gchar *directory=g_build_filename(remote_ui.directory,name,NULL),*path=g_build_filename(directory,"connection.conf",NULL); GKeyFile *key=g_key_file_new();
  if(g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL)) {
   gchar *host=g_key_file_get_string(key,"ssh","host",NULL),*user=g_key_file_get_string(key,"ssh","user",NULL),*identity=g_key_file_get_string(key,"ssh","identity",NULL),*server=g_key_file_get_string(key,"ssh","remote",NULL),*local=g_key_file_get_string(key,"ssh","local",NULL),*config=g_key_file_get_string(key,"ssh","config",NULL); gint port=g_key_file_get_integer(key,"ssh","port",NULL);
   InsightRemote *r=local && config && g_file_test(local,G_FILE_TEST_IS_DIR) ? insight_remote_new(host,user,(guint)port,identity,server,local,NULL) : NULL;
   if(r) { RemoteRoot *root=g_new0(RemoteRoot,1); root->remote=r; root->directory=g_strdup(directory); root->config=g_strdup(config); root->auto_upload=g_key_file_get_boolean(key,"ssh","auto-upload",NULL); g_ptr_array_add(remote_ui.roots,root); }
   g_free(host); g_free(user); g_free(identity); g_free(server); g_free(local); g_free(config);
  }
  g_key_file_unref(key); g_free(path); g_free(directory);
 }
 g_dir_close(dir);
}
static void next_transfer(void) { remote_ui.active=NULL; if(!remote_ui.closing && !g_queue_is_empty(&remote_ui.pending)) transfer_run(g_queue_pop_head(&remote_ui.pending)); }
static void transfer_done(GObject *object,GAsyncResult *result,gpointer data) {
 Transfer *t=data; GError *error=NULL; gchar *output=NULL;
 gboolean ok=g_subprocess_communicate_utf8_finish(G_SUBPROCESS(object),result,&output,NULL,&error) && g_subprocess_get_successful(t->process);
 if(remote_ui.closing) { g_clear_error(&error); g_free(output); transfer_free(t); return; }
 if(!ok) { gchar *message=g_strdup_printf("Transfer failed; local edits retained: %.1200s",error ? error->message : (output && *output ? output : "OpenSSH exited unsuccessfully")); status(message); g_free(message); }
 else if(t->kind==PROJECT_READ) {
  t->project=g_key_file_new();
  if(!g_key_file_load_from_file(t->project,t->target,G_KEY_FILE_KEEP_COMMENTS,NULL) || !g_key_file_has_group(t->project,"project")) status("Remote file is not a Geany project");
  else {
   gchar *base=g_key_file_get_string(t->project,"project","base_path",NULL),*normalized=insight_remote_normalize(t->root->remote->remote,base && *base ? base : "."); g_free(base); g_free(t->root->remote->remote); t->root->remote->remote=normalized;
   t->kind=PROJECT_FOLDER; g_clear_object(&t->process); g_clear_object(&t->cancel); auth_cleanup(t->auth); g_clear_pointer(&t->auth,g_free); g_clear_error(&error); g_free(output); transfer_run(t); return;
  }
 } else if(t->kind==UPLOAD) { gchar *message=g_strdup_printf("Uploaded %s:%s",t->root->remote->host,t->target); status(message); g_free(message); }
 else {
  RemoteRoot *root=t->root; t->root=NULL;
  if(t->kind==PROJECT_FOLDER) {
   g_key_file_set_string(t->project,"project","base_path","data"); g_key_file_remove_group(t->project,"files",NULL);
   g_key_file_set_string(t->project,"insight-ssh","host",root->remote->host); g_key_file_set_string(t->project,"insight-ssh","user",root->remote->user); g_key_file_set_integer(t->project,"insight-ssh","port",(gint)root->remote->port); g_key_file_set_string(t->project,"insight-ssh","remote-path",t->origin); g_key_file_set_string(t->project,"insight-ssh","remote-base-path",root->remote->remote); g_key_file_set_boolean(t->project,"insight-ssh","auto-upload",root->auto_upload); g_key_file_set_boolean(t->project,"insight-ssh","save-password",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password)));
   gsize length; gchar *contents=g_key_file_to_data(t->project,&length,NULL); gboolean written=g_file_set_contents(t->target,contents,(gssize)length,NULL); g_free(contents);
   gchar *key=credentials_target(root->remote),*password=insight_remote_password_load(root->config,key,NULL);
   if(password && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password))) insight_remote_password_save(t->target,key,password,NULL);
   g_free(password); g_free(key);
   g_free(root->config); root->config=g_strdup(t->target); root_register(root);
   if(written && project_ask_close() && document_close_all() && project_load_file(t->target)) status("Remote project opened; source files use the SSH cache"); else status("Project cached; opening cancelled or project file could not be written");
  } else { root_register(root); if(t->kind==OPEN_FILE) { if(document_open_file(t->target,FALSE,NULL,NULL)) status("Remote file opened; Save remote uploads, or enable Auto upload"); else status("File downloaded but editor could not open it"); } else { if(remote_ui.workspace(root->remote->local)) status("Remote source folder opened and indexing started"); else status("Downloaded folder could not be indexed"); } }
 }
 g_clear_error(&error); g_free(output); transfer_free(t); next_transfer();
}
static void transfer_run(Transfer *t) {
 remote_ui.active=t; GError *error=NULL; gchar *batch=NULL;
 if(t->kind==UPLOAD) { gchar *nonce=g_uuid_string_random(); batch=insight_remote_upload(t->snapshot,t->target,nonce,&error); g_free(nonce); }
 else batch=insight_remote_download(t->kind==OPEN_FILE || t->kind==PROJECT_READ ? t->origin : t->root->remote->remote,t->kind==OPEN_FILE || t->kind==PROJECT_READ ? t->target : t->root->remote->local,t->kind==OPEN_FOLDER || t->kind==PROJECT_FOLDER,&error);
 if(batch) { GSubprocessLauncher *spawn=launcher(t->root,&t->auth); t->process=spawn_client(spawn,t->root,TRUE,&error); g_object_unref(spawn); }
 if(!t->process) { status(error ? error->message : "SFTP unavailable"); g_clear_error(&error); g_free(batch); transfer_free(t); next_transfer(); return; }
 t->cancel=g_cancellable_new(); status(t->kind==UPLOAD ? "Uploading saved snapshot…" : "Downloading via SFTP…"); g_subprocess_communicate_utf8_async(t->process,batch,t->cancel,transfer_done,t); g_free(batch);
}
static RemoteRoot *form_root(const char *remote,const char *local,const char *directory,GError **error) {
 RemoteRoot *root=g_new0(RemoteRoot,1);gchar *host=g_strdup(gtk_entry_get_text(GTK_ENTRY(remote_ui.host))),*user=g_strdup(gtk_entry_get_text(GTK_ENTRY(remote_ui.user)));g_strstrip(host);g_strstrip(user);root->remote=insight_remote_new(host,user,(guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port)),gtk_entry_get_text(GTK_ENTRY(remote_ui.identity)),remote,local,error);g_free(host);g_free(user);
 if(!root->remote) { g_free(root); return NULL; } root->directory=g_strdup(directory); root->config=g_strdup(remote_ui.config); root->auto_upload=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.automatic)); return root;
}
static void open_remote(GtkWidget *widget,gpointer data) {
 (void)widget; if(remote_ui.active) { status("A transfer is active; stop it or wait before opening another location"); return; } profile_save();
 TransferKind kind=(TransferKind)GPOINTER_TO_INT(data); const char *path=gtk_entry_get_text(GTK_ENTRY(remote_ui.path));
 if(*path!='/' || (kind!=OPEN_FOLDER && (g_str_has_suffix(path,"/") || g_str_has_suffix(path,"/.") || g_str_has_suffix(path,"/..")))) { status("Enter an absolute remote file or folder path"); return; }
 gchar *uuid=g_uuid_string_random(),*directory=g_build_filename(remote_ui.directory,uuid,NULL),*local=g_build_filename(directory,"data",NULL),*base=g_strdup(path); gchar *slash=strrchr(base,'/'); if(kind!=OPEN_FOLDER) { if(slash==base) slash[1]=0; else *slash=0; } g_free(uuid);
 GError *error=NULL; RemoteRoot *root=form_root(base,local,directory,&error); g_free(base);
 if(!root || g_mkdir_with_parents(directory,0700)) { status(error ? error->message : "Cannot create SSH cache"); g_clear_error(&error); if(root) root_free(root); g_free(local); g_free(directory); return; }
 Transfer *t=g_new0(Transfer,1); t->root=root; t->kind=kind; t->origin=g_strdup(path);
 if(kind==OPEN_FILE) { g_mkdir(local,0700); const char *name=strrchr(path,'/')+1; t->target=g_build_filename(local,name,NULL); } else if(kind==PROJECT_READ) t->target=g_build_filename(directory,"project.geany",NULL);
 g_free(local); g_free(directory); transfer_run(t);
}
static RemoteRoot *root_for_file(const char *path,gchar **remote) {
 for(gint i=(gint)remote_ui.roots->len-1;i>=0;i--) { RemoteRoot *root=g_ptr_array_index(remote_ui.roots,(guint)i); *remote=insight_remote_path(root->remote,path); if(*remote) return root; } return NULL;
}
typedef struct { Transfer *transfer; gchar *source,*key; guint64 sequence,epoch; } UploadSnapshot;
/* Independent SCP/archive job. Each child is owned directly, so Stop also
 * cancels packing and never touches an interactive shell process. */
struct _ScpJob {
 RemoteRoot *root;gchar *local,*temporary,*stage,*auth,*client,*python,*helper;gint mode;gboolean upload,folder;
 GSubprocessLauncher *spawn;GSubprocess *process;GCancellable *cancel;GMutex lock;
};
static void remove_staging(const gchar *path) {
 if(!path) return;if(g_file_test(path,G_FILE_TEST_IS_DIR) && !g_file_test(path,G_FILE_TEST_IS_SYMLINK)) { GDir *dir=g_dir_open(path,0,NULL);const gchar *name;if(dir) { while((name=g_dir_read_name(dir))) { gchar *child=g_build_filename(path,name,NULL);remove_staging(child);g_free(child); }g_dir_close(dir); }g_rmdir(path); }else g_remove(path);
}
static void scp_free(gpointer data) {
 ScpJob *j=data;remove_staging(j->temporary);auth_cleanup(j->auth);root_free(j->root);g_free(j->local);g_free(j->temporary);g_free(j->stage);g_free(j->auth);g_free(j->client);g_free(j->python);g_free(j->helper);g_clear_object(&j->spawn);g_clear_object(&j->process);g_clear_object(&j->cancel);g_mutex_clear(&j->lock);g_free(j);
}
static void scp_cancel(void) {
 ScpJob *j=remote_ui.scp;if(!j) return;g_cancellable_cancel(j->cancel);g_mutex_lock(&j->lock);if(j->process) g_subprocess_force_exit(j->process);g_mutex_unlock(&j->lock);
}
static gboolean scp_child(ScpJob *j,const gchar *const *argv,const gchar *binary,GError **error) {
 if(g_cancellable_set_error_if_cancelled(j->cancel,error)) return FALSE;
 g_subprocess_launcher_set_flags(j->spawn,binary ? G_SUBPROCESS_FLAGS_STDERR_PIPE:G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE);
 if(binary) g_subprocess_launcher_set_stdout_file_path(j->spawn,binary);
 GSubprocess *p=g_subprocess_launcher_spawnv(j->spawn,argv,error);if(!p) return FALSE;
 g_mutex_lock(&j->lock);j->process=g_object_ref(p);if(g_cancellable_is_cancelled(j->cancel)) g_subprocess_force_exit(p);g_mutex_unlock(&j->lock);
 GInputStream *stream=binary ? g_subprocess_get_stderr_pipe(p):g_subprocess_get_stdout_pipe(p);GString *log=g_string_new(NULL);gchar buffer[4096];gboolean ok=TRUE;
 while(ok) { gssize n=g_input_stream_read(stream,buffer,sizeof buffer,j->cancel,error);if(n<0) { ok=FALSE;break; }if(!n) break;if(log->len+(gsize)n>262144) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Transfer diagnostics exceed 256 KiB");ok=FALSE;break; }g_string_append_len(log,buffer,n); }
 if(ok) ok=g_subprocess_wait(p,j->cancel,error);if(ok && !g_subprocess_get_successful(p)) { gchar *valid=g_utf8_make_valid(log->str,MIN(log->len,(gsize)8192));g_set_error(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Transfer failed: %s",*valid ? valid:"OpenSSH/packing process exited unsuccessfully");g_free(valid);ok=FALSE; }
 if(!ok) { g_subprocess_force_exit(p);g_subprocess_wait(p,NULL,NULL); }
 g_mutex_lock(&j->lock);g_clear_object(&j->process);g_mutex_unlock(&j->lock);g_string_free(log,TRUE);g_object_unref(p);return ok;
}
static void scp_worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel) {
 (void)object;ScpJob *j=data;GError *error=NULL;gboolean ok=TRUE;
 if(j->upload && j->mode) { const gchar *pack[]={j->python,"-I",j->helper,j->mode==1 ? "gz":"tar.gz",j->local,j->stage,NULL};ok=scp_child(j,pack,NULL,&error); }
 gchar **argv=NULL;if(ok) { if(!j->upload && j->mode) argv=insight_remote_archive_argv(j->root->remote,j->mode==2,&error);else argv=insight_remote_scp_argv(j->root->remote,j->upload ? (j->mode ? j->stage:j->local):j->stage,j->upload,j->folder && !j->mode,&error);ok=argv!=NULL; }
 if(ok) { g_free(argv[0]);argv[0]=g_strdup(j->client);if(!g_object_get_data(G_OBJECT(j->spawn),"insight-ssh-askpass")) { g_free(argv[1]);argv[1]=g_strdup("-oBatchMode=yes"); }ok=scp_child(j,(const gchar *const *)argv,!j->upload && j->mode ? j->stage:NULL,&error); }g_strfreev(argv);
 if(ok && !j->upload) { GFile *from=g_file_new_for_path(j->stage),*to=g_file_new_for_path(j->local);/* No overwrite, including a target created during transfer. */ok=g_file_move(from,to,G_FILE_COPY_NOFOLLOW_SYMLINKS,cancel,NULL,NULL,&error);g_object_unref(from);g_object_unref(to); }
 if(ok) g_task_return_boolean(task,TRUE);else if(error) g_task_return_error(task,error);else g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_FAILED,"Transfer failed");
}
static void scp_done(GObject *object,GAsyncResult *result,gpointer data) {
 (void)object;(void)data;ScpJob *j=g_task_get_task_data(G_TASK(result));GError *error=NULL;gboolean ok=g_task_propagate_boolean(G_TASK(result),&error);
 if(remote_ui.scp==j) { remote_ui.scp=NULL;if(!remote_ui.closing) { const gchar *message=ok ? "Transfer completed; terminal connections retained":error->message;gtk_label_set_text(GTK_LABEL(remote_ui.transfer_status),message);status(message); } }g_clear_error(&error);
}
static void scp_start(GtkWidget *widget,gpointer data) {
 (void)widget;if(remote_ui.scp) { status("A file transfer is already running. Stop it before starting another.");return; }
 const gchar *local=gtk_entry_get_text(GTK_ENTRY(remote_ui.transfer_local)),*remote=gtk_entry_get_text(GTK_ENTRY(remote_ui.transfer_remote));gboolean upload=GPOINTER_TO_INT(data),folder=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.transfer_folder));gint mode=gtk_combo_box_get_active(GTK_COMBO_BOX(remote_ui.transfer_mode));GError *error=NULL;
 if(!g_path_is_absolute(local) || (!upload && g_file_test(local,G_FILE_TEST_EXISTS)) || (upload && !g_file_test(local,folder ? G_FILE_TEST_IS_DIR:G_FILE_TEST_IS_REGULAR)) || (folder && mode==1)) { status("Choose an absolute local path and matching File/Folder mode. Receive requires a new destination. gz accepts one file.");return; }
 ScpJob *j=g_new0(ScpJob,1);g_mutex_init(&j->lock);j->cancel=g_cancellable_new();j->root=form_root(remote,local,remote_ui.directory,&error);j->upload=upload;j->folder=folder;j->mode=mode;j->local=g_strdup(local);
 if(j->root) { j->client=client_program(!upload && mode ? "ssh":"scp");if(!j->client) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Install the OpenSSH scp/ssh clients"); }
 if(j->root && !error) { j->spawn=launcher(j->root,&j->auth);gchar *parent=upload ? g_strdup(remote_ui.directory):g_path_get_dirname(local);gchar *template=g_build_filename(parent,".geany-transfer-XXXXXX",NULL);g_free(parent);j->temporary=g_mkdtemp(template);if(!j->temporary) { g_free(template);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create private transfer staging directory"); }else { j->stage=g_build_filename(j->temporary,mode==1 ? "payload.gz":mode==2 ? "payload.tar.gz":"payload",NULL); } }
 if(!error && upload && mode) { j->python=client_program("python3");if(!j->python) j->python=client_program("python");j->helper=g_build_filename(app->datadir,"insight","tools","remote_pack.py",NULL);if(!j->python || !g_file_test(j->helper,G_FILE_TEST_IS_REGULAR)) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND,"Archive upload requires Python 3 and the installed remote_pack.py helper"); }
 if(error || !j->root) { status(error ? error->message:"Invalid transfer settings");g_clear_error(&error);scp_free(j);return; }
 remote_ui.scp=j;gtk_label_set_text(GTK_LABEL(remote_ui.transfer_status),"Transferring… Stop cancels packing and transfer; SSH sessions stay connected.");GTask *task=g_task_new(NULL,j->cancel,scp_done,NULL);g_task_set_task_data(task,j,scp_free);g_task_run_in_thread(task,scp_worker);g_object_unref(task);
}
static void scp_choose(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;gboolean folder=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.transfer_folder));GtkWidget *d=gtk_file_chooser_dialog_new("Choose local source",GTK_WINDOW(main_widgets.window),folder ? GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER:GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Choose",GTK_RESPONSE_ACCEPT,NULL);if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));gtk_entry_set_text(GTK_ENTRY(remote_ui.transfer_local),path);g_free(path); }gtk_widget_destroy(d);
}
static void upload_snapshot_free(gpointer data) { UploadSnapshot *j=data; transfer_free(j->transfer); g_free(j->source); g_free(j->key); g_free(j); }
static void upload_snapshot_run(GTask *task,gpointer source,gpointer data,GCancellable *cancel) {
 (void)source; UploadSnapshot *j=data; GFile *from=g_file_new_for_path(j->source),*to=g_file_new_for_path(j->transfer->snapshot); GError *error=NULL;
 gboolean ok=g_file_copy(from,to,G_FILE_COPY_NONE,cancel,NULL,NULL,&error); g_object_unref(from); g_object_unref(to);
 if(error) g_task_return_error(task,error); else g_task_return_boolean(task,ok);
}
static void upload_snapshot_done(GObject *source,GAsyncResult *result,gpointer data) {
 (void)source; (void)data; GTask *task=G_TASK(result); UploadSnapshot *j=g_task_get_task_data(task); GError *error=NULL; gboolean copied=g_task_propagate_boolean(task,&error);
 if(remote_ui.closing || j->epoch!=remote_epoch) { g_clear_error(&error); return; }
 guint64 *latest=g_hash_table_lookup(remote_ui.upload_versions,j->key);
 if(!latest || *latest!=j->sequence) { g_clear_error(&error); return; }
 g_hash_table_remove(remote_ui.upload_versions,j->key);
 if(!copied) { status(error ? error->message : "Could not snapshot saved source"); g_clear_error(&error); return; }
 Transfer *t=j->transfer; j->transfer=NULL; InsightRemote *root=t->root->remote;
 for(GList *item=remote_ui.pending.head,*next;item;item=next) {
  next=item->next; Transfer *old=item->data;
  if(old->kind==UPLOAD && !strcmp(old->root->remote->host,root->host) && !strcmp(old->root->remote->user,root->user) && old->root->remote->port==root->port && !strcmp(old->target,t->target)) { g_queue_delete_link(&remote_ui.pending,item); transfer_free(old); }
 }
 if(remote_ui.active) { g_queue_push_tail(&remote_ui.pending,t); status("Saved locally; latest SSH upload queued"); } else transfer_run(t);
}
static void upload_document(GeanyDocument *doc) {
 if(!doc || !doc->file_name) return;
 gchar *path=NULL; RemoteRoot *root=root_for_file(doc->file_name,&path); if(!root) { status("Current file is not associated with an SSH source folder"); return; }
 Transfer *t=g_new0(Transfer,1); t->kind=UPLOAD; t->root=root_copy(root); t->target=path; gchar *uuid=g_uuid_string_random(); t->snapshot=g_build_filename(remote_ui.directory,uuid,NULL); g_free(uuid);
 UploadSnapshot *j=g_new0(UploadSnapshot,1); j->transfer=t; j->source=utils_get_locale_from_utf8(doc->file_name); j->epoch=remote_epoch; j->sequence=++upload_sequence;
 gchar *target=credentials_target(root->remote); j->key=g_strconcat(target,":",path,NULL); g_free(target);
 guint64 *version=g_new(guint64,1); *version=j->sequence; g_hash_table_replace(remote_ui.upload_versions,g_strdup(j->key),version);
 GTask *task=g_task_new(NULL,remote_ui.snapshot_cancel,upload_snapshot_done,NULL); g_task_set_task_data(task,j,upload_snapshot_free); g_task_run_in_thread(task,upload_snapshot_run); g_object_unref(task); status("Saved locally; preparing SSH upload snapshot…");
}
static void saved_document(GObject *object,GeanyDocument *doc,gpointer data) { (void)object; (void)data; if(!doc || !doc->file_name) return; gchar *path=NULL; RemoteRoot *root=root_for_file(doc->file_name,&path); g_free(path); if(root && root->auto_upload) upload_document(doc); }
static void save_remote(GtkWidget *widget,gpointer data) { (void)widget; (void)data; GeanyDocument *doc=document_get_current(); if(!doc) return; gchar *path=NULL; RemoteRoot *root=doc->file_name ? root_for_file(doc->file_name,&path) : NULL; g_free(path); if(!root) { status("Open a file through SSH first"); return; } if(!doc->changed) upload_document(doc); else if(document_save_file(doc,FALSE) && !root->auto_upload) upload_document(doc); }
static void stop_transfer(GtkWidget *widget,gpointer data) { (void)widget; (void)data; scp_cancel(); remote_epoch++; if(remote_ui.snapshot_cancel) { g_cancellable_cancel(remote_ui.snapshot_cancel); g_clear_object(&remote_ui.snapshot_cancel); } if(!remote_ui.closing) remote_ui.snapshot_cancel=g_cancellable_new(); if(remote_ui.upload_versions) g_hash_table_remove_all(remote_ui.upload_versions); if(remote_ui.active) { g_cancellable_cancel(remote_ui.active->cancel); g_subprocess_force_exit(remote_ui.active->process); } while(!g_queue_is_empty(&remote_ui.pending)) transfer_free(g_queue_pop_head(&remote_ui.pending)); status("Transfers stopped; local edits retained"); }
static void shell_append(ShellSession *s,const gchar *bytes,gsize size) {
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->output)); g_string_append_len(s->tail,bytes,(gssize)size); GString *plain=g_string_new(NULL); gsize consumed=0;
 for(const gchar *p=s->tail->str,*end=p+s->tail->len;p<end;) {
  guint8 c=(guint8)*p;
  if(s->ansi) { p++; consumed=(gsize)(p-s->tail->str); if(s->ansi==1) s->ansi=c=='[' ? 2 : (c==']' ? 3 : 0); else if(s->ansi==2 && c>=0x40 && c<=0x7e) s->ansi=0; else if(s->ansi==3 && c==7) s->ansi=0; else if(s->ansi==3 && c==27) s->ansi=4; else if(s->ansi==4) s->ansi=c=='\\' ? 0 : 3; continue; }
  if(c==27) { s->ansi=1; p++; consumed=(gsize)(p-s->tail->str); continue; }
  if(c=='\r') { s->carriage=TRUE; p++; consumed=(gsize)(p-s->tail->str); continue; }
  if(s->carriage) { if(c!='\n') { if(plain->len) { GtkTextIter it; gtk_text_buffer_get_end_iter(buffer,&it); gtk_text_buffer_insert(buffer,&it,plain->str,(gint)plain->len); g_string_truncate(plain,0); } GtkTextIter a,b; gtk_text_buffer_get_end_iter(buffer,&b); a=b; gtk_text_iter_set_line_offset(&a,0); gtk_text_buffer_delete(buffer,&a,&b); } s->carriage=FALSE; }
  if(c==8) { if(plain->len) { gchar *previous=g_utf8_find_prev_char(plain->str,plain->str+plain->len); if(previous) g_string_truncate(plain,(gsize)(previous-plain->str)); } else { GtkTextIter a,b; gtk_text_buffer_get_end_iter(buffer,&b); a=b; if(gtk_text_iter_backward_char(&a)) gtk_text_buffer_delete(buffer,&a,&b); } p++; consumed=(gsize)(p-s->tail->str); continue; }
  gunichar character=g_utf8_get_char_validated(p,(gssize)(end-p)); if(character==(gunichar)-2) break;
  if(character==(gunichar)-1) { g_string_append(plain,"\357\277\275"); p++; } else { const gchar *next=g_utf8_next_char(p); if(character>=32 || character=='\n' || character=='\t') g_string_append_len(plain,p,next-p); p=next; }
  consumed=(gsize)(p-s->tail->str);
 }
 g_string_erase(s->tail,0,(gssize)consumed); GtkTextIter it; gtk_text_buffer_get_end_iter(buffer,&it); gtk_text_buffer_insert(buffer,&it,plain->str,(gint)plain->len); g_string_free(plain,TRUE);
 if(gtk_text_buffer_get_char_count(buffer)>1000000) { GtkTextIter a,b; gtk_text_buffer_get_start_iter(buffer,&a); b=a; gtk_text_iter_forward_chars(&b,100000); gtk_text_buffer_delete(buffer,&a,&b); }
 gtk_text_buffer_get_end_iter(buffer,&it); gtk_text_buffer_place_cursor(buffer,&it); gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(s->output),&it,0,FALSE,0,0);
}
static void shell_complete(ShellSession *s,GSubprocess *process) {
 if(remote_ui.closing || s->process!=process || !g_object_get_data(G_OBJECT(process),"insight-ssh-eof") || !g_object_get_data(G_OBJECT(process),"insight-ssh-exited")) return;
 const gchar *error=g_object_get_data(G_OBJECT(process),"insight-ssh-error");gchar *message=NULL;
 if(error) message=g_strdup_printf("SSH failed: %s",error);
 else if(g_subprocess_get_if_exited(process)) { gint code=g_subprocess_get_exit_status(process);message=code ? g_strdup_printf("SSH client exited with status %d. See its output above.",code) : g_strdup("SSH session ended"); }
 else message=g_strdup("SSH client terminated. See its output above.");
 session_status(s,message);g_free(message);g_cancellable_cancel(s->cancel);g_clear_object(&s->cancel);g_clear_object(&s->process);auth_cleanup(s->auth);g_clear_pointer(&s->auth,g_free);
 s->writing=FALSE;insight_queue_clear_full(&s->pending,g_free);
}
static void shell_read(GObject *object,GAsyncResult *result,gpointer data) {
 GSubprocess *process=data;ShellSession *s=g_object_get_data(G_OBJECT(process),"insight-ssh-session");GError *error=NULL;GBytes *bytes=g_input_stream_read_bytes_finish(G_INPUT_STREAM(object),result,&error);
 if(!remote_ui.closing && s->process==process) {
  if(bytes && g_bytes_get_size(bytes)) { if(!g_object_get_data(G_OBJECT(process),"insight-ssh-output-seen")) { g_object_set_data(G_OBJECT(process),"insight-ssh-output-seen",GINT_TO_POINTER(1));session_status(s,"SSH client running — authentication and server output follow below"); }gsize size;const gchar *text=g_bytes_get_data(bytes,&size);shell_append(s,text,size);g_input_stream_read_bytes_async(g_subprocess_get_stdout_pipe(process),8192,G_PRIORITY_DEFAULT,s->cancel,shell_read,g_object_ref(process)); }
  else { if(error) { g_object_set_data_full(G_OBJECT(process),"insight-ssh-error",g_strdup(error->message),g_free);g_subprocess_force_exit(process); }g_object_set_data(G_OBJECT(process),"insight-ssh-eof",GINT_TO_POINTER(1));shell_complete(s,process); }
 }
 if(bytes) g_bytes_unref(bytes);g_clear_error(&error);g_object_unref(process);
}
static void shell_finished(GObject *object,GAsyncResult *result,gpointer data) {
 (void)data;GSubprocess *process=G_SUBPROCESS(object);ShellSession *s=g_object_get_data(object,"insight-ssh-session");GError *error=NULL;gboolean ok=g_subprocess_wait_finish(process,result,&error);
 if(!remote_ui.closing && s->process==process) { if(!ok) g_object_set_data_full(G_OBJECT(process),"insight-ssh-error",g_strdup(error ? error->message : "Cannot wait for SSH client"),g_free);g_object_set_data(G_OBJECT(process),"insight-ssh-exited",GINT_TO_POINTER(1));shell_complete(s,process); }
 g_clear_error(&error);
}
static void session_disconnect(ShellSession *s) {
 if(s->process) { g_cancellable_cancel(s->cancel);g_subprocess_force_exit(s->process);g_clear_object(&s->process);g_clear_object(&s->cancel);auth_cleanup(s->auth);g_clear_pointer(&s->auth,g_free); }
 s->writing=FALSE;insight_queue_clear_full(&s->pending,g_free);
}
static void shell_disconnect(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(!remote_ui.selected) return;session_disconnect(remote_ui.selected);session_status(remote_ui.selected,"SSH shell disconnected");
}
static void shell_connect(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;ShellSession *s=remote_ui.selected;if(!s) return;profile_save();session_disconnect(s); GError *error=NULL; RemoteRoot *root=form_root("/",remote_ui.directory,remote_ui.directory,&error);
 if(!root) { session_status(s,error ? error->message : "Invalid SSH connection"); g_clear_error(&error); return; }
 GSubprocessLauncher *spawn=launcher(root,&s->auth); g_subprocess_launcher_setenv(spawn,"TERM","dumb",TRUE); gboolean askpass=g_object_get_data(G_OBJECT(spawn),"insight-ssh-askpass")!=NULL;session_status(s,"Starting SSH client; waiting for the server and authentication…");s->process=spawn_client(spawn,root,FALSE,&error);g_object_unref(spawn);g_free(s->endpoint);s->endpoint=g_strdup_printf("%s@%s:%u",root->remote->user,root->remote->host,root->remote->port);root_free(root);
 if(!s->process) { session_status(s,error ? error->message : "OpenSSH unavailable"); g_clear_error(&error); auth_cleanup(s->auth); g_clear_pointer(&s->auth,g_free); return; }
 g_object_set_data_full(G_OBJECT(s->process),"insight-ssh-session",session_ref(s),session_unref);s->cancel=g_cancellable_new(); s->ansi=0; s->carriage=FALSE; g_string_truncate(s->tail,0);
 g_input_stream_read_bytes_async(g_subprocess_get_stdout_pipe(s->process),8192,G_PRIORITY_DEFAULT,s->cancel,shell_read,g_object_ref(s->process)); g_subprocess_wait_async(s->process,NULL,shell_finished,NULL);
 if(!askpass) session_status(s,"SSH password dialog helper is missing; only key/agent authentication is available.");gtk_notebook_set_current_page(GTK_NOTEBOOK(remote_ui.tabs),gtk_notebook_page_num(GTK_NOTEBOOK(remote_ui.tabs),s->page));gtk_widget_grab_focus(s->input);
}
typedef struct { GSubprocess *process; gchar *text; } ShellWrite;
static void shell_flush(ShellSession *s);
static void shell_write_done(GObject *object,GAsyncResult *result,gpointer data) {
 ShellWrite *write=data;ShellSession *s=g_object_get_data(G_OBJECT(write->process),"insight-ssh-session");GError *error=NULL; gboolean ok=g_output_stream_write_all_finish(G_OUTPUT_STREAM(object),result,NULL,&error);
 if(!remote_ui.closing && s->process==write->process) { s->writing=FALSE; if(!ok) session_status(s,error ? error->message : "SSH shell write failed"); else shell_flush(s); }
 g_clear_error(&error); g_object_unref(write->process); g_free(write->text); g_free(write);
}
static void shell_flush(ShellSession *s) {
 if(!s->process || s->writing || g_queue_is_empty(&s->pending)) return;
 ShellWrite *write=g_new0(ShellWrite,1); write->process=g_object_ref(s->process); write->text=g_queue_pop_head(&s->pending); s->writing=TRUE;
 g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(s->process),write->text,strlen(write->text),G_PRIORITY_DEFAULT,s->cancel,shell_write_done,write);
}
static void shell_send(GtkWidget *widget,gpointer data) {
 (void)widget;ShellSession *s=remote_ui.selected;if(!s || !s->process) { session_status(s,"Connect SSH shell first"); return; }
 gboolean interrupt=GPOINTER_TO_INT(data)!=0; gchar *text=interrupt ? g_strdup("\003") : g_strdup_printf("%s\n",gtk_entry_get_text(GTK_ENTRY(s->input)));
 g_queue_push_tail(&s->pending,text); shell_flush(s); if(!interrupt) gtk_entry_set_text(GTK_ENTRY(s->input),"");
}
static gboolean shell_key(GtkWidget *widget,GdkEventKey *event,gpointer data) { (void)widget; (void)data; if(event->keyval==GDK_KEY_c && (event->state&GDK_CONTROL_MASK)) { shell_send(NULL,GINT_TO_POINTER(1)); return TRUE; } return FALSE; }
static void identity_choose(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; GtkWidget *dialog=gtk_file_chooser_dialog_new("SSH identity file",GTK_WINDOW(main_widgets.window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Choose",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(dialog))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog)); gtk_entry_set_text(GTK_ENTRY(remote_ui.identity),path); g_free(path); } gtk_widget_destroy(dialog);
}
static gboolean endpoint_matches(const InsightRemote *r) { return !strcmp(r->host,gtk_entry_get_text(GTK_ENTRY(remote_ui.host))) && !strcmp(r->user,gtk_entry_get_text(GTK_ENTRY(remote_ui.user))) && r->port==(guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port)); }
static void profile_store_key(GKeyFile *key) {
 g_key_file_set_string(key,"insight-ssh","host",gtk_entry_get_text(GTK_ENTRY(remote_ui.host))); g_key_file_set_string(key,"insight-ssh","user",gtk_entry_get_text(GTK_ENTRY(remote_ui.user))); g_key_file_set_integer(key,"insight-ssh","port",gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port))); g_key_file_set_string(key,"insight-ssh","identity",gtk_entry_get_text(GTK_ENTRY(remote_ui.identity))); g_key_file_set_string(key,"insight-ssh","remote-path",gtk_entry_get_text(GTK_ENTRY(remote_ui.path))); g_key_file_set_boolean(key,"insight-ssh","save-password",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password))); g_key_file_set_boolean(key,"insight-ssh","auto-upload",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.automatic)));
}
static void profile_save(void) {
 if(remote_ui.loading || remote_ui.closing) return;
 GKeyFile *key=g_key_file_new(); if(g_file_test(remote_ui.config,G_FILE_TEST_EXISTS) && !g_key_file_load_from_file(key,remote_ui.config,G_KEY_FILE_KEEP_COMMENTS,NULL)) { g_key_file_unref(key); status("SSH profile not saved: invalid project/config file"); return; }
 profile_store_key(key);
 gsize length; gchar *data=g_key_file_to_data(key,&length,NULL); GFile *file=g_file_new_for_path(remote_ui.config); GError *error=NULL; if(!g_file_replace_contents(file,data,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error)) status(error ? error->message : "SSH profile write failed"); g_clear_error(&error); g_object_unref(file); g_free(data); g_key_file_unref(key);
}
static void profile_toggle(GtkWidget *widget,gpointer data) { (void)widget; (void)data; if(remote_ui.loading) return; gboolean enabled=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.automatic)); for(guint i=0;i<remote_ui.roots->len;i++) { RemoteRoot *root=g_ptr_array_index(remote_ui.roots,i); if(endpoint_matches(root->remote)) { root->auto_upload=enabled; root_metadata(root); } } if(remote_ui.active && endpoint_matches(remote_ui.active->root->remote)) remote_ui.active->root->auto_upload=enabled; profile_save(); }
static void profile_load(GKeyFile *key) {
 remote_ui.loading=TRUE; const char *names[]={"host","user","identity","remote-path"}; GtkWidget *fields[]={remote_ui.host,remote_ui.user,remote_ui.identity,remote_ui.path};
 for(guint i=0;i<4;i++) { gchar *value=g_key_file_get_string(key,"insight-ssh",names[i],NULL); gtk_entry_set_text(GTK_ENTRY(fields[i]),value ? value : ""); g_free(value); }
 gint port=g_key_file_get_integer(key,"insight-ssh","port",NULL); gtk_spin_button_set_value(GTK_SPIN_BUTTON(remote_ui.port),port>0 && port<=65535 ? port : 22); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remote_ui.save_password),g_key_file_get_boolean(key,"insight-ssh","save-password",NULL)); gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remote_ui.automatic),g_key_file_get_boolean(key,"insight-ssh","auto-upload",NULL)); remote_ui.loading=FALSE;
}
static void project_changed(GObject *object,GKeyFile *key,gpointer data) {
 (void)object; if(!app->project || !app->project->file_name) return;
 gchar *previous=g_strdup(remote_ui.config); g_free(remote_ui.config); remote_ui.config=utils_get_locale_from_utf8(app->project->file_name);
 if(GPOINTER_TO_INT(data)) {
  RemoteRoot *form=form_root("/",remote_ui.directory,remote_ui.directory,NULL);
  if(form) {
   gchar *target=credentials_target(form->remote),*password=insight_remote_password_load(previous,target,NULL);
   if(password && gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password))) { gchar *group=g_strdup_printf("insight-ssh-password-%s",target); g_key_file_set_string(key,group,"password",password); g_free(group); }
   g_free(password); g_free(target); root_free(form);
  }
  profile_store_key(key);
  gchar *base_path=project_get_base_path();
  if(base_path) for(guint i=0;i<remote_ui.roots->len;i++) { RemoteRoot *root=g_ptr_array_index(remote_ui.roots,i); if(!g_strcmp0(root->remote->local,base_path)) { g_key_file_set_string(key,"insight-ssh","remote-base-path",root->remote->remote); break; } }
  g_free(base_path);
 } else if(g_key_file_has_group(key,"insight-ssh")) profile_load(key);
 gchar *remote=g_key_file_get_string(key,"insight-ssh","remote-base-path",NULL),*base=project_get_base_path();
 if(remote && *remote && base) { RemoteRoot *old=NULL; for(guint i=0;i<remote_ui.roots->len;i++) { RemoteRoot *candidate=g_ptr_array_index(remote_ui.roots,i); if(!g_strcmp0(candidate->remote->local,base)) { old=candidate; break; } } if(old) { g_free(old->config); old->config=g_strdup(remote_ui.config); old->auto_upload=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.automatic)); root_metadata(old); } else { RemoteRoot *root=form_root(remote,base,remote_ui.directory,NULL); if(root) g_ptr_array_add(remote_ui.roots,root); } }
 g_free(remote); g_free(base); g_free(previous);
}
static void project_closed(GObject *object,gpointer data) { (void)object; (void)data; g_free(remote_ui.config); remote_ui.config=g_build_filename(app->configdir,"insight","ssh.conf",NULL); GKeyFile *key=g_key_file_new(); g_key_file_load_from_file(key,remote_ui.config,G_KEY_FILE_NONE,NULL); profile_load(key); g_key_file_unref(key); }
static void forget_password(GtkWidget *widget,gpointer data) { (void)widget; (void)data; RemoteRoot *root=form_root("/",remote_ui.directory,remote_ui.directory,NULL); if(!root) return; gchar *target=credentials_target(root->remote); GError *error=NULL; if(insight_remote_password_forget(remote_ui.config,target,&error)) status("Saved login password removed from this config"); else status(error ? error->message : "Could not remove password"); g_clear_error(&error); g_free(target); root_free(root); }
static void show_remote(GtkWidget *widget,gpointer data) { (void)widget; (void)data; msgwin_show_hide(TRUE); gtk_notebook_set_current_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),gtk_notebook_page_num(GTK_NOTEBOOK(main_widgets.message_window_notebook),remote_ui.page)); gtk_widget_grab_focus(remote_ui.host); }
static gboolean ai_insert(const gchar *command,gpointer data) {
 ShellSession *s=data;if(remote_ui.closing || !s->input) return FALSE;gtk_entry_set_text(GTK_ENTRY(s->input),command);return TRUE;
}
static gchar *session_context(ShellSession *s) {
 GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(s->output));GtkTextIter a,z;if(!gtk_text_buffer_get_selection_bounds(b,&a,&z)) { gtk_text_buffer_get_end_iter(b,&z);a=z;gtk_text_iter_backward_chars(&a,12000); }
 gchar *log=gtk_text_buffer_get_text(b,&a,&z,TRUE),*context=g_strdup_printf("SSH session %u\nConnected endpoint: %s\nConnection form: host=%s user=%s port=%d\nTransfer remote path: %s\nThe shell is line-oriented. Analyze connection/transfer issues and remote diagnostics; relate file:line errors to explicitly attached local source. Commands execute only on explicit user action.\n\n%s",s->number,s->endpoint ? s->endpoint:"not connected",gtk_entry_get_text(GTK_ENTRY(remote_ui.host)),gtk_entry_get_text(GTK_ENTRY(remote_ui.user)),gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port)),gtk_entry_get_text(GTK_ENTRY(remote_ui.path)),log);g_free(log);return context;
}
static void ai_back(GtkWidget *widget,gpointer data) { (void)widget;ShellSession *s=data;gtk_stack_set_visible_child_name(GTK_STACK(s->stack),"terminal"); }
static void ai_execute(GtkWidget *widget,gpointer data) {
 (void)widget;ShellSession *s=data;if(!s->process) { session_status(s,"Connect this SSH session first");return; }gchar *command=insight_ai_suggested_command(s->chat);if(!command) { session_status(s,"Select a complete AI answer with a valid suggested command first");return; }
 g_queue_push_tail(&s->pending,g_strconcat(command,"\n",NULL));shell_flush(s);g_free(command);session_status(s,"Reviewed AI command sent to this SSH session");
}
static void ai_console(GtkWidget *widget,gpointer data) {
 (void)widget;ShellSession *s=data ? data:remote_ui.selected;if(!s) return;gchar *context=session_context(s);
 if(!s->chat) {
  GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,3),*row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*back=gtk_button_new_with_label("Terminal"),*run=gtk_button_new_with_label("Run reviewed command in this SSH session");
  gtk_box_pack_start(GTK_BOX(row),back,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),run,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(body),row,FALSE,FALSE,0);g_signal_connect(back,"clicked",G_CALLBACK(ai_back),s);g_signal_connect(run,"clicked",G_CALLBACK(ai_execute),s);gtk_widget_set_name(run,"insight-ssh-ai-run");
  gtk_stack_add_named(GTK_STACK(s->stack),body,"assistant");s->chat=insight_ai_embed(body,"SSH assistant — connection, transfer, errors and local code",context,s->page,ai_insert,s);g_object_add_weak_pointer(G_OBJECT(s->chat),(gpointer *)&s->chat);g_object_set_data_full(G_OBJECT(s->chat),"insight-ssh-session-owner",session_ref(s),session_unref);gtk_widget_show_all(body);
 }else insight_ai_context_update(s->chat,context);
 g_free(context);gtk_stack_set_visible_child_name(GTK_STACK(s->stack),"assistant");gtk_notebook_set_current_page(GTK_NOTEBOOK(remote_ui.tabs),gtk_notebook_page_num(GTK_NOTEBOOK(remote_ui.tabs),s->page));
}
static void ai_position(GtkWidget *widget,GtkAllocation *allocation,gpointer data) {
 (void)widget;ShellSession *s=data;gint w=MAX(0,allocation->width-gtk_widget_get_allocated_width(s->ai_button)),h=MAX(0,allocation->height-gtk_widget_get_allocated_height(s->ai_button));gint x=(gint)(s->x*w),y=(gint)(s->y*h);
 if(gtk_widget_get_margin_start(s->ai_button)!=x) gtk_widget_set_margin_start(s->ai_button,x);if(gtk_widget_get_margin_top(s->ai_button)!=y) gtk_widget_set_margin_top(s->ai_button,y);
}
static gboolean ai_press(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget;ShellSession *s=data;if(event->button!=1) return FALSE;s->dragging=TRUE;s->moved=FALSE;s->press_x=event->x_root;s->press_y=event->y_root;s->start_x=gtk_widget_get_margin_start(s->ai_button);s->start_y=gtk_widget_get_margin_top(s->ai_button);return TRUE;
}
static gboolean ai_motion(GtkWidget *widget,GdkEventMotion *event,gpointer data) {
 (void)widget;ShellSession *s=data;if(!s->dragging) return FALSE;gdouble dx=event->x_root-s->press_x,dy=event->y_root-s->press_y;if(dx*dx+dy*dy<25 && !s->moved) return TRUE;s->moved=TRUE;
 gint w=MAX(1,gtk_widget_get_allocated_width(s->overlay)-gtk_widget_get_allocated_width(s->ai_button)),h=MAX(1,gtk_widget_get_allocated_height(s->overlay)-gtk_widget_get_allocated_height(s->ai_button));s->x=CLAMP((s->start_x+dx)/w,0,1);s->y=CLAMP((s->start_y+dy)/h,0,1);GtkAllocation a;gtk_widget_get_allocation(s->overlay,&a);ai_position(s->overlay,&a,s);return TRUE;
}
static gboolean ai_release(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget;ShellSession *s=data;if(event->button!=1 || !s->dragging) return FALSE;s->dragging=FALSE;if(!s->moved) ai_console(NULL,s);return TRUE;
}
static void ssh_tab(GtkWidget *page,const gchar *label,const gchar *hint) {
 GtkWidget *tab=gtk_label_new(label);gtk_label_set_angle(GTK_LABEL(tab),270);gtk_widget_set_tooltip_text(tab,hint);gtk_notebook_append_page(GTK_NOTEBOOK(remote_ui.tabs),page,tab);
}
static void session_selected(GtkNotebook *notebook,GtkWidget *page,guint number,gpointer data) {
 (void)notebook;(void)number;(void)data;ShellSession *s=g_object_get_data(G_OBJECT(page),"insight-ssh-session");if(s) { remote_ui.selected=s;remote_ui.output=s->output;remote_ui.input=s->input; }
}
static void session_add(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;if(remote_ui.sessions->len>=16) { status("Up to 16 SSH terminals can be open");return; }
 ShellSession *s=g_new0(ShellSession,1);s->refs=1;s->number=remote_ui.sessions->len+1;s->x=1;s->tail=g_string_new(NULL);s->page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);g_object_ref_sink(s->page);g_object_set_data(G_OBJECT(s->page),"insight-ssh-session",s);
 s->stack=gtk_stack_new();gtk_stack_set_transition_type(GTK_STACK(s->stack),GTK_STACK_TRANSITION_TYPE_NONE);gtk_box_pack_start(GTK_BOX(s->page),s->stack,TRUE,TRUE,0);GtkWidget *terminal=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_stack_add_named(GTK_STACK(s->stack),terminal,"terminal");
 s->overlay=gtk_overlay_new();GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);s->output=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(s->output),FALSE);gtk_text_view_set_monospace(GTK_TEXT_VIEW(s->output),TRUE);gtk_container_add(GTK_CONTAINER(scroll),s->output);gtk_container_add(GTK_CONTAINER(s->overlay),scroll);gtk_box_pack_start(GTK_BOX(terminal),s->overlay,TRUE,TRUE,0);
 s->ai_button=gtk_button_new_with_label("Ai");gtk_widget_set_name(s->ai_button,"insight-ssh-ai");gtk_widget_set_opacity(s->ai_button,0.8);gtk_widget_set_tooltip_text(s->ai_button,"SSH AI assistant — drag to move");gtk_widget_set_halign(s->ai_button,GTK_ALIGN_START);gtk_widget_set_valign(s->ai_button,GTK_ALIGN_START);gtk_widget_add_events(s->ai_button,GDK_BUTTON_PRESS_MASK|GDK_BUTTON_RELEASE_MASK|GDK_POINTER_MOTION_MASK);gtk_overlay_add_overlay(GTK_OVERLAY(s->overlay),s->ai_button);
 g_signal_connect(s->ai_button,"clicked",G_CALLBACK(ai_console),s);g_signal_connect(s->ai_button,"button-press-event",G_CALLBACK(ai_press),s);g_signal_connect(s->ai_button,"motion-notify-event",G_CALLBACK(ai_motion),s);g_signal_connect(s->ai_button,"button-release-event",G_CALLBACK(ai_release),s);g_signal_connect(s->overlay,"size-allocate",G_CALLBACK(ai_position),s);
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);s->input=gtk_entry_new();gtk_entry_set_width_chars(GTK_ENTRY(s->input),1);gtk_entry_set_placeholder_text(GTK_ENTRY(s->input),"SSH command — Enter sends, Ctrl+C interrupts");gtk_box_pack_start(GTK_BOX(row),s->input,TRUE,TRUE,0);GtkWidget *send=gtk_button_new_with_label("Send"),*interrupt=gtk_button_new_with_label("Ctrl+C");gtk_box_pack_start(GTK_BOX(row),send,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),interrupt,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(terminal),row,FALSE,FALSE,0);g_signal_connect(send,"clicked",G_CALLBACK(shell_send),NULL);g_signal_connect(interrupt,"clicked",G_CALLBACK(shell_send),GINT_TO_POINTER(1));g_signal_connect(s->input,"activate",G_CALLBACK(shell_send),NULL);g_signal_connect(s->input,"key-press-event",G_CALLBACK(shell_key),NULL);
 gchar *name=g_strdup_printf("insight-ssh-session-%u",s->number);gtk_widget_set_name(s->page,name);g_free(name);name=s->number==1 ? g_strdup("insight-ssh-output"):g_strdup_printf("insight-ssh-output-%u",s->number);gtk_widget_set_name(s->output,name);g_free(name);name=s->number==1 ? g_strdup("insight-ssh-input"):g_strdup_printf("insight-ssh-input-%u",s->number);gtk_widget_set_name(s->input,name);g_free(name);g_ptr_array_add(remote_ui.sessions,s);name=g_strdup_printf("SSH %u",s->number);ssh_tab(s->page,name,"Independent SSH terminal");g_free(name);gtk_widget_show_all(s->page);gtk_notebook_set_current_page(GTK_NOTEBOOK(remote_ui.tabs),gtk_notebook_page_num(GTK_NOTEBOOK(remote_ui.tabs),s->page));
}
static void form_row(GtkWidget *grid,gint row,const gchar *label,GtkWidget *field) {
 GtkWidget *title=gtk_label_new(label);gtk_label_set_xalign(GTK_LABEL(title),0);gtk_grid_attach(GTK_GRID(grid),title,0,row,1,1);gtk_widget_set_hexpand(field,TRUE);if(GTK_IS_ENTRY(field)) gtk_entry_set_width_chars(GTK_ENTRY(field),1);gtk_grid_attach(GTK_GRID(grid),field,1,row,1,1);
}
static GtkWidget *form_page(const gchar *label) {
 GtkWidget *body=gtk_box_new(GTK_ORIENTATION_VERTICAL,4),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_set_border_width(GTK_CONTAINER(body),5);gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),GTK_POLICY_NEVER,GTK_POLICY_AUTOMATIC);gtk_container_add(GTK_CONTAINER(scroll),body);ssh_tab(scroll,label,label);return body;
}
static GtkWidget *form_button(GtkWidget *box,const gchar *label,const gchar *name,GCallback cb,gpointer data) {
 GtkWidget *b=gtk_button_new_with_label(label);if(name) gtk_widget_set_name(b,name);gtk_box_pack_start(GTK_BOX(box),b,FALSE,FALSE,0);g_signal_connect(b,"clicked",cb,data);return b;
}
void insight_remote_ui_init(gboolean (*workspace)(const char *)) {
 remote_epoch++; remote_ui.upload_versions=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); remote_ui.snapshot_cancel=g_cancellable_new();
 remote_ui.workspace=workspace; remote_ui.roots=g_ptr_array_new_with_free_func(root_free); remote_ui.directory=g_build_filename(app->configdir,"insight","remote",NULL); g_mkdir_with_parents(remote_ui.directory,0700); remote_ui.config=g_build_filename(app->configdir,"insight","ssh.conf",NULL);
 restore_roots();
 remote_ui.sessions=g_ptr_array_new_with_free_func(session_unref);remote_ui.page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);gtk_widget_set_name(remote_ui.page,"insight-ssh-page");remote_ui.tabs=gtk_notebook_new();gtk_widget_set_name(remote_ui.tabs,"insight-ssh-tabs");gtk_notebook_set_tab_pos(GTK_NOTEBOOK(remote_ui.tabs),GTK_POS_RIGHT);gtk_notebook_set_scrollable(GTK_NOTEBOOK(remote_ui.tabs),TRUE);gtk_box_pack_start(GTK_BOX(remote_ui.page),remote_ui.tabs,TRUE,TRUE,0);g_signal_connect(remote_ui.tabs,"switch-page",G_CALLBACK(session_selected),NULL);
 remote_ui.connection=form_page("Connection");GtkWidget *grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),4);gtk_grid_set_column_spacing(GTK_GRID(grid),5);gtk_box_pack_start(GTK_BOX(remote_ui.connection),grid,FALSE,FALSE,0);
 remote_ui.host=gtk_entry_new();remote_ui.user=gtk_entry_new();remote_ui.identity=gtk_entry_new();remote_ui.port=gtk_spin_button_new_with_range(1,65535,1);gtk_spin_button_set_value(GTK_SPIN_BUTTON(remote_ui.port),22);GtkWidget *fields[]={remote_ui.host,remote_ui.user,remote_ui.port,remote_ui.identity};const gchar *labels[]={"Host / alias","Login","Port","Identity file"},*names[]={"insight-ssh-host","insight-ssh-user","insight-ssh-port","insight-ssh-identity"};for(guint i=0;i<4;i++) { gtk_widget_set_name(fields[i],names[i]);form_row(grid,i,labels[i],fields[i]); }
 GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(remote_ui.connection),row,FALSE,FALSE,0);form_button(row,"Key…",NULL,G_CALLBACK(identity_choose),NULL);form_button(row,"Connect shell","insight-ssh-connect",G_CALLBACK(shell_connect),NULL);form_button(row,"Disconnect","insight-ssh-disconnect",G_CALLBACK(shell_disconnect),NULL);form_button(row,"AI help",NULL,G_CALLBACK(ai_console),NULL);
 remote_ui.save_password=gtk_check_button_new_with_label("Save password");gtk_widget_set_tooltip_text(remote_ui.save_password,"Opt-in: save password / key passphrase as text in this project or global SSH config");gtk_box_pack_start(GTK_BOX(remote_ui.connection),remote_ui.save_password,FALSE,FALSE,0);form_button(remote_ui.connection,"Forget saved password",NULL,G_CALLBACK(forget_password),NULL);
 remote_ui.files=form_page("Transfer");remote_ui.path=gtk_entry_new();gtk_entry_set_width_chars(GTK_ENTRY(remote_ui.path),1);gtk_widget_set_name(remote_ui.path,"insight-ssh-path");gtk_entry_set_placeholder_text(GTK_ENTRY(remote_ui.path),"Absolute remote file, folder or project path");gtk_box_pack_start(GTK_BOX(remote_ui.files),remote_ui.path,FALSE,FALSE,0);row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(remote_ui.files),row,FALSE,FALSE,0);const gchar *open_labels[]={"Open file","Open folder","Open project"};for(guint i=0;i<3;i++) form_button(row,open_labels[i],NULL,G_CALLBACK(open_remote),GINT_TO_POINTER(i==0 ? OPEN_FILE:i==1 ? OPEN_FOLDER:PROJECT_READ));
 remote_ui.automatic=gtk_check_button_new_with_label("Auto upload on save");gtk_box_pack_start(GTK_BOX(remote_ui.files),remote_ui.automatic,FALSE,FALSE,0);row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(remote_ui.files),row,FALSE,FALSE,0);form_button(row,"Save remote",NULL,G_CALLBACK(save_remote),NULL);form_button(row,"Stop transfer",NULL,G_CALLBACK(stop_transfer),NULL);
 GtkWidget *separator=gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(remote_ui.files),separator,FALSE,FALSE,4);
 grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(grid),4);gtk_grid_set_column_spacing(GTK_GRID(grid),5);gtk_box_pack_start(GTK_BOX(remote_ui.files),grid,FALSE,FALSE,0);remote_ui.transfer_local=gtk_entry_new();form_row(grid,0,"Local source / new destination",remote_ui.transfer_local);gtk_widget_set_name(remote_ui.transfer_local,"insight-scp-local");remote_ui.transfer_remote=gtk_entry_new();form_row(grid,1,"Remote source / destination",remote_ui.transfer_remote);gtk_widget_set_name(remote_ui.transfer_remote,"insight-scp-remote");
 row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(remote_ui.files),row,FALSE,FALSE,0);remote_ui.transfer_folder=gtk_check_button_new_with_label("Folder");gtk_widget_set_name(remote_ui.transfer_folder,"insight-scp-folder");gtk_box_pack_start(GTK_BOX(row),remote_ui.transfer_folder,FALSE,FALSE,0);remote_ui.transfer_mode=gtk_combo_box_text_new();gtk_widget_set_name(remote_ui.transfer_mode,"insight-scp-packing");const gchar *packing[]={"No packing","gz (file)","tar.gz (file/folder)"};for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(remote_ui.transfer_mode),packing[i]);gtk_combo_box_set_active(GTK_COMBO_BOX(remote_ui.transfer_mode),0);gtk_box_pack_start(GTK_BOX(row),remote_ui.transfer_mode,FALSE,FALSE,0);form_button(row,"Choose…","insight-scp-choose",G_CALLBACK(scp_choose),NULL);
 row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(remote_ui.files),row,FALSE,FALSE,0);form_button(row,"Send","insight-scp-send",G_CALLBACK(scp_start),GINT_TO_POINTER(TRUE));form_button(row,"Receive","insight-scp-receive",G_CALLBACK(scp_start),GINT_TO_POINTER(FALSE));form_button(row,"Stop","insight-scp-stop",G_CALLBACK(stop_transfer),NULL);remote_ui.transfer_status=gtk_label_new("Archives are transferred as files. Receive never overwrites an existing local destination.");gtk_widget_set_name(remote_ui.transfer_status,"insight-scp-status");gtk_label_set_selectable(GTK_LABEL(remote_ui.transfer_status),TRUE);gtk_label_set_line_wrap(GTK_LABEL(remote_ui.transfer_status),TRUE);gtk_label_set_xalign(GTK_LABEL(remote_ui.transfer_status),0);gtk_box_pack_start(GTK_BOX(remote_ui.files),remote_ui.transfer_status,FALSE,FALSE,0);
 remote_ui.status=gtk_label_new("SSH/SFTP — configure a connection, transfer files, or add a terminal");gtk_widget_set_name(remote_ui.status,"insight-ssh-status");gtk_label_set_xalign(GTK_LABEL(remote_ui.status),0);gtk_label_set_line_wrap(GTK_LABEL(remote_ui.status),TRUE);gtk_label_set_line_wrap_mode(GTK_LABEL(remote_ui.status),PANGO_WRAP_WORD_CHAR);gtk_label_set_selectable(GTK_LABEL(remote_ui.status),TRUE);gtk_label_set_width_chars(GTK_LABEL(remote_ui.status),1);gtk_box_pack_start(GTK_BOX(remote_ui.page),remote_ui.status,FALSE,TRUE,0);
 GtkWidget *plus=gtk_button_new_from_icon_name("list-add-symbolic",GTK_ICON_SIZE_MENU);gtk_widget_set_name(plus,"insight-ssh-new-terminal");gtk_widget_set_tooltip_text(plus,"New SSH terminal");g_signal_connect(plus,"clicked",G_CALLBACK(session_add),NULL);gtk_notebook_set_action_widget(GTK_NOTEBOOK(remote_ui.tabs),plus,GTK_PACK_END);session_add(NULL,NULL);gtk_notebook_set_current_page(GTK_NOTEBOOK(remote_ui.tabs),0);
 insight_panel_help_attach(remote_ui.page,"panel-ssh.md"); gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),remote_ui.page,gtk_label_new("SSH Terminal")); gtk_widget_show_all(remote_ui.page);
 GKeyFile *profile=g_key_file_new(); g_key_file_load_from_file(profile,remote_ui.config,G_KEY_FILE_NONE,NULL); profile_load(profile); g_key_file_unref(profile);
 g_signal_connect(remote_ui.automatic,"toggled",G_CALLBACK(profile_toggle),NULL); g_signal_connect(remote_ui.save_password,"toggled",G_CALLBACK(profile_toggle),NULL);
 remote_ui.signals[0]=g_signal_connect_after(geany_object,"document-save",G_CALLBACK(saved_document),NULL); remote_ui.signals[1]=g_signal_connect_after(geany_object,"project-save",G_CALLBACK(project_changed),GINT_TO_POINTER(1)); remote_ui.signals[2]=g_signal_connect_after(geany_object,"project-open",G_CALLBACK(project_changed),NULL); remote_ui.signals[3]=g_signal_connect_after(geany_object,"project-close",G_CALLBACK(project_closed),NULL);
 if(app->project && app->project->file_name) { gchar *native=utils_get_locale_from_utf8(app->project->file_name); GKeyFile *keyfile=g_key_file_new(); if(g_key_file_load_from_file(keyfile,native,G_KEY_FILE_NONE,NULL)) project_changed(NULL,keyfile,NULL); g_key_file_unref(keyfile); g_free(native); }
 GtkWidget *menus[]={gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"file1"))),gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_project1")))};
 for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label("SSH files / folders / projects…"); gtk_container_add(GTK_CONTAINER(menus[i]),item); g_signal_connect(item,"activate",G_CALLBACK(show_remote),NULL); gtk_widget_show(item); }
}
void insight_remote_ui_finalize(void) {
 profile_save(); remote_ui.closing=TRUE; for(guint i=0;i<4;i++) if(remote_ui.signals[i]) g_signal_handler_disconnect(geany_object,remote_ui.signals[i]); stop_transfer(NULL,NULL); g_clear_pointer(&remote_ui.upload_versions,g_hash_table_destroy); for(guint i=0;i<remote_ui.sessions->len;i++) { ShellSession *s=g_ptr_array_index(remote_ui.sessions,i);session_disconnect(s);gtk_widget_destroy(s->page); }g_ptr_array_free(remote_ui.sessions,TRUE);g_ptr_array_free(remote_ui.roots,TRUE); g_free(remote_ui.config); g_free(remote_ui.directory);
}
