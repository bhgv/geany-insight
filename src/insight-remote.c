/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-remote.h"
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
static struct {
 GtkWidget *page,*host,*user,*port,*identity,*path,*status,*output,*input,*save_password,*automatic;
 GPtrArray *roots; GHashTable *upload_versions; GCancellable *snapshot_cancel; GQueue pending,shell_pending; gboolean shell_writing; Transfer *active; GSubprocess *shell; GCancellable *shell_cancel;
 gchar *shell_auth,*config,*directory; GString *tail; guint ansi; gboolean carriage,closing,loading;
 gulong signals[4]; gboolean (*workspace)(const char *);
} remote_ui;
static guint64 remote_epoch,upload_sequence;
static void profile_save(void);
static void transfer_run(Transfer *);
static void root_free(gpointer data) { RemoteRoot *r=data; if(!r) return; insight_remote_free(r->remote); g_free(r->directory); g_free(r->config); g_free(r); }
static RemoteRoot *root_copy(const RemoteRoot *r) { RemoteRoot *copy=g_new0(RemoteRoot,1); const InsightRemote *s=r->remote; copy->remote=insight_remote_new(s->host,s->user,s->port,s->identity,s->remote,s->local,NULL); copy->directory=g_strdup(r->directory); copy->config=g_strdup(r->config); copy->auto_upload=r->auto_upload; return copy; }
static void auth_cleanup(const char *path) { if(!path) return; GDir *dir=g_dir_open(path,0,NULL); if(dir) { const char *name; while((name=g_dir_read_name(dir))) { gchar *file=g_build_filename(path,name,NULL); g_remove(file); g_free(file); } g_dir_close(dir); } g_rmdir(path); }
static void transfer_free(Transfer *t) { if(!t) return; root_free(t->root); if(t->snapshot) g_remove(t->snapshot); auth_cleanup(t->auth); g_free(t->auth); g_free(t->origin); g_free(t->target); g_free(t->snapshot); if(t->project) g_key_file_unref(t->project); g_clear_object(&t->process); g_clear_object(&t->cancel); g_free(t); }
static void status(const char *message) { if(remote_ui.closing) return; gtk_label_set_text(GTK_LABEL(remote_ui.status),message); ui_set_statusbar(TRUE,"SSH: %s",message); }
static gchar *credentials_target(const InsightRemote *r) { gchar *identity=g_strdup_printf("%s:%u:%s",r->host,r->port,r->user),*hash=g_compute_checksum_for_string(G_CHECKSUM_SHA256,identity,-1); g_free(identity); return hash; }
static GSubprocessLauncher *launcher(RemoteRoot *root,gchar **auth) {
 GSubprocessLauncher *spawn=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE);
#ifdef G_OS_WIN32
 gchar *installation=win32_get_installation_dir(),*client_bin=g_build_filename(installation,"bin",NULL),*search_path=g_strconcat(client_bin,";",g_getenv("PATH") ? g_getenv("PATH") : "",NULL);
 g_subprocess_launcher_setenv(spawn,"PATH",search_path,TRUE); g_free(search_path); g_free(client_bin); g_free(installation);
#endif
 gchar *helper=g_find_program_in_path("geany-insight-askpass");
#ifdef G_OS_WIN32
 if(!helper) { gchar *prefix=win32_get_installation_dir(); helper=g_build_filename(prefix,"bin","geany-insight-askpass.exe",NULL); g_free(prefix); if(!g_file_test(helper,G_FILE_TEST_IS_EXECUTABLE)) g_clear_pointer(&helper,g_free); }
#else
 if(!helper) { gchar *executable=g_file_read_link("/proc/self/exe",NULL); if(executable) { gchar *bin=g_path_get_dirname(executable); helper=g_build_filename(bin,"geany-insight-askpass",NULL); g_free(bin); g_free(executable); if(!g_file_test(helper,G_FILE_TEST_IS_EXECUTABLE)) g_clear_pointer(&helper,g_free); } }
#endif
 if(helper) { g_subprocess_launcher_setenv(spawn,"SSH_ASKPASS",helper,TRUE); g_subprocess_launcher_setenv(spawn,"SSH_ASKPASS_REQUIRE","force",TRUE); }
 gchar *uuid=g_uuid_string_random(); *auth=g_build_filename(remote_ui.directory,uuid,NULL); g_mkdir_with_parents(*auth,0700); g_free(uuid);
 gchar *target=credentials_target(root->remote); g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_CONFIG",root->config,TRUE); g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_TARGET",target,TRUE); g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_AUTH_SESSION",*auth,TRUE);
 g_subprocess_launcher_setenv(spawn,"GEANY_INSIGHT_SSH_SAVE_PASSWORD",gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remote_ui.save_password)) ? "1" : "0",TRUE); g_free(target); g_free(helper); return spawn;
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
 if(batch) { GSubprocessLauncher *spawn=launcher(t->root,&t->auth); gchar **argv=insight_remote_argv(t->root->remote,TRUE); t->process=g_subprocess_launcher_spawnv(spawn,(const gchar *const *)argv,&error); g_strfreev(argv); g_object_unref(spawn); }
 if(!t->process) { status(error ? error->message : "SFTP unavailable"); g_clear_error(&error); g_free(batch); transfer_free(t); next_transfer(); return; }
 t->cancel=g_cancellable_new(); status(t->kind==UPLOAD ? "Uploading saved snapshot…" : "Downloading via SFTP…"); g_subprocess_communicate_utf8_async(t->process,batch,t->cancel,transfer_done,t); g_free(batch);
}
static RemoteRoot *form_root(const char *remote,const char *local,const char *directory,GError **error) {
 RemoteRoot *root=g_new0(RemoteRoot,1); root->remote=insight_remote_new(gtk_entry_get_text(GTK_ENTRY(remote_ui.host)),gtk_entry_get_text(GTK_ENTRY(remote_ui.user)),(guint)gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(remote_ui.port)),gtk_entry_get_text(GTK_ENTRY(remote_ui.identity)),remote,local,error);
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
static void stop_transfer(GtkWidget *widget,gpointer data) { (void)widget; (void)data; remote_epoch++; if(remote_ui.snapshot_cancel) { g_cancellable_cancel(remote_ui.snapshot_cancel); g_clear_object(&remote_ui.snapshot_cancel); } if(!remote_ui.closing) remote_ui.snapshot_cancel=g_cancellable_new(); if(remote_ui.upload_versions) g_hash_table_remove_all(remote_ui.upload_versions); if(remote_ui.active) { g_cancellable_cancel(remote_ui.active->cancel); g_subprocess_force_exit(remote_ui.active->process); } while(!g_queue_is_empty(&remote_ui.pending)) transfer_free(g_queue_pop_head(&remote_ui.pending)); status("Transfers stopped; local edits retained"); }
static void shell_append(const gchar *bytes,gsize size) {
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(remote_ui.output)); g_string_append_len(remote_ui.tail,bytes,(gssize)size); GString *plain=g_string_new(NULL); gsize consumed=0;
 for(const gchar *p=remote_ui.tail->str,*end=p+remote_ui.tail->len;p<end;) {
  guint8 c=(guint8)*p;
  if(remote_ui.ansi) { p++; consumed=(gsize)(p-remote_ui.tail->str); if(remote_ui.ansi==1) remote_ui.ansi=c=='[' ? 2 : (c==']' ? 3 : 0); else if(remote_ui.ansi==2 && c>=0x40 && c<=0x7e) remote_ui.ansi=0; else if(remote_ui.ansi==3 && c==7) remote_ui.ansi=0; else if(remote_ui.ansi==3 && c==27) remote_ui.ansi=4; else if(remote_ui.ansi==4) remote_ui.ansi=c=='\\' ? 0 : 3; continue; }
  if(c==27) { remote_ui.ansi=1; p++; consumed=(gsize)(p-remote_ui.tail->str); continue; }
  if(c=='\r') { remote_ui.carriage=TRUE; p++; consumed=(gsize)(p-remote_ui.tail->str); continue; }
  if(remote_ui.carriage) { if(c!='\n') { if(plain->len) { GtkTextIter it; gtk_text_buffer_get_end_iter(buffer,&it); gtk_text_buffer_insert(buffer,&it,plain->str,(gint)plain->len); g_string_truncate(plain,0); } GtkTextIter a,b; gtk_text_buffer_get_end_iter(buffer,&b); a=b; gtk_text_iter_set_line_offset(&a,0); gtk_text_buffer_delete(buffer,&a,&b); } remote_ui.carriage=FALSE; }
  if(c==8) { if(plain->len) { gchar *previous=g_utf8_find_prev_char(plain->str,plain->str+plain->len); if(previous) g_string_truncate(plain,(gsize)(previous-plain->str)); } else { GtkTextIter a,b; gtk_text_buffer_get_end_iter(buffer,&b); a=b; if(gtk_text_iter_backward_char(&a)) gtk_text_buffer_delete(buffer,&a,&b); } p++; consumed=(gsize)(p-remote_ui.tail->str); continue; }
  gunichar character=g_utf8_get_char_validated(p,(gssize)(end-p)); if(character==(gunichar)-2) break;
  if(character==(gunichar)-1) { g_string_append(plain,"\357\277\275"); p++; } else { const gchar *next=g_utf8_next_char(p); if(character>=32 || character=='\n' || character=='\t') g_string_append_len(plain,p,next-p); p=next; }
  consumed=(gsize)(p-remote_ui.tail->str);
 }
 g_string_erase(remote_ui.tail,0,(gssize)consumed); GtkTextIter it; gtk_text_buffer_get_end_iter(buffer,&it); gtk_text_buffer_insert(buffer,&it,plain->str,(gint)plain->len); g_string_free(plain,TRUE);
 if(gtk_text_buffer_get_char_count(buffer)>1000000) { GtkTextIter a,b; gtk_text_buffer_get_start_iter(buffer,&a); b=a; gtk_text_iter_forward_chars(&b,100000); gtk_text_buffer_delete(buffer,&a,&b); }
 gtk_text_buffer_get_end_iter(buffer,&it); gtk_text_buffer_place_cursor(buffer,&it); gtk_text_view_scroll_to_iter(GTK_TEXT_VIEW(remote_ui.output),&it,0,FALSE,0,0);
}
static void shell_read(GObject *object,GAsyncResult *result,gpointer data) {
 GSubprocess *process=data; GError *error=NULL; GBytes *bytes=g_input_stream_read_bytes_finish(G_INPUT_STREAM(object),result,&error);
 if(!remote_ui.closing && remote_ui.shell==process && bytes && g_bytes_get_size(bytes)) { gsize size; const gchar *text=g_bytes_get_data(bytes,&size); shell_append(text,size); g_input_stream_read_bytes_async(g_subprocess_get_stdout_pipe(process),8192,G_PRIORITY_DEFAULT,remote_ui.shell_cancel,shell_read,g_object_ref(process)); }
 else if(!remote_ui.closing && remote_ui.shell==process) { status(error ? error->message : "SSH shell disconnected"); g_cancellable_cancel(remote_ui.shell_cancel); g_clear_object(&remote_ui.shell_cancel); g_clear_object(&remote_ui.shell); auth_cleanup(remote_ui.shell_auth); g_clear_pointer(&remote_ui.shell_auth,g_free); }
 if(bytes) g_bytes_unref(bytes);
 g_clear_error(&error); g_object_unref(process);
}
static void shell_finished(GObject *object,GAsyncResult *result,gpointer data) {
 (void)data; g_subprocess_wait_finish(G_SUBPROCESS(object),result,NULL);
 if(!remote_ui.closing && remote_ui.shell==G_SUBPROCESS(object)) status("SSH shell exited; draining output…");
}
static void shell_disconnect(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; if(remote_ui.shell) { g_cancellable_cancel(remote_ui.shell_cancel); g_subprocess_force_exit(remote_ui.shell); g_clear_object(&remote_ui.shell); g_clear_object(&remote_ui.shell_cancel); auth_cleanup(remote_ui.shell_auth); g_clear_pointer(&remote_ui.shell_auth,g_free); } if(!remote_ui.closing) status("SSH shell disconnected");
 remote_ui.shell_writing=FALSE; g_queue_clear_full(&remote_ui.shell_pending,g_free);
}
static void shell_connect(GtkWidget *widget,gpointer data) {
 (void)widget; (void)data; profile_save(); shell_disconnect(NULL,NULL); GError *error=NULL; RemoteRoot *root=form_root("/",remote_ui.directory,remote_ui.directory,&error);
 if(!root) { status(error ? error->message : "Invalid SSH connection"); g_clear_error(&error); return; }
 GSubprocessLauncher *spawn=launcher(root,&remote_ui.shell_auth); g_subprocess_launcher_setenv(spawn,"TERM","dumb",TRUE); gchar **argv=insight_remote_argv(root->remote,FALSE); remote_ui.shell=g_subprocess_launcher_spawnv(spawn,(const gchar *const *)argv,&error); g_strfreev(argv); g_object_unref(spawn); root_free(root);
 if(!remote_ui.shell) { status(error ? error->message : "OpenSSH unavailable"); g_clear_error(&error); auth_cleanup(remote_ui.shell_auth); g_clear_pointer(&remote_ui.shell_auth,g_free); return; }
 remote_ui.shell_cancel=g_cancellable_new(); remote_ui.ansi=0; remote_ui.carriage=FALSE; g_string_truncate(remote_ui.tail,0);
 g_input_stream_read_bytes_async(g_subprocess_get_stdout_pipe(remote_ui.shell),8192,G_PRIORITY_DEFAULT,remote_ui.shell_cancel,shell_read,g_object_ref(remote_ui.shell)); g_subprocess_wait_async(remote_ui.shell,NULL,shell_finished,NULL);
 status("SSH shell connected: line input, Ctrl+C interrupts; full-screen TUI is not emulated"); gtk_widget_grab_focus(remote_ui.input);
}
typedef struct { GSubprocess *process; gchar *text; } ShellWrite;
static void shell_flush(void);
static void shell_write_done(GObject *object,GAsyncResult *result,gpointer data) {
 ShellWrite *write=data; GError *error=NULL; gboolean ok=g_output_stream_write_all_finish(G_OUTPUT_STREAM(object),result,NULL,&error);
 if(!remote_ui.closing && remote_ui.shell==write->process) { remote_ui.shell_writing=FALSE; if(!ok) status(error ? error->message : "SSH shell write failed"); else shell_flush(); }
 g_clear_error(&error); g_object_unref(write->process); g_free(write->text); g_free(write);
}
static void shell_flush(void) {
 if(!remote_ui.shell || remote_ui.shell_writing || g_queue_is_empty(&remote_ui.shell_pending)) return;
 ShellWrite *write=g_new0(ShellWrite,1); write->process=g_object_ref(remote_ui.shell); write->text=g_queue_pop_head(&remote_ui.shell_pending); remote_ui.shell_writing=TRUE;
 g_output_stream_write_all_async(g_subprocess_get_stdin_pipe(remote_ui.shell),write->text,strlen(write->text),G_PRIORITY_DEFAULT,remote_ui.shell_cancel,shell_write_done,write);
}
static void shell_send(GtkWidget *widget,gpointer data) {
 (void)widget; if(!remote_ui.shell) { status("Connect SSH shell first"); return; }
 gboolean interrupt=GPOINTER_TO_INT(data)!=0; gchar *text=interrupt ? g_strdup("\003") : g_strdup_printf("%s\n",gtk_entry_get_text(GTK_ENTRY(remote_ui.input)));
 g_queue_push_tail(&remote_ui.shell_pending,text); shell_flush(); if(!interrupt) gtk_entry_set_text(GTK_ENTRY(remote_ui.input),"");
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
void insight_remote_ui_init(gboolean (*workspace)(const char *)) {
 remote_epoch++; remote_ui.upload_versions=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free); remote_ui.snapshot_cancel=g_cancellable_new();
 remote_ui.workspace=workspace; remote_ui.roots=g_ptr_array_new_with_free_func(root_free); remote_ui.tail=g_string_new(NULL); remote_ui.directory=g_build_filename(app->configdir,"insight","remote",NULL); g_mkdir_with_parents(remote_ui.directory,0700); remote_ui.config=g_build_filename(app->configdir,"insight","ssh.conf",NULL);
 restore_roots();
 remote_ui.page=gtk_box_new(GTK_ORIENTATION_VERTICAL,2); GtkWidget *connection=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); remote_ui.host=gtk_entry_new(); remote_ui.user=gtk_entry_new(); remote_ui.port=gtk_spin_button_new_with_range(1,65535,1); remote_ui.identity=gtk_entry_new(); gtk_spin_button_set_value(GTK_SPIN_BUTTON(remote_ui.port),22);
 GtkWidget *fields[]={remote_ui.host,remote_ui.user,remote_ui.identity}; const char *hints[]={"Host or SSH config alias","Login (optional)","Identity file (optional)"}; for(guint i=0;i<3;i++) { gtk_entry_set_placeholder_text(GTK_ENTRY(fields[i]),hints[i]); gtk_entry_set_width_chars(GTK_ENTRY(fields[i]),i==0 ? 18 : 12); gtk_box_pack_start(GTK_BOX(connection),fields[i],TRUE,TRUE,0); } gtk_box_pack_start(GTK_BOX(connection),remote_ui.port,FALSE,FALSE,0);
 GtkWidget *key=gtk_button_new_with_label("Key…"); gtk_box_pack_start(GTK_BOX(connection),key,FALSE,FALSE,0); g_signal_connect(key,"clicked",G_CALLBACK(identity_choose),NULL);
 remote_ui.save_password=gtk_check_button_new_with_label("Save password"); gtk_widget_set_tooltip_text(remote_ui.save_password,"Opt-in: save login password / key passphrase as text in the project config (global SSH profile when no project is open)"); remote_ui.automatic=gtk_check_button_new_with_label("Auto upload on save"); gtk_box_pack_start(GTK_BOX(connection),remote_ui.save_password,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(connection),remote_ui.automatic,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(remote_ui.page),connection,FALSE,FALSE,0);
 GtkWidget *files=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); remote_ui.path=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(remote_ui.path),"Absolute remote file, folder or .geany project path"); gtk_box_pack_start(GTK_BOX(files),remote_ui.path,TRUE,TRUE,0);
 const char *labels[]={"Open file","Open folder","Open project","Save remote","Stop transfer"}; for(guint i=0;i<5;i++) { GtkWidget *button=gtk_button_new_with_label(labels[i]); gtk_box_pack_start(GTK_BOX(files),button,FALSE,FALSE,0); g_signal_connect(button,"clicked",i<3 ? G_CALLBACK(open_remote) : (i==3 ? G_CALLBACK(save_remote) : G_CALLBACK(stop_transfer)),GINT_TO_POINTER(i==2 ? PROJECT_READ : (i==1 ? OPEN_FOLDER : OPEN_FILE))); } gtk_box_pack_start(GTK_BOX(remote_ui.page),files,FALSE,FALSE,0);
 GtkWidget *shell=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); const char *shell_labels[]={"Connect shell","Disconnect","Forget saved password"}; for(guint i=0;i<3;i++) { GtkWidget *button=gtk_button_new_with_label(shell_labels[i]); gtk_box_pack_start(GTK_BOX(shell),button,FALSE,FALSE,0); g_signal_connect(button,"clicked",i==0 ? G_CALLBACK(shell_connect) : (i==1 ? G_CALLBACK(shell_disconnect) : G_CALLBACK(forget_password)),NULL); } remote_ui.status=gtk_label_new("SSH/SFTP — OpenSSH config, key/agent or password; line-oriented remote shell"); gtk_label_set_ellipsize(GTK_LABEL(remote_ui.status),PANGO_ELLIPSIZE_END); gtk_box_pack_start(GTK_BOX(shell),remote_ui.status,TRUE,TRUE,0); gtk_box_pack_start(GTK_BOX(remote_ui.page),shell,FALSE,FALSE,0);
 remote_ui.output=gtk_text_view_new(); gtk_text_view_set_editable(GTK_TEXT_VIEW(remote_ui.output),FALSE); gtk_text_view_set_monospace(GTK_TEXT_VIEW(remote_ui.output),TRUE); GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL); gtk_container_add(GTK_CONTAINER(scroll),remote_ui.output); gtk_box_pack_start(GTK_BOX(remote_ui.page),scroll,TRUE,TRUE,0);
 GtkWidget *input=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3); remote_ui.input=gtk_entry_new(); gtk_entry_set_placeholder_text(GTK_ENTRY(remote_ui.input),"SSH shell input — Enter sends a line, Ctrl+C interrupts"); gtk_box_pack_start(GTK_BOX(input),remote_ui.input,TRUE,TRUE,0); GtkWidget *send=gtk_button_new_with_label("Send"),*interrupt=gtk_button_new_with_label("Ctrl+C"); gtk_box_pack_start(GTK_BOX(input),send,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(input),interrupt,FALSE,FALSE,0); gtk_box_pack_start(GTK_BOX(remote_ui.page),input,FALSE,FALSE,0); g_signal_connect(send,"clicked",G_CALLBACK(shell_send),NULL); g_signal_connect(interrupt,"clicked",G_CALLBACK(shell_send),GINT_TO_POINTER(1)); g_signal_connect(remote_ui.input,"activate",G_CALLBACK(shell_send),NULL); g_signal_connect(remote_ui.input,"key-press-event",G_CALLBACK(shell_key),NULL);
 insight_panel_help_attach(remote_ui.page,"panel-ssh.md"); gtk_notebook_append_page(GTK_NOTEBOOK(main_widgets.message_window_notebook),remote_ui.page,gtk_label_new("SSH Terminal")); gtk_widget_show_all(remote_ui.page);
 GKeyFile *profile=g_key_file_new(); g_key_file_load_from_file(profile,remote_ui.config,G_KEY_FILE_NONE,NULL); profile_load(profile); g_key_file_unref(profile);
 g_signal_connect(remote_ui.automatic,"toggled",G_CALLBACK(profile_toggle),NULL); g_signal_connect(remote_ui.save_password,"toggled",G_CALLBACK(profile_toggle),NULL);
 remote_ui.signals[0]=g_signal_connect_after(geany_object,"document-save",G_CALLBACK(saved_document),NULL); remote_ui.signals[1]=g_signal_connect_after(geany_object,"project-save",G_CALLBACK(project_changed),GINT_TO_POINTER(1)); remote_ui.signals[2]=g_signal_connect_after(geany_object,"project-open",G_CALLBACK(project_changed),NULL); remote_ui.signals[3]=g_signal_connect_after(geany_object,"project-close",G_CALLBACK(project_closed),NULL);
 if(app->project && app->project->file_name) { gchar *native=utils_get_locale_from_utf8(app->project->file_name); GKeyFile *keyfile=g_key_file_new(); if(g_key_file_load_from_file(keyfile,native,G_KEY_FILE_NONE,NULL)) project_changed(NULL,keyfile,NULL); g_key_file_unref(keyfile); g_free(native); }
 GtkWidget *menus[]={gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"file1"))),gtk_menu_item_get_submenu(GTK_MENU_ITEM(ui_lookup_widget(main_widgets.window,"menu_project1")))};
 for(guint i=0;i<2;i++) { GtkWidget *item=gtk_menu_item_new_with_label("SSH files / folders / projects…"); gtk_container_add(GTK_CONTAINER(menus[i]),item); g_signal_connect(item,"activate",G_CALLBACK(show_remote),NULL); gtk_widget_show(item); }
}
void insight_remote_ui_finalize(void) {
 profile_save(); remote_ui.closing=TRUE; for(guint i=0;i<4;i++) if(remote_ui.signals[i]) g_signal_handler_disconnect(geany_object,remote_ui.signals[i]); stop_transfer(NULL,NULL); g_clear_pointer(&remote_ui.upload_versions,g_hash_table_destroy); shell_disconnect(NULL,NULL); g_ptr_array_free(remote_ui.roots,TRUE); g_string_free(remote_ui.tail,TRUE); g_free(remote_ui.config); g_free(remote_ui.directory);
}
