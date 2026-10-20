/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "editor.h"
#include "main.h"
#include "ui_utils.h"
#include "sciwrappers.h"
#include "keybindings.h"
#include <string.h>
#define sci_set_selection(s,a,b) scintilla_send_message(s,SCI_SETSEL,a,b)
#define sci_set_readonly(s,v) scintilla_send_message(s,SCI_SETREADONLY,v,0)
#define sci_undo(s) scintilla_send_message(s,SCI_UNDO,0,0)
#define sci_redo(s) scintilla_send_message(s,SCI_REDO,0,0)
#define document_grab_focus(d) gtk_widget_grab_focus(GTK_WIDGET((d)->editor->sci))
static void check(ScintillaObject *sci,const gchar *expected) { gchar *text=sci_get_contents(sci,-1);g_assert_cmpstr(text,==,expected);g_free(text); }
static gboolean step(gpointer data) {
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 ScintillaObject *sci=doc->editor->sci;
 const gchar *inputs[]={"ab\ncd\nef","ab\ncd\nef","αβ γδ","a  b","alpha\nbeta\n","a\r\nb\r\nc"};
 const gchar *expected[]={"ab\ncdb\ncd\nef","ab\ncdb\ncd\nef","αβαβ γδ","a    b","alpha\nalpha\nbeta\n","a\r\nba\r\nb\r\nc"};
 const gint starts[]={1,5,0,1,2,0},ends[]={5,1,4,3,2,4};
 for(guint i=0;i<G_N_ELEMENTS(inputs);i++) {
  sci_set_text(sci,inputs[i]);scintilla_send_message(sci,SCI_EMPTYUNDOBUFFER,0,0);sci_set_selection(sci,starts[i],ends[i]);document_grab_focus(doc);
  keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_DUPLICATELINE);check(sci,expected[i]);
  sci_undo(sci);check(sci,inputs[i]);sci_redo(sci);check(sci,expected[i]);
 }
 sci_set_text(sci,"word");sci_set_selection(sci,0,4);sci_set_readonly(sci,TRUE);document_grab_focus(doc);keybindings_send_command(GEANY_KEY_GROUP_EDITOR,GEANY_KEYS_EDITOR_DUPLICATELINE);check(sci,"word");sci_set_readonly(sci,FALSE);
 scintilla_send_message(sci,SCI_SETSAVEPOINT,0,0);
 g_print("Exact Duplicate: partial multiline, reverse selection, UTF-8, spaces, empty-selection line fallback, CRLF, read-only, Undo/Redo passed\n");
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(gtk_widget_get_toplevel(GTK_WIDGET(sci)),"menu_quit1")));return G_SOURCE_REMOVE;
}
int main(void) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *profile=g_dir_make_tmp("duplicate-selection-XXXXXX",NULL),*config=g_build_filename(profile,"geany.conf",NULL);
 g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));g_free(config);
 gchar *args[]={"duplicate-selection","--new-instance","--no-session","--config",profile,NULL};g_timeout_add(600,step,NULL);return main_lib(5,args);
}
