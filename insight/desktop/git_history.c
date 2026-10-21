/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "git_history.h"
#include <string.h>
static const gchar *bytes_data(GBytes *bytes,gsize *size)
{ const gchar *data=g_bytes_get_data(bytes,size);return data ? data:""; }
static gboolean fail(GError **error,GIOErrorEnum code,const gchar *message)
{ g_set_error_literal(error,G_IO_ERROR,code,message);return FALSE; }
static gboolean oid_valid(const gchar *oid)
{
    if(!oid || (strlen(oid)!=40 && strlen(oid)!=64)) return FALSE;
    for(const gchar *p=oid;*p;p++) if(!g_ascii_isxdigit(*p)) return FALSE;return TRUE;
}
static GBytes *read_git(const gchar *root,const gchar *const *args,gsize limit,GCancellable *cancel,GError **error)
{
    if(!root || !g_path_is_absolute(root)) { fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Git history requires an absolute repository path.");return NULL; }
    GSubprocessLauncher *launcher=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE);g_subprocess_launcher_set_cwd(launcher,root);
    gchar **environment=g_get_environ();
    for(guint i=0;environment[i];i++) if(g_str_has_prefix(environment[i],"GIT_")) { gchar *name=g_strndup(environment[i],strchr(environment[i],'=')-environment[i]);g_subprocess_launcher_unsetenv(launcher,name);g_free(name); }
    g_strfreev(environment);
    g_subprocess_launcher_setenv(launcher,"GIT_TERMINAL_PROMPT","0",TRUE);g_subprocess_launcher_setenv(launcher,"GIT_OPTIONAL_LOCKS","0",TRUE);g_subprocess_launcher_setenv(launcher,"GIT_LITERAL_PATHSPECS","1",TRUE);g_subprocess_launcher_setenv(launcher,"GIT_NO_LAZY_FETCH","1",TRUE);
    GPtrArray *argv=g_ptr_array_new();const gchar *prefix[]={"git","--no-pager","--no-replace-objects","-c","color.ui=false","-c","core.quotepath=false","-c","core.fsmonitor=false",NULL};
    for(guint i=0;prefix[i];i++) g_ptr_array_add(argv,(gpointer)prefix[i]);for(guint i=0;args[i];i++) g_ptr_array_add(argv,(gpointer)args[i]);g_ptr_array_add(argv,NULL);
    GSubprocess *process=g_subprocess_launcher_spawnv(launcher,(const gchar *const *)argv->pdata,error);g_ptr_array_unref(argv);g_object_unref(launcher);if(!process) return NULL;
    GInputStream *stream=g_subprocess_get_stdout_pipe(process);GByteArray *bytes=g_byte_array_new();guchar buffer[16384];gboolean ok=TRUE;
    for(;;) {
        gssize size=g_input_stream_read(stream,buffer,sizeof buffer,cancel,error);if(size<0) { ok=FALSE;break; }if(!size) break;
        if((gsize)size>limit-bytes->len) { ok=fail(error,G_IO_ERROR_NO_SPACE,"Git history output exceeds the display limit.");break; }
        g_byte_array_append(bytes,buffer,size);
    }
    if(!ok) { g_subprocess_force_exit(process);g_subprocess_wait(process,NULL,NULL); }
    else if(!g_subprocess_wait(process,cancel,error)) { ok=FALSE;g_subprocess_force_exit(process);g_subprocess_wait(process,NULL,NULL); }
    else if(!g_subprocess_get_successful(process)) {
        gchar *message=g_utf8_make_valid(bytes->len ? (const gchar *)bytes->data:"",MIN(bytes->len,4096u));g_set_error(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Git: %s",*message ? message:"history command failed");g_free(message);ok=FALSE;
    }
    g_object_unref(process);if(!ok) { g_byte_array_unref(bytes);return NULL; }return g_byte_array_free_to_bytes(bytes);
}
static gchar *field(const gchar **p,const gchar *end)
{ const gchar *zero=memchr(*p,0,end-*p);if(!zero) return NULL;gchar *text=g_strndup(*p,zero-*p);*p=zero+1;return text; }
static gchar *display(gchar *raw) { gchar *text=g_utf8_make_valid(raw,-1);g_free(raw);return text; }
static void ref_free(gpointer data) { InsightGitRef *ref=data;g_free(ref->name);g_free(ref->oid);g_free(ref); }
void insight_git_revision_free(InsightGitRevision *revision)
{ if(!revision) return;g_free(revision->oid);g_free(revision->author);g_free(revision->date);g_free(revision->subject);g_free(revision->message);g_clear_pointer(&revision->parents,g_ptr_array_unref);g_free(revision); }
static void delta_free(gpointer data)
{ InsightGitDelta *delta=data;g_free(delta->path);g_free(delta->old_oid);g_free(delta->new_oid);g_free(delta); }
gchar *insight_git_history_root(const gchar *path,GCancellable *cancel,GError **error)
{
    const gchar *args[]={"rev-parse","--show-toplevel",NULL};GBytes *bytes=read_git(path,args,65536,cancel,error);if(!bytes) return NULL;gsize size;const gchar *raw=bytes_data(bytes,&size);
    gchar *root=size && raw[size-1]=='\n' && !memchr(raw,0,size) ? g_strndup(raw,size-1):NULL;g_bytes_unref(bytes);
    if(!root || !g_path_is_absolute(root)) { g_free(root);fail(error,G_IO_ERROR_INVALID_DATA,"Cannot identify the Git worktree root.");return NULL; }return root;
}
GPtrArray *insight_git_history_refs(const gchar *root,GCancellable *cancel,GError **error)
{
    const gchar *args[]={"for-each-ref","--count=1001","--sort=refname","--format=%(refname)%00%(objectname)%00","refs/heads","refs/remotes",NULL};
    GBytes *bytes=read_git(root,args,1024*1024,cancel,error);if(!bytes) return NULL;GPtrArray *refs=g_ptr_array_new_with_free_func(ref_free);
    /* HEAD also represents detached checkouts. An unborn HEAD has no object. */
    const gchar *head_args[]={"rev-parse","--verify","HEAD^{commit}",NULL};GError *head_error=NULL;GBytes *head=read_git(root,head_args,4096,cancel,&head_error);
    if(head) { gsize size;const gchar *data=bytes_data(head,&size);gchar *oid=g_strndup(data,size);g_strchomp(oid);if(oid_valid(oid)) { InsightGitRef *ref=g_new0(InsightGitRef,1);ref->name=g_strdup("HEAD");ref->oid=oid;g_ptr_array_add(refs,ref); }else g_free(oid);g_bytes_unref(head); }
    if(g_error_matches(head_error,G_IO_ERROR,G_IO_ERROR_CANCELLED)) { g_propagate_error(error,head_error);g_bytes_unref(bytes);g_ptr_array_unref(refs);return NULL; }g_clear_error(&head_error);
    gsize size;const gchar *p=bytes_data(bytes,&size),*end=p+size;gboolean ok=TRUE;guint count=0;
    while(p<end) {
        gchar *name=field(&p,end),*oid=name ? field(&p,end):NULL;
        if(!name || !oid_valid(oid) || p>=end || *p++!='\n' || ++count>1000) { g_free(name);g_free(oid);ok=fail(error,G_IO_ERROR_INVALID_DATA,"Invalid or oversized Git branch list (1000 refs maximum).");break; }
        InsightGitRef *ref=g_new0(InsightGitRef,1);ref->name=display(name);ref->oid=oid;g_ptr_array_add(refs,ref);
    }
    g_bytes_unref(bytes);if(!ok) { g_ptr_array_unref(refs);return NULL; }return refs;
}
GPtrArray *insight_git_history_log(const gchar *root,const gchar *tip,guint offset,gboolean *more,GCancellable *cancel,GError **error)
{
    if(more) *more=FALSE;if(!oid_valid(tip) || offset>100000) { fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Invalid Git history revision or page.");return NULL; }
    gchar *skip=g_strdup_printf("--skip=%u",offset);const gchar *args[]={"log","--no-show-signature","--no-decorate","--encoding=UTF-8","-z","--format=%H%x00%an%x00%aI%x00%s","--max-count=201",skip,tip,"--",NULL};GBytes *bytes=read_git(root,args,8u*1024u*1024u,cancel,error);g_free(skip);if(!bytes) return NULL;
    GPtrArray *rows=g_ptr_array_new_with_free_func((GDestroyNotify)insight_git_revision_free);gsize size;const gchar *p=bytes_data(bytes,&size),*end=p+size;gboolean ok=TRUE;
    while(p<end) {
        InsightGitRevision *row=g_new0(InsightGitRevision,1);row->oid=field(&p,end);row->author=field(&p,end);row->date=field(&p,end);row->subject=field(&p,end);
        if(!oid_valid(row->oid) || !row->author || !row->date || !row->subject || rows->len>INSIGHT_GIT_HISTORY_PAGE) { insight_git_revision_free(row);ok=fail(error,G_IO_ERROR_INVALID_DATA,"Invalid Git commit list.");break; }
        row->author=display(row->author);row->date=display(row->date);row->subject=display(row->subject);g_ptr_array_add(rows,row);
    }
    if(rows->len>INSIGHT_GIT_HISTORY_PAGE) { if(more) *more=TRUE;g_ptr_array_remove_index(rows,rows->len-1); }
    g_bytes_unref(bytes);if(!ok) { g_ptr_array_unref(rows);return NULL; }return rows;
}
InsightGitRevision *insight_git_history_revision(const gchar *root,const gchar *oid,GCancellable *cancel,GError **error)
{
    if(!oid_valid(oid)) { fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Choose a full Git commit object ID.");return NULL; }
    const gchar *args[]={"show","-s","--no-show-signature","--encoding=UTF-8","--format=%H%x00%P%x00%an%x00%aI%x00%B%x00",oid,"--",NULL};GBytes *bytes=read_git(root,args,128u*1024u,cancel,error);if(!bytes) return NULL;
    gsize size;const gchar *p=bytes_data(bytes,&size),*end=p+size;InsightGitRevision *row=g_new0(InsightGitRevision,1);row->oid=field(&p,end);gchar *parents=field(&p,end);row->author=field(&p,end);row->date=field(&p,end);row->message=field(&p,end);row->parents=g_ptr_array_new_with_free_func(g_free);gboolean ok=oid_valid(row->oid) && parents && row->author && row->date && row->message && strlen(row->message)<=65536 && (p==end || (p+1==end && *p=='\n'));
    if(ok && *parents) { gchar **parts=g_strsplit(parents," ",-1);for(guint i=0;parts[i];i++) { if(!oid_valid(parts[i]) || i>=128) { ok=FALSE;break; }g_ptr_array_add(row->parents,g_strdup(parts[i])); }g_strfreev(parts); }
    g_free(parents);g_bytes_unref(bytes);if(!ok) { insight_git_revision_free(row);fail(error,G_IO_ERROR_INVALID_DATA,"Invalid commit metadata or message above 64 KiB.");return NULL; }
    row->author=display(row->author);row->date=display(row->date);row->message=display(row->message);return row;
}
GPtrArray *insight_git_history_delta(const gchar *root,const gchar *oid,const gchar *parent,GCancellable *cancel,GError **error)
{
    if(!oid_valid(oid) || (parent && !oid_valid(parent))) { fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Invalid commit comparison IDs.");return NULL; }
    const gchar *args[]={"diff-tree","-r","--root","--no-commit-id","--raw","--no-abbrev","-z","--no-renames","--no-ext-diff","--no-textconv",parent ? parent:oid,parent ? oid:"--",parent ? "--":NULL,NULL};GBytes *bytes=read_git(root,args,8u*1024u*1024u,cancel,error);if(!bytes) return NULL;
    GPtrArray *rows=g_ptr_array_new_with_free_func(delta_free);gsize size;const gchar *p=bytes_data(bytes,&size),*end=p+size;gboolean ok=TRUE;
    while(p<end) {
        gchar *header=field(&p,end),*path=header ? field(&p,end):NULL;gchar **parts=header ? g_strsplit(header," ",-1):NULL;
        if(!header || !path || !*path || !parts || g_strv_length(parts)!=5 || parts[0][0]!=':' || strlen(parts[0]+1)!=6 || strlen(parts[1])!=6 || !oid_valid(parts[2]) || !oid_valid(parts[3]) || strlen(parts[4])!=1 || !strchr("AMDTUXB",parts[4][0]) || rows->len>=10000) { g_free(header);g_free(path);g_strfreev(parts);ok=fail(error,G_IO_ERROR_INVALID_DATA,"Invalid or oversized Git changed-file list (10000 files maximum).");break; }
        InsightGitDelta *row=g_new0(InsightGitDelta,1);row->path=path;row->old_mode=g_ascii_strtoull(parts[0]+1,NULL,8);row->new_mode=g_ascii_strtoull(parts[1],NULL,8);row->old_oid=g_strdup(parts[2]);row->new_oid=g_strdup(parts[3]);row->status=parts[4][0];g_ptr_array_add(rows,row);g_free(header);g_strfreev(parts);
    }
    g_bytes_unref(bytes);if(!ok) { g_ptr_array_unref(rows);return NULL; }return rows;
}
gchar *insight_git_history_text(const gchar *root,const gchar *oid,guint mode,gsize *length,GCancellable *cancel,GError **error)
{
    if(length) *length=0;if(!oid_valid(oid)) { fail(error,G_IO_ERROR_INVALID_ARGUMENT,"Invalid Git blob object ID.");return NULL; }
    if(!mode) return g_strdup("");
    if(mode==0160000) { gchar *text=g_strdup_printf("Submodule commit: %s\n",oid);if(length) *length=strlen(text);return text; }
    const gchar *size_args[]={"cat-file","-s",oid,NULL};GBytes *size_bytes=read_git(root,size_args,256,cancel,error);if(!size_bytes) return NULL;gsize n;const gchar *raw=bytes_data(size_bytes,&n);gchar *number=g_strndup(raw,n),*tail=NULL;guint64 size=g_ascii_strtoull(number,&tail,10);gboolean valid=tail && tail!=number && (!strcmp(tail,"\n") || !*tail) && size<=INSIGHT_GIT_HISTORY_TEXT_LIMIT;g_free(number);g_bytes_unref(size_bytes);
    if(!valid) { fail(error,G_IO_ERROR_NO_SPACE,"Historical file exceeds the 1 MiB text limit.");return NULL; }
    const gchar *args[]={"cat-file","blob",oid,NULL};GBytes *bytes=read_git(root,args,INSIGHT_GIT_HISTORY_TEXT_LIMIT,cancel,error);if(!bytes) return NULL;raw=bytes_data(bytes,&n);if(!raw) raw="";gchar *text=NULL;
    if(n!=size || memchr(raw,0,n) || !g_utf8_validate(raw,n,NULL)) fail(error,G_IO_ERROR_INVALID_DATA,"Binary or non-UTF-8 historical file: text diff unavailable.");
    else { text=g_strndup(raw,n);if(length) *length=n; }g_bytes_unref(bytes);return text;
}
