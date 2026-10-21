/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../desktop/git_history.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root;
static gchar *git(const gchar *const *args)
{
 GSubprocessLauncher *l=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE);g_subprocess_launcher_set_cwd(l,root);GPtrArray *a=g_ptr_array_new();g_ptr_array_add(a,"git");for(guint i=0;args[i];i++) g_ptr_array_add(a,(gpointer)args[i]);g_ptr_array_add(a,NULL);GError *e=NULL;GSubprocess *p=g_subprocess_launcher_spawnv(l,(const gchar *const *)a->pdata,&e);g_assert_no_error(e);gchar *out=NULL,*err=NULL;g_assert_true(g_subprocess_communicate_utf8(p,NULL,NULL,&out,&err,&e));g_assert_no_error(e);if(!g_subprocess_get_successful(p)) g_error("git failed: %s",err);g_free(err);g_object_unref(p);g_object_unref(l);g_ptr_array_unref(a);return out;
}
#define GIT(...) do { const gchar *a[]={__VA_ARGS__,NULL};g_free(git(a)); } while(0)
static gchar *head(void) { const gchar *a[]={"rev-parse","HEAD",NULL};gchar *s=git(a);g_strchomp(s);return s; }
static void write_file(const gchar *name,const gchar *text,gssize n) { gchar *p=g_build_filename(root,name,NULL);g_assert_true(g_file_set_contents(p,text,n,NULL));g_free(p); }
int main(void)
{
 root=g_dir_make_tmp("git-history-XXXXXX",NULL);g_assert_nonnull(root);GIT("init","--initial-branch=main");GIT("config","user.name","History Test");GIT("config","user.email","history@example.invalid");
 GError *e=NULL;GPtrArray *refs=insight_git_history_refs(root,NULL,&e);g_assert_no_error(e);g_assert_cmpuint(refs->len,==,0);g_ptr_array_unref(refs);
 write_file("sample.c","// original\nint value = 1;\n",-1);write_file("empty.c","",0);GIT("add",".");GIT("commit","-m","Initial source\n\nFull initial comment.");gchar *initial=head();
 gchar *found=insight_git_history_root(root,NULL,&e);g_assert_no_error(e);g_assert_cmpstr(found,==,root);g_free(found);
 InsightGitRevision *rev=insight_git_history_revision(root,initial,NULL,&e);g_assert_no_error(e);g_assert_cmpuint(rev->parents->len,==,0);g_assert_nonnull(strstr(rev->message,"Full initial comment."));insight_git_revision_free(rev);
 GPtrArray *files=insight_git_history_delta(root,initial,NULL,NULL,&e);g_assert_no_error(e);g_assert_cmpuint(files->len,==,2);
 for(guint i=0;i<files->len;i++) { InsightGitDelta *f=g_ptr_array_index(files,i);gchar *s=insight_git_history_text(root,f->old_oid,f->old_mode,NULL,NULL,&e);g_assert_no_error(e);g_assert_cmpstr(s,==,"");g_free(s);s=insight_git_history_text(root,f->new_oid,f->new_mode,NULL,NULL,&e);g_assert_no_error(e);g_assert_nonnull(s);g_free(s); }g_ptr_array_unref(files);
 GIT("branch","topic");write_file("sample.c","// revised\nint value = 2;\n",-1);write_file("line\nbreak.c","int added;\n",-1);write_file("binary.dat","a\0b",3);gchar *large=g_malloc(INSIGHT_GIT_HISTORY_TEXT_LIMIT+1);memset(large,'x',INSIGHT_GIT_HISTORY_TEXT_LIMIT+1);write_file("large.c",large,INSIGHT_GIT_HISTORY_TEXT_LIMIT+1);g_free(large);GIT("add",".");GIT("commit","-m","Revise source\n\nDetailed change comment.");gchar *second=head();
 files=insight_git_history_delta(root,second,initial,NULL,&e);g_assert_no_error(e);g_assert_cmpuint(files->len,==,4);
 for(guint i=0;i<files->len;i++) { InsightGitDelta *f=g_ptr_array_index(files,i);gchar *s=insight_git_history_text(root,f->new_oid,f->new_mode,NULL,NULL,&e);if(!strcmp(f->path,"binary.dat") || !strcmp(f->path,"large.c")) { g_assert_null(s);g_assert_nonnull(e);g_clear_error(&e); }else { g_assert_no_error(e);g_assert_nonnull(s);g_free(s); } }g_ptr_array_unref(files);
 GIT("checkout","topic");write_file("topic.c","int topic;\n",-1);GIT("add","topic.c");GIT("commit","-m","Topic feature");GIT("checkout","main");GIT("merge","--no-ff","topic","-m","Merge topic\n\nReview both parents.");gchar *merge=head();
 rev=insight_git_history_revision(root,merge,NULL,&e);g_assert_no_error(e);g_assert_cmpuint(rev->parents->len,==,2);for(guint i=0;i<2;i++) { files=insight_git_history_delta(root,merge,g_ptr_array_index(rev->parents,i),NULL,&e);g_assert_no_error(e);g_assert_cmpuint(files->len,>,0);g_ptr_array_unref(files); }insight_git_revision_free(rev);
 refs=insight_git_history_refs(root,NULL,&e);g_assert_no_error(e);g_assert_cmpuint(refs->len,==,3);g_ptr_array_unref(refs);
 gboolean more=TRUE;GPtrArray *log=insight_git_history_log(root,merge,0,&more,NULL,&e);g_assert_no_error(e);g_assert_false(more);g_assert_cmpuint(log->len,==,4);g_ptr_array_unref(log);
 const gchar *status_args[]={"status","--porcelain=v1",NULL};gchar *before=git(status_args);GCancellable *cancel=g_cancellable_new();g_cancellable_cancel(cancel);log=insight_git_history_log(root,merge,0,NULL,cancel,&e);g_assert_null(log);g_assert_error(e,G_IO_ERROR,G_IO_ERROR_CANCELLED);g_clear_error(&e);g_object_unref(cancel);
 g_assert_null(insight_git_history_revision(root,"--all",NULL,&e));g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT);g_clear_error(&e);
 gchar *after=git(status_args),*unchanged=head();g_assert_cmpstr(before,==,after);g_assert_cmpstr(unchanged,==,merge);g_free(before);g_free(after);g_free(unchanged);
 /* Pagination stays pinned to an immutable tip, including boundary and end. */
 for(guint i=0;i<198;i++) GIT("commit","--allow-empty","-m","History page fixture");gchar *tip=head();log=insight_git_history_log(root,tip,0,&more,NULL,&e);g_assert_no_error(e);g_assert_true(more);g_assert_cmpuint(log->len,==,200);g_ptr_array_unref(log);log=insight_git_history_log(root,tip,200,&more,NULL,&e);g_assert_no_error(e);g_assert_false(more);g_assert_cmpuint(log->len,==,2);g_ptr_array_unref(log);
 g_free(tip);g_free(initial);g_free(second);g_free(merge);g_free(root);g_print("Read-only Git history: unborn/root/merge, full messages, refs, bounded blobs, NUL paths, empty files, immutable IDs, cancellation and pagination passed\n");return 0;
}
