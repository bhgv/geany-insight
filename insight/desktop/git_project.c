/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "git_project.h"
#include "glib_compat.h"
#include <glib/gstdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <sys/stat.h>

GBytes *insight_git_run(const gchar *root,const gchar *index,const gchar *const *args,GCancellable *cancel,GError **error)
{
 GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE);
 if(root) g_subprocess_launcher_set_cwd(launcher,root);
 /* An inherited Git environment must not redirect operations to another repository. */
 const gchar *unset[]={"GIT_DIR","GIT_WORK_TREE","GIT_INDEX_FILE","GIT_COMMON_DIR","GIT_PREFIX","GIT_OBJECT_DIRECTORY","GIT_ALTERNATE_OBJECT_DIRECTORIES",NULL};
 for(guint i=0;unset[i];i++) g_subprocess_launcher_unsetenv(launcher,unset[i]);
 g_subprocess_launcher_setenv(launcher,"GIT_TERMINAL_PROMPT","0",TRUE);
 g_subprocess_launcher_setenv(launcher,"GIT_LITERAL_PATHSPECS","1",TRUE);
 g_subprocess_launcher_setenv(launcher,"GIT_OPTIONAL_LOCKS","0",TRUE);
 if(index) g_subprocess_launcher_setenv(launcher,"GIT_INDEX_FILE",index,TRUE);
 GPtrArray *argv=g_ptr_array_new();g_ptr_array_add(argv,"git");
 g_ptr_array_add(argv,"-c");g_ptr_array_add(argv,"core.quotepath=false");
 for(guint i=0;args[i];i++) g_ptr_array_add(argv,(gpointer)args[i]);
 g_ptr_array_add(argv,NULL);
 GSubprocess *process=g_subprocess_launcher_spawnv(launcher,(const gchar *const *)argv->pdata,error);
 g_ptr_array_free(argv,TRUE);g_object_unref(launcher);
 if(!process) return NULL;
 GBytes *out=NULL,*err=NULL;
 if(!g_subprocess_communicate(process,NULL,cancel,&out,&err,error)) {
  g_subprocess_force_exit(process);g_subprocess_wait(process,NULL,NULL);g_clear_pointer(&out,g_bytes_unref);
 } else if(!g_subprocess_get_successful(process)) {
  gsize n;const gchar *p=g_bytes_get_data(err,&n);gchar *text=n ? g_utf8_make_valid(p,MIN(n,8192)) : g_strdup("");
  g_set_error(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Git: %s",*text ? text : "command failed");
  g_free(text);g_clear_pointer(&out,g_bytes_unref);
 }
 g_clear_pointer(&err,g_bytes_unref);g_object_unref(process);return out;
}
static gboolean command(const gchar *root,const gchar *index,const gchar *const *args,GError **error)
{ GBytes *b=insight_git_run(root,index,args,NULL,error);if(!b) return FALSE;g_bytes_unref(b);return TRUE; }
static void change_free(gpointer p) { InsightGitChange *c=p;g_free(c->path);g_free(c->snapshot);g_free(c); }
static gchar *fingerprint(const gchar *root,const gchar *path,const gchar *staged,GCancellable *cancel)
{
 GChecksum *sum=g_checksum_new(G_CHECKSUM_SHA256);g_checksum_update(sum,(const guchar *)staged,strlen(staged));
 gchar *name=g_build_filename(root,path,NULL);GStatBuf st;
 if(!g_lstat(name,&st)) {
  gchar *mode=g_strdup_printf("%u",(guint)st.st_mode);g_checksum_update(sum,(const guchar *)mode,strlen(mode));g_free(mode);
  if(g_file_test(name,G_FILE_TEST_IS_SYMLINK)) { gchar *target=g_file_read_link(name,NULL);if(target) { g_checksum_update(sum,(const guchar *)target,strlen(target));g_free(target); } }
  else if(S_ISREG(st.st_mode)) { GFile *file=g_file_new_for_path(name);GFileInputStream *stream=g_file_read(file,cancel,NULL);if(stream) { guchar buffer[65536];gssize n;while((n=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,cancel,NULL))>0) g_checksum_update(sum,buffer,n);g_object_unref(stream); }g_object_unref(file); }
 }
 gchar *result=g_strdup(g_checksum_get_string(sum));g_checksum_free(sum);g_free(name);return result;
}
static gint change_sort(gconstpointer a,gconstpointer b)
{ return strcmp((*(InsightGitChange *const *)a)->path,(*(InsightGitChange *const *)b)->path); }
GPtrArray *insight_git_status(const gchar *root,GCancellable *cancel,GError **error)
{
 /* Disable rename folding: both ends remain independently selectable, including
  * names containing newlines. Porcelain -z is never parsed as display text. */
 const gchar *args[]={"status","--porcelain=v1","-z","--untracked-files=all","--no-renames",NULL};
 GBytes *b=insight_git_run(root,NULL,args,cancel,error);if(!b) return NULL;
 const gchar *stage_args[]={"ls-files","--stage","-z",NULL};GBytes *index=insight_git_run(root,NULL,stage_args,cancel,error);
 if(!index) { g_bytes_unref(b);return NULL; }
 gchar *staged=g_compute_checksum_for_bytes(G_CHECKSUM_SHA256,index);g_bytes_unref(index);
 gsize n;const gchar *p=g_bytes_get_data(b,&n),*end=p+n;
 GPtrArray *rows=g_ptr_array_new_with_free_func(change_free);
 GHashTable *seen=g_hash_table_new(g_str_hash,g_str_equal);
 while(p<end) {
  const gchar *nul=memchr(p,0,end-p);
  if(!nul || nul-p<4 || p[2]!=' ') { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid Git status output");g_ptr_array_unref(rows);rows=NULL;break; }
  InsightGitChange *c=g_new0(InsightGitChange,1);c->x=p[0];c->y=p[1];c->path=g_strndup(p+3,nul-p-3);
  gboolean duplicate=g_hash_table_contains(seen,c->path);
  if(duplicate) change_free(c);else { g_hash_table_add(seen,c->path);c->snapshot=fingerprint(root,c->path,staged,cancel);g_ptr_array_add(rows,c); }p=nul+1;
 }
 g_hash_table_unref(seen);g_free(staged);g_bytes_unref(b);if(rows) g_ptr_array_sort(rows,change_sort);return rows;
}
GPtrArray *insight_git_files(const gchar *root,GCancellable *cancel,GError **error)
{
 const gchar *args[]={"ls-files","--cached","-z",NULL};GBytes *b=insight_git_run(root,NULL,args,cancel,error);if(!b) return NULL;
 gsize n;const gchar *p=g_bytes_get_data(b,&n),*end=p+n;GPtrArray *files=g_ptr_array_new_with_free_func(g_free);
 while(p<end) { const gchar *nul=memchr(p,0,end-p);if(!nul) break;g_ptr_array_add(files,g_strndup(p,nul-p));p=nul+1; }
 g_bytes_unref(b);return files;
}
static gchar *path_file(GPtrArray *paths,GError **error)
{
 gchar *name=NULL;gint fd=g_file_open_tmp("insight-git-paths-XXXXXX",&name,error);if(fd<0) return NULL;close(fd);
 GString *s=g_string_new(NULL);for(guint i=0;i<paths->len;i++) { const gchar *p=g_ptr_array_index(paths,i);g_string_append_len(s,p,strlen(p)+1); }
 if(!g_file_set_contents(name,s->str,s->len,error)) { g_unlink(name);g_clear_pointer(&name,g_free); }
 g_string_free(s,TRUE);return name;
}
gboolean insight_git_paths(const gchar *root,GPtrArray *paths,gboolean add,GCancellable *cancel,GError **error)
{
 if(!paths->len) return TRUE;
 gchar *file=path_file(paths,error);if(!file) return FALSE;
 gchar *option=g_strconcat("--pathspec-from-file=",file,NULL);
 const gchar *args[]={add ? "add" : "rm",add ? "--ignore-removal" : "--cached",add ? "--verbose" : "-r",option,"--pathspec-file-nul",NULL};
 GBytes *out=insight_git_run(root,NULL,args,cancel,error);gboolean ok=out!=NULL;
 g_clear_pointer(&out,g_bytes_unref);g_unlink(file);g_free(file);g_free(option);return ok;
}
static gchar *git_path(const gchar *root,const gchar *path,GError **error)
{
 const gchar *args[]={"rev-parse","--git-path",path,NULL};GBytes *b=insight_git_run(root,NULL,args,NULL,error);if(!b) return NULL;
 gsize n;const gchar *p=g_bytes_get_data(b,&n);while(n && (p[n-1]=='\n' || p[n-1]=='\r')) n--;
 gchar *raw=g_strndup(p,n),*result=insight_canonicalize_filename(raw,root);g_free(raw);g_bytes_unref(b);return result;
}
gboolean insight_git_commit(const gchar *root,GPtrArray *changes,const gchar *message,GError **error)
{
 if(!changes->len || !message || !*message) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Select files and enter a commit message");return FALSE; }
 /* Partial commits during merge/sequencer operations are unsafe. */
 const gchar *states[]={"MERGE_HEAD","CHERRY_PICK_HEAD","REVERT_HEAD","rebase-merge","rebase-apply","sequencer",NULL};
 for(guint i=0;states[i];i++) { gchar *p=git_path(root,states[i],error);if(!p) return FALSE;gboolean exists=g_file_test(p,G_FILE_TEST_EXISTS);g_free(p);if(exists) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Finish or abort the active merge, rebase or cherry-pick before a partial commit");return FALSE; } }
 gchar *index=git_path(root,"index",error);if(!index) return FALSE;
 gchar *lock=g_strconcat(index,".lock",NULL),*temp=g_strconcat(index,".insight-XXXXXX",NULL),*reconcile=g_strconcat(index,".insight-reconcile-XXXXXX",NULL);
 gint fd=g_open(lock,O_WRONLY|O_CREAT|O_EXCL,0600);gboolean ok=FALSE,committed=FALSE;
 if(fd<0) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Git index is locked or not writable; another Git operation may be running");goto done; }close(fd);
 GPtrArray *current=insight_git_status(root,NULL,error);
 if(!current) goto unlock;
 gboolean fresh=TRUE;
 for(guint i=0;i<changes->len;i++) { InsightGitChange *chosen=g_ptr_array_index(changes,i);gboolean found=FALSE;for(guint k=0;k<current->len;k++) { InsightGitChange *now=g_ptr_array_index(current,k);if(!strcmp(chosen->path,now->path) && chosen->x==now->x && chosen->y==now->y && !g_strcmp0(chosen->snapshot,now->snapshot)) found=TRUE; }if(!found) fresh=FALSE; }
 g_ptr_array_unref(current);
 if(!fresh) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Files or Git staging changed after review. Reopen the commit dialog.");goto unlock; }
 fd=g_mkstemp(temp);if(fd<0) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create private Git index");goto unlock; }close(fd);g_unlink(temp);
 fd=g_mkstemp(reconcile);if(fd<0) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot create reconciliation index");goto unlock; }close(fd);g_unlink(reconcile);
 gchar *original=NULL;gsize length=0;
 if(g_file_test(index,G_FILE_TEST_EXISTS)) {
  if(!g_file_get_contents(index,&original,&length,error)) goto unlock;
  gboolean copied=g_file_set_contents(reconcile,original,length,error);g_free(original);if(!copied) goto unlock;
 }
 const gchar *head[]={"rev-parse","--verify","HEAD",NULL};GBytes *exists=insight_git_run(root,NULL,head,NULL,NULL);
 const gchar *read[]={"read-tree",exists ? "HEAD" : "--empty",NULL};g_clear_pointer(&exists,g_bytes_unref);
 if(!command(root,temp,read,error)) goto unlock;
 GPtrArray *paths=g_ptr_array_new();GPtrArray *adds=g_ptr_array_new();
 for(guint i=0;i<changes->len;i++) {
  InsightGitChange *c=g_ptr_array_index(changes,i);g_ptr_array_add(paths,c->path);
  gchar *full=g_build_filename(root,c->path,NULL);gboolean directory=g_file_test(full,G_FILE_TEST_IS_DIR) && !g_file_test(full,G_FILE_TEST_IS_SYMLINK);g_free(full);
  if(directory) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Commit nested repositories and submodules in their own Git project first");ok=FALSE;goto selected_done; }
  if(c->x=='U' || c->y=='U' || (c->x=='A' && c->y=='A') || (c->x=='D' && c->y=='D')) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_BUSY,"Resolve conflicted files before committing");ok=FALSE;goto selected_done; }
  if(c->x=='D' || c->y=='D') { const gchar *rm[]={"update-index","--force-remove","--",c->path,NULL};if(!command(root,temp,rm,error)) { ok=FALSE;goto selected_done; } }
  else g_ptr_array_add(adds,c->path);
 }
 gchar *file=path_file(adds,error);if(!file) { ok=FALSE;goto selected_done; }
 gchar *option=g_strconcat("--pathspec-from-file=",file,NULL);
 const gchar *add[]={"add","--all",option,"--pathspec-file-nul",NULL};
 ok=!adds->len || command(root,temp,add,error);g_unlink(file);g_free(file);g_free(option);if(!ok) goto selected_done;
 const gchar *commit[]={"commit","-m",message,NULL};
 ok=command(root,temp,commit,error);if(!ok) goto selected_done;committed=TRUE;
 file=path_file(paths,error);if(!file) { ok=FALSE;goto selected_done; }option=g_strconcat("--pathspec-from-file=",file,NULL);
 const gchar *reset[]={"reset","-q","HEAD",option,"--pathspec-file-nul",NULL};
 ok=command(root,reconcile,reset,error);g_unlink(file);g_free(file);g_free(option);
 if(ok) { gchar *data=NULL;gsize size;ok=g_file_get_contents(reconcile,&data,&size,error);if(ok) ok=g_file_set_contents(lock,data,size,error);g_free(data);if(ok && g_rename(lock,index)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Cannot replace Git index");ok=FALSE; } }
selected_done:
 g_ptr_array_unref(paths);g_ptr_array_unref(adds);
unlock:
 g_unlink(lock);g_unlink(temp);g_unlink(reconcile);
 if(committed && !ok && error && *error) g_prefix_error(error,"Commit was created, but refreshing the index failed. Inspect Git status before retrying. ");
done:
 g_free(index);g_free(lock);g_free(temp);g_free(reconcile);return ok;
}
gchar *insight_git_message(GPtrArray *changes)
{
 guint added=0,removed=0,modified=0;GString *s=g_string_new(NULL);
 for(guint i=0;i<changes->len;i++) { InsightGitChange *c=g_ptr_array_index(changes,i);if(c->x=='?' || c->x=='A') added++;else if(c->x=='D' || c->y=='D') removed++;else modified++; }
 g_string_append_printf(s,"Update project: %u added, %u changed, %u removed\n",added,modified,removed);
 for(guint i=0;i<changes->len;i++) { InsightGitChange *c=g_ptr_array_index(changes,i);gchar *name=g_filename_display_name(c->path);g_string_append_printf(s,"\n%c%c %s",c->x,c->y,name);g_free(name); }
 return g_string_free(s,FALSE);
}
