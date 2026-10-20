/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "Scintilla.h"
#include <string.h>

typedef struct {
 const gchar *name,*text,*expected;
 guint start,end,source,target;
 gboolean initial_ctrl;
 const gchar *last_key;
 guint undo_steps;
 gboolean read_only;
} DragCase;
static const DragCase cases[]={
 {"one line", "alpha\nbeta\ngamma\n\n", "alpha\nbeta\ngamma\nalpha\n\n",0,6,2,17,TRUE,"",1,FALSE},
 {"multiple lines", "alpha\nbeta\ngamma\n\n", "alpha\nbeta\ngamma\nalpha\nbeta\n\n",0,11,2,17,TRUE,"",1,FALSE},
 {"plain move", "alpha\nbeta\ngamma\n\n", "beta\ngamma\nalpha\n\n",0,6,2,17,FALSE,"",2,FALSE},
 {"Ctrl pressed at destination", "alpha beta  gamma\nEND\n", "alpha beta  gamma\nalphaEND\n",0,5,2,18,FALSE,"keydown Control_L",1,FALSE},
 {"Ctrl released at destination", "alpha beta  gamma\nEND\n", " beta  gamma\nalphaEND\n",0,5,2,18,TRUE,"keyup Control_L",2,FALSE},
 {"one letter", "alpha beta  gamma\nEND\n", "alpha beta  gamma\nlEND\n",1,2,1,18,FALSE,"keydown Control_L",1,FALSE},
 {"spaces", "alpha beta  gamma\nEND\n", "  alpha beta  gamma\nEND\n",10,12,10,0,FALSE,"keydown Control_L",1,FALSE},
 {"UTF-8 word", "αβ γδ\nEND\n", "αβ γδ\nαβEND\n",0,4,2,10,FALSE,"keydown Control_L",1,FALSE},
 {"inside a line", "alpha beta  gamma\nEND\n", "alpha bealphata  gamma\nEND\n",0,5,2,8,TRUE,"",1,FALSE},
 {"read only", "alpha beta  gamma\nEND\n", "alpha beta  gamma\nEND\n",0,5,2,18,TRUE,"",0,TRUE},
 {"Escape", "alpha beta  gamma\nEND\n", "alpha beta  gamma\nEND\n",0,5,2,18,TRUE,"key Escape",0,FALSE},
};
static ScintillaObject *sci;
static guint phase,scenario;
static gboolean done;
static sptr_t send(unsigned int msg,uptr_t w,sptr_t l) { return scintilla_send_message(sci,msg,w,l); }
static void exited(GPid pid,gint status,gpointer data) {
 (void)data;GError *error=NULL;
 g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);
 g_spawn_close_pid(pid);done=TRUE;
}
static void drag(const DragCase *c) {
 GtkWidget *widget=GTK_WIDGET(sci),*top=gtk_widget_get_toplevel(widget);
 gint ox,oy,wx,wy;
 gdk_window_get_origin(gtk_widget_get_window(top),&ox,&oy);
 g_assert_true(gtk_widget_translate_coordinates(widget,top,0,0,&wx,&wy));ox+=wx;oy+=wy;
 gint sx=ox+send(SCI_POINTXFROMPOSITION,0,c->source)+2;
 gint sy=oy+send(SCI_POINTYFROMPOSITION,0,c->source)+send(SCI_TEXTHEIGHT,0,0)/2;
 gint dx=ox+send(SCI_POINTXFROMPOSITION,0,c->target);
 gint dy=oy+send(SCI_POINTYFROMPOSITION,0,c->target)+send(SCI_TEXTHEIGHT,0,0)/2;
 /* Change Ctrl only after the final motion: the release state decides. */
 gchar *script=g_strdup_printf("xdotool mousemove --sync %d %d %s mousedown 1 sleep 0.2 mousemove --sync %d %d sleep 0.3 mousemove --sync %d %d sleep 0.3 mousemove --sync %d %d sleep 0.3 %s sleep 0.2 mouseup 1 keyup Control_L",sx,sy,c->initial_ctrl ? "keydown Control_L" : "",sx+18,sy,dx+10,dy,dx,dy,c->last_key);
 gchar *args[]={"sh","-c",script,NULL};GPid pid;GError *error=NULL;done=FALSE;
 g_assert_true(g_spawn_async(NULL,args,NULL,G_SPAWN_SEARCH_PATH|G_SPAWN_DO_NOT_REAP_CHILD,NULL,NULL,&pid,&error));g_assert_no_error(error);
 g_child_watch_add(pid,exited,NULL);g_free(script);
}
static void check_text(const gchar *expected) {
 gchar buffer[256];send(SCI_GETTEXT,sizeof buffer,(sptr_t)buffer);g_assert_cmpstr(buffer,==,expected);
}
static gboolean step(gpointer data) {
 (void)data;if(!document_get_current()) return G_SOURCE_CONTINUE;
 sci=document_get_current()->editor->sci;const DragCase *c=&cases[scenario];
 if(phase==0) {
  GtkWindow *window=GTK_WINDOW(gtk_widget_get_toplevel(GTK_WIDGET(sci)));
  gtk_window_move(window,10,10);gtk_window_resize(window,1400,900);
  send(SCI_SETREADONLY,0,0);send(SCI_SETCODEPAGE,SC_CP_UTF8,0);
  send(SCI_SETTEXT,0,(sptr_t)c->text);send(SCI_EMPTYUNDOBUFFER,0,0);
  send(SCI_SETXOFFSET,0,0);send(SCI_SETSEL,c->start,c->end);
  send(SCI_SETREADONLY,c->read_only,0);gtk_widget_grab_focus(GTK_WIDGET(sci));phase=1;return G_SOURCE_CONTINUE;
 }
 if(phase==1) { drag(c);phase=2;return G_SOURCE_CONTINUE; }
 if(!done) return G_SOURCE_CONTINUE;
 g_printerr("Checking native editor drag: %s\n",c->name);check_text(c->expected);
 if(scenario==1 && g_getenv("GEANY_DRAG_CAPTURE")) {
  GdkWindow *w=gtk_widget_get_window(GTK_WIDGET(sci));
  GdkPixbuf *image=gdk_pixbuf_get_from_window(w,0,0,MIN(700,gdk_window_get_width(w)),MIN(230,gdk_window_get_height(w)));
  g_assert_nonnull(image);g_assert_true(gdk_pixbuf_save(image,g_getenv("GEANY_DRAG_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(image);
 }
 for(guint i=0;i<c->undo_steps;i++) send(SCI_UNDO,0,0);
 check_text(c->text);g_assert_false(send(SCI_CANUNDO,0,0));
 for(guint i=0;i<c->undo_steps;i++) send(SCI_REDO,0,0);
 check_text(c->expected);
 if(++scenario<G_N_ELEMENTS(cases)) { phase=0;return G_SOURCE_CONTINUE; }
 send(SCI_SETSAVEPOINT,0,0);
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(gtk_widget_get_toplevel(GTK_WIDGET(sci)),"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Editor drag timeout scenario %u phase %u",scenario,phase);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *driver=g_find_program_in_path("xdotool");if(!driver) return 77;g_free(driver);
 gchar *profile=g_dir_make_tmp("editor-drag-XXXXXX",NULL);g_assert_nonnull(profile);
 gchar *config=g_build_filename(profile,"geany.conf",NULL);
 g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *args[]={"editor-drag-gui","--new-instance","--no-session","--config",profile,NULL};
 g_timeout_add(500,step,NULL);g_timeout_add_seconds(50,timeout,NULL);
 int result=main_lib(5,args);g_free(profile);return result;
}
