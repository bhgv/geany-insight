/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "keybindings.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static gchar *root;
static gchar *git(const gchar *const *args)
{
 GSubprocessLauncher *l=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_PIPE);g_subprocess_launcher_set_cwd(l,root);GPtrArray *a=g_ptr_array_new();g_ptr_array_add(a,"git");for(guint i=0;args[i];i++) g_ptr_array_add(a,(gpointer)args[i]);g_ptr_array_add(a,NULL);GError *e=NULL;GSubprocess *p=g_subprocess_launcher_spawnv(l,(const gchar *const *)a->pdata,&e);g_assert_no_error(e);gchar *out=NULL,*err=NULL;g_assert_true(g_subprocess_communicate_utf8(p,NULL,NULL,&out,&err,&e));g_assert_no_error(e);if(!g_subprocess_get_successful(p)) g_error("git failed: %s",err);g_free(err);g_object_unref(p);g_object_unref(l);g_ptr_array_unref(a);return out;
}
#define GIT(...) do { const gchar *a[]={__VA_ARGS__,NULL};g_free(git(a)); } while(0)
static gchar *head(void) { const gchar *a[]={"rev-parse","HEAD",NULL};gchar *s=git(a);g_strchomp(s);return s; }
static void write_file(const gchar *name,const gchar *text,gssize n) { gchar *p=g_build_filename(root,name,NULL);g_assert_true(g_file_set_contents(p,text,n,NULL));g_free(p); }

static GtkWidget *window;static gchar *path,*before_head,*before_status;
static const gchar *old_code="// original value\nint value = 1;\n\nint answer(void) {\n    return value;\n}\n",*new_code="// revised value\nint value = 2;\n\nint answer(void) {\n    return value + 40;\n}\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static GtkWidget *top(void) { GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data) && !g_strcmp0(gtk_widget_get_name(p->data),"insight-git-history-window")) found=p->data;g_list_free(list);return found; }
static void flush(void) { while(gtk_events_pending()) gtk_main_iteration(); }
static GtkWidget *ready(void) { GtkWidget *v=top();g_assert_nonnull(v);gint64 end=g_get_monotonic_time()+10*G_TIME_SPAN_SECOND;while(g_object_get_data(G_OBJECT(v),"insight-git-history-busy") && g_get_monotonic_time()<end) { flush();g_usleep(1000); }g_assert_null(g_object_get_data(G_OBJECT(v),"insight-git-history-busy"));flush();return v; }
static void click(GtkWidget *v,const gchar *name) { gtk_button_clicked(GTK_BUTTON(named(v,name))); }
static void row(GtkWidget *v,const gchar *name,gint n) { GtkTreePath *p=gtk_tree_path_new_from_indices(n,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(named(v,name)),p,NULL,FALSE);gtk_tree_path_free(p); }
static void file(GtkWidget *v,const gchar *name) { GtkTreeModel *m=gtk_tree_view_get_model(GTK_TREE_VIEW(named(v,"insight-git-history-files")));GtkTreeIter i;gboolean valid=gtk_tree_model_get_iter_first(m,&i);gint n=0;while(valid) { gchar *p=NULL;gtk_tree_model_get(m,&i,2,&p,-1);gboolean match=!strcmp(name,p);g_free(p);if(match) { row(v,"insight-git-history-files",n);ready();return; }n++;valid=gtk_tree_model_iter_next(m,&i); }g_error("Missing historical file %s",name); }
static void revision(GtkWidget *v,const gchar *subject) { GtkTreeModel *m=gtk_tree_view_get_model(GTK_TREE_VIEW(named(v,"insight-git-history-commits")));GtkTreeIter i;gint n=0;for(gboolean valid=gtk_tree_model_get_iter_first(m,&i);valid;valid=gtk_tree_model_iter_next(m,&i),n++) { gchar *text=NULL;gtk_tree_model_get(m,&i,2,&text,-1);gboolean match=!strcmp(text,subject);g_free(text);if(match) { row(v,"insight-git-history-commits",n);ready();return; } }g_error("Missing commit %s",subject); }
static void pane(GtkWidget *v,const gchar *name,const gchar *expected) { ScintillaObject *s=SCINTILLA(named(v,name));g_assert_true(SEND(s,SCI_GETREADONLY,0,0));g_assert_cmpint(SEND(s,SCI_GETMARGINWIDTHN,0,0),>,0);g_assert_false(SEND(s,SCI_CANUNDO,0,0));gchar *text=sci_get_contents(s,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void branch(GtkWidget *v,gint index) { gtk_combo_box_set_active(GTK_COMBO_BOX(named(v,"insight-git-history-branch")),index);ready(); }
static gboolean step(gpointer data)
{
 (void)data;GeanyDocument *source=document_get_current();if(!source || !source->file_name || strcmp(source->file_name,path)) return G_SOURCE_CONTINUE;window=gtk_widget_get_toplevel(GTK_WIDGET(source->editor->sci));
 sci_set_text(source->editor->sci,"unsaved source buffer\n");SEND(source->editor->sci,SCI_SETSEL,7,3);gint undo=SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0);
 GtkWidget *item=named(window,"insight-git-history-menu");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));GtkWidget *v=ready();
 GtkTextBuffer *buffer=gtk_text_view_get_buffer(GTK_TEXT_VIEW(named(v,"insight-git-history-message")));GtkTextIter a,z;gtk_text_buffer_get_bounds(buffer,&a,&z);gchar *message=gtk_text_buffer_get_text(buffer,&a,&z,FALSE);g_assert_nonnull(strstr(message,"Review both parents."));g_free(message);g_assert_false(gtk_text_view_get_editable(GTK_TEXT_VIEW(named(v,"insight-git-history-message"))));
 g_assert_cmpint(gtk_tree_model_iter_n_children(gtk_combo_box_get_model(GTK_COMBO_BOX(named(v,"insight-git-history-parent"))),NULL),==,2);
 gtk_combo_box_set_active(GTK_COMBO_BOX(named(v,"insight-git-history-parent")),1);ready();file(v,"sample.c");pane(v,"insight-git-history-left",old_code);pane(v,"insight-git-history-right",new_code);
 /* Newer/older commits select and reload; inspect binary status then source. */
 click(v,"insight-git-history-older");ready();click(v,"insight-git-history-newer");ready();revision(v,"Revise source");file(v,"binary.dat");g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(v,"insight-git-history-status"))),"Binary"));pane(v,"insight-git-history-right","");file(v,"sample.c");
 pane(v,"insight-git-history-left",old_code);pane(v,"insight-git-history-right",new_code);ScintillaObject *s=SCINTILLA(named(v,"insight-git-history-right"));
 g_assert_cmpint(SEND(s,SCI_GETSTYLEAT,0,0),!=,SEND(s,SCI_GETSTYLEAT,strlen("// revised value\n"),0));g_assert_cmpint(SEND(s,SCI_MARKERGET,1,0)&(1<<10),!=,0);
 SEND(s,SCI_INSERTTEXT,0,(sptr_t)"should not edit");pane(v,"insight-git-history-right",new_code);
 GtkTreeModel *hunks=gtk_tree_view_get_model(GTK_TREE_VIEW(named(v,"insight-git-history-hunks")));g_assert_cmpint(gtk_tree_model_iter_n_children(hunks,NULL),==,2);click(v,"insight-git-history-diff-next");g_assert_cmpint(SEND(s,SCI_GETSELECTIONSTART,0,0),>,20);click(v,"insight-git-history-diff-previous");
 if(g_getenv("GEANY_GIT_HISTORY_CAPTURE")) { flush();GdkPixbuf *image=gdk_pixbuf_get_from_window(gtk_widget_get_window(v),0,0,gtk_widget_get_allocated_width(v),gtk_widget_get_allocated_height(v));g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_GIT_HISTORY_CAPTURE"),"jpeg",NULL,"quality","88",NULL));g_object_unref(image); }
 /* HEAD, main, topic; topic has an initial root commit. */
 branch(v,2);row(v,"insight-git-history-commits",1);ready();file(v,"sample.c");pane(v,"insight-git-history-left","");pane(v,"insight-git-history-right",old_code);
 /* Replaced workers cannot publish a stale branch or commit. */
 gtk_combo_box_set_active(GTK_COMBO_BOX(named(v,"insight-git-history-branch")),1);gtk_combo_box_set_active(GTK_COMBO_BOX(named(v,"insight-git-history-branch")),2);ready();row(v,"insight-git-history-commits",1);ready();file(v,"sample.c");pane(v,"insight-git-history-right",old_code);
 click(v,"insight-git-history-refresh");click(v,"insight-git-history-cancel");ready();click(v,"insight-git-history-close");g_assert_null(top());
 keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_INSIGHT_GITHISTORY);g_assert_nonnull(top());click(top(),"insight-git-history-close");flush();
 gchar *text=sci_get_contents(source->editor->sci,-1);g_assert_cmpstr(text,==,"unsaved source buffer\n");g_free(text);g_assert_cmpint(SEND(source->editor->sci,SCI_GETUNDOCURRENT,0,0),==,undo);g_assert_cmpint(SEND(source->editor->sci,SCI_GETANCHOR,0,0),==,7);g_assert_cmpint(SEND(source->editor->sci,SCI_GETCURRENTPOS,0,0),==,3);
 gchar *now=head();g_assert_cmpstr(now,==,before_head);g_free(now);const gchar *status[]={"status","--porcelain=v1",NULL};now=git(status);g_assert_cmpstr(now,==,before_status);g_free(now);g_assert_true(g_file_get_contents(path,&text,NULL,NULL));g_assert_cmpstr(text,==,new_code);g_free(text);
 document_set_text_changed(source,FALSE);g_print("Git history GUI: menu/key, branches/commits/parents/files, full comments, syntax styles, line numbers, read-only panes, hunk navigation, binary errors, cancellation and unchanged HEAD/worktree/dirty buffer passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Git history GUI timeout");return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("git-history-native-XXXXXX",NULL);root=g_build_filename(dir,"repository",NULL);g_assert_cmpint(g_mkdir(root,0700),==,0);GIT("init","--initial-branch=main");GIT("config","user.name","History Test");GIT("config","user.email","history@example.invalid");write_file("sample.c",old_code,-1);GIT("add",".");GIT("commit","-m","Initial source");GIT("branch","topic");write_file("sample.c",new_code,-1);write_file("binary.dat","a\0b",3);GIT("add",".");GIT("commit","-m","Revise source\n\nUse a named value and adjust the answer.\nPreserve the original for comparison.");GIT("checkout","topic");write_file("topic.c","int topic;\n",-1);GIT("add",".");GIT("commit","-m","Topic feature");GIT("checkout","main");GIT("merge","--no-ff","topic","-m","Merge topic\n\nReview both parents.");before_head=head();const gchar *status[]={"status","--porcelain=v1",NULL};before_status=git(status);
 path=g_build_filename(root,"sample.c",NULL);gchar *profile=g_build_filename(dir,"profile",NULL);gchar *args[]={"git-history-native","--new-instance","--no-session","--config",profile,path,NULL};g_timeout_add(400,step,NULL);g_timeout_add_seconds(48,timeout,NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);return main_lib(6,args);
}
