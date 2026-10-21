/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static GeanyDocument *doc;
static ScintillaObject *sci;
static gchar *path;
static guint phase,timer,reply;
static gint mode,radix,guard;
static const gchar *value;
static gboolean hook_armed,hook_done,capture_image;
static const gchar *original="int a = 4; // keep 99\r\nint b = 7; // keep 88\r\nint c = 9; // keep 77\r\n";
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *found=named(m,name);if(found) return found; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *found=NULL;for(GList *p=children;p&&!found;p=p->next) found=named(p->data,name);g_list_free(children);return found; }return NULL;
}
static GtkWidget *dialog(void) {
 GList *list=gtk_window_list_toplevels();GtkWidget *found=NULL;for(GList *p=list;p;p=p->next) if(gtk_widget_get_visible(p->data)&&!g_strcmp0(gtk_widget_get_name(p->data),"insight-renumber-ranges-dialog")) found=p->data;g_list_free(list);return found;
}
static void check(const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static void reset(const gchar *text) {
 SEND(sci,SCI_SETREADONLY,FALSE,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);SEND(sci,SCI_CLEARSELECTIONS,0,0);sci_set_text(sci,text);SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);SEND(sci,SCI_SETMULTIPLESELECTION,TRUE,0);SEND(sci,SCI_SETVIRTUALSPACEOPTIONS,SCVS_RECTANGULARSELECTION,0);SEND(sci,SCI_SETTABWIDTH,4,0);
}
static sptr_t pos(guint line,guint column) { return SEND(sci,SCI_FINDCOLUMN,line,column); }
static void rectangle(guint first,guint last,guint left,guint right,gboolean reverse) {
 SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);SEND(sci,SCI_SETRECTANGULARSELECTIONANCHOR,reverse ? pos(last,right):pos(first,left),0);SEND(sci,SCI_SETRECTANGULARSELECTIONCARET,reverse ? pos(first,left):pos(last,right),0);
}
static void undo_redo(const gchar *after,const gchar *before) {
 check(after);SEND(sci,SCI_UNDO,0,0);check(before);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));SEND(sci,SCI_REDO,0,0);check(after);
}
static gboolean respond(gpointer data) {
 (void)data;GtkWidget *d=dialog();if(!d) return G_SOURCE_CONTINUE;
 if(!reply++) {
  gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-renumber-ranges-mode")),mode);gtk_combo_box_set_active(GTK_COMBO_BOX(named(d,"insight-renumber-ranges-radix")),radix);gtk_entry_set_text(GTK_ENTRY(named(d,"insight-renumber-ranges-value")),value);g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY)));gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_HELP);return G_SOURCE_CONTINUE;
 }
 if(reply==2) return G_SOURCE_CONTINUE; /* allow a frame for the real capture */
 if(guard==7||guard==8) {
  g_assert_false(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY)));
  if(guard==7) g_assert_nonnull(strstr(gtk_label_get_text(GTK_LABEL(named(d,"insight-renumber-ranges-status"))),"64-bit"));
  timer=0;gtk_dialog_response(GTK_DIALOG(d),GTK_RESPONSE_CANCEL);return G_SOURCE_REMOVE;
 }
 g_assert_true(gtk_widget_get_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(d),GTK_RESPONSE_APPLY)));
 if(capture_image&&g_getenv("GEANY_RENUMBER_RANGES_CAPTURE")) {
  ScintillaObject *before=SCINTILLA(named(d,"insight-renumber-ranges-before")),*after=SCINTILLA(named(d,"insight-renumber-ranges-after"));g_assert_true(SEND(before,SCI_GETREADONLY,0,0));g_assert_true(SEND(after,SCI_GETREADONLY,0,0));g_assert_cmpint(SEND(before,SCI_GETMARGINWIDTHN,1,0),>,0);g_assert_cmpint(SEND(before,SCI_GETSTYLEAT,0,0),!=,SEND(before,SCI_GETSTYLEAT,strstr(original,"//")-original,0));
  gchar *text=sci_get_contents(after,-1);g_assert_cmpstr(text,==,"int a = 10; // keep 99\r\nint b = 11; // keep 88\r\nint c = 12; // keep 77\r\n");g_free(text);check(original);
  GdkWindow *w=gtk_widget_get_window(d);GdkPixbuf *im=gdk_pixbuf_get_from_window(w,0,0,gtk_widget_get_allocated_width(d),gtk_widget_get_allocated_height(d));g_assert_nonnull(im);g_assert_true(gdk_pixbuf_save(im,g_getenv("GEANY_RENUMBER_RANGES_CAPTURE"),"jpeg",NULL,"quality","90",NULL));g_object_unref(im);
 }
 if(guard==1) { SEND(sci,SCI_APPENDTEXT,7,(sptr_t)"manual\n");SEND(sci,SCI_UNDO,0,0); }
 if(guard==2) SEND(sci,SCI_SETSEL,0,1);
 if(guard==3) SEND(sci,SCI_SETREADONLY,TRUE,0);
 if(guard==5) { SEND(sci,SCI_SETSAVEPOINT,0,0);g_assert_true(document_close(doc)); }
 if(guard==6) document_new_file("other.c",NULL,"int other;\n");
 if(guard==9) { SEND(sci,SCI_APPENDTEXT,7,(sptr_t)"manual\n"); }
 timer=0;gtk_dialog_response(GTK_DIALOG(d),guard==4 ? GTK_RESPONSE_CANCEL:GTK_RESPONSE_APPLY);return G_SOURCE_REMOVE;
}
static void command(gint m,gint r,const gchar *v,gint g,gboolean image) {
 mode=m;radix=r;value=v;guard=g;reply=0;capture_image=image;timer=g_timeout_add(50,respond,NULL);GtkWidget *item=named(window,"insight-renumber");g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));if(timer) { g_source_remove(timer);timer=0; }
}
static void interfere(ScintillaObject *s,gint id,SCNotification *nt,gpointer data) {
 (void)s;(void)id;(void)data;if(!hook_armed||hook_done||nt->nmhdr.code!=SCN_MODIFIED||!(nt->modificationType&SC_MOD_INSERTTEXT)) return;hook_done=TRUE;
 if(phase==1) SEND(sci,SCI_INSERTTEXT,0,(sptr_t)"// hook\n");else { GtkWidget *d=dialog();g_assert_nonnull(d);gtk_widget_destroy(d); }
}
static gboolean step(gpointer data) {
 (void)data;doc=document_find_by_filename(path);if(!doc) return G_SOURCE_CONTINUE;sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 reset(original);rectangle(0,2,8,9,FALSE);g_assert_cmpint(SEND(sci,SCI_GETSELECTIONS,0,0),==,3);gint handle=SEND(sci,SCI_MARKERADD,2,24);command(1,1,"10",0,TRUE);g_assert_cmpint(SEND(sci,SCI_GETSELECTIONMODE,0,0),==,SC_SEL_STREAM);g_assert_cmpint(SEND(sci,SCI_GETSELECTIONS,0,0),==,3);g_assert_cmpint(SEND(sci,SCI_MARKERLINEFROMHANDLE,handle,0),==,2);undo_redo("int a = 10; // keep 99\r\nint b = 11; // keep 88\r\nint c = 12; // keep 77\r\n",original);
 reset(original);rectangle(0,2,8,9,TRUE);gint main_line=SEND(sci,SCI_LINEFROMPOSITION,SEND(sci,SCI_GETSELECTIONNCARET,SEND(sci,SCI_GETMAINSELECTION,0,0),0),0);command(2,2,"2",0,FALSE);g_assert_cmpint(SEND(sci,SCI_LINEFROMPOSITION,SEND(sci,SCI_GETSELECTIONNCARET,SEND(sci,SCI_GETMAINSELECTION,0,0),0),0),==,main_line);for(guint i=0;i<3;i++) g_assert_cmpint(SEND(sci,SCI_GETSELECTIONNANCHOR,i,0),>,SEND(sci,SCI_GETSELECTIONNCARET,i,0));undo_redo("int a = 0x6; // keep 99\r\nint b = 0x9; // keep 88\r\nint c = 0xb; // keep 77\r\n",original);
 reset(original);SEND(sci,SCI_SETSELECTION,pos(2,9),pos(2,8));SEND(sci,SCI_ADDSELECTION,pos(0,9),pos(0,8));SEND(sci,SCI_ADDSELECTION,pos(1,9),pos(1,8));SEND(sci,SCI_SETMAINSELECTION,0,0);command(0,1,"0",0,FALSE);g_assert_cmpint(SEND(sci,SCI_GETMAINSELECTION,0,0),==,2);undo_redo("int a = 4; // keep 99\r\nint b = 5; // keep 88\r\nint c = 6; // keep 77\r\n",original);
 const gchar *shorts="    4\nx\n    9\n";reset(shorts);rectangle(0,2,4,5,FALSE);command(1,1,"1",0,FALSE);undo_redo("    1\nx\n    2\n",shorts);
 const gchar *unicode="\tα 4;\r\n\tβ 7;\r\n";reset(unicode);rectangle(0,1,6,7,FALSE);command(1,2,"8",0,FALSE);undo_redo("\tα 0x8;\r\n\tβ 0x9;\r\n",unicode);
 reset("1\n\n2\n");SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);SEND(sci,SCI_SETRECTANGULARSELECTIONANCHOR,1,0);SEND(sci,SCI_SETRECTANGULARSELECTIONANCHORVIRTUALSPACE,3,0);SEND(sci,SCI_SETRECTANGULARSELECTIONCARET,4,0);SEND(sci,SCI_SETRECTANGULARSELECTIONCARETVIRTUALSPACE,4,0);command(1,1,"10",8,FALSE);check("1\n\n2\n");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 for(gint g=1;g<=4;g++) { reset(original);rectangle(0,2,8,9,FALSE);command(1,1,"10",g,FALSE);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0)); }
 reset(original);rectangle(0,2,8,9,FALSE);command(1,1,"9223372036854775807",7,FALSE);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original);rectangle(0,2,8,9,FALSE);command(1,1,"10",9,FALSE);gchar *manual=g_strconcat(original,"manual\n",NULL);check(manual);g_free(manual);SEND(sci,SCI_UNDO,0,0);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original);rectangle(0,2,8,9,FALSE);command(1,1,"10",6,FALSE);g_assert_true(document_get_current()!=doc);g_assert_true(document_open_file(path,FALSE,NULL,NULL)==doc);check(original);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset("\t4\n\t7\n");rectangle(0,1,0,1,FALSE);SEND(sci,SCI_SETRECTANGULARSELECTIONANCHORVIRTUALSPACE,1,0);SEND(sci,SCI_SETSELECTIONNANCHORVIRTUALSPACE,0,1);command(1,1,"10",0,FALSE);g_assert_cmpuint(reply,==,0);check("\t4\n\t7\n");g_assert_false(SEND(sci,SCI_CANUNDO,0,0));
 reset(original);rectangle(0,2,8,9,FALSE);gulong hook=g_signal_connect(sci,"sci-notify",G_CALLBACK(interfere),NULL);phase=1;hook_armed=TRUE;hook_done=FALSE;command(1,1,"10",0,FALSE);hook_armed=FALSE;g_assert_true(hook_done);undo_redo("// hook\nint a = 4; // keep 99\r\nint b = 7; // keep 88\r\nint c = 12; // keep 77\r\n",original);
 reset(original);rectangle(0,2,8,9,FALSE);phase=2;hook_armed=TRUE;hook_done=FALSE;command(1,1,"10",0,FALSE);hook_armed=FALSE;g_assert_true(hook_done);check("int a = 4; // keep 99\r\nint b = 7; // keep 88\r\nint c = 12; // keep 77\r\n");SEND(sci,SCI_UNDO,0,0);check(original);g_signal_handler_disconnect(sci,hook);
 gchar *disk=NULL;g_assert_true(g_file_get_contents(path,&disk,NULL,NULL));g_assert_cmpstr(disk,==,original);g_free(disk);reset(original);rectangle(0,2,8,9,FALSE);command(1,1,"10",5,FALSE);g_assert_null(document_find_by_filename(path));
 g_print("Renumber ranges native: real rectangles/multiple selections, reverse/main selection, Unicode/tabs/CRLF, virtual/empty rows, bounds, colored readonly preview, markers, exact outside bytes, Undo/Redo, text/selection/readonly/tab/close guards and synchronous modification/destruction passed\n");GeanyDocument *current=document_get_current();if(current) document_set_text_changed(current,FALSE);gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Renumber ranges timeout reply %u",reply);return G_SOURCE_REMOVE; }
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("DISPLAY")) return 77;
 gchar *dir=g_dir_make_tmp("renumber-ranges-native-XXXXXX",NULL),*profile=g_build_filename(dir,"profile",NULL);path=g_build_filename(dir,"fixture.c",NULL);g_assert_true(g_file_set_contents(path,original,-1,NULL));gchar *args[]={"renumber-ranges-native","--new-instance","--no-session","--config",profile,path,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,step,NULL);g_timeout_add_seconds(75,timeout,NULL);return main_lib(6,args);
}
