/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
static GtkWidget *window;
static ScintillaObject *sci;
static GtkWidget *named(GtkWidget *w,const gchar *name)
{
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;
 if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *children=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=children;p && !f;p=p->next) f=named(p->data,name);g_list_free(children);return f; }return NULL;
}
static void action(const gchar *id)
{
 gchar *name=g_strconcat("insight-nav-",id,NULL);GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item));g_free(name);
}
static void selection(gint anchor,gint caret)
{
 g_assert_cmpint(SEND(sci,SCI_GETANCHOR,0,0),==,anchor);g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,caret);
}
static gboolean wrapped_checks(gpointer data)
{
 (void)data;
 g_assert_cmpint(SEND(sci,SCI_WRAPCOUNT,0,0),>,1);
 SEND(sci,SCI_SETSEL,8,2);SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);
 gint display=SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0);action("scroll-half-down");
 g_assert_cmpint(SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),==,display+MAX(1,SEND(sci,SCI_LINESONSCREEN,0,0)/2));selection(8,2);
 SEND(sci,SCI_SETXOFFSET,0,0);action("scroll-right");g_assert_cmpint(SEND(sci,SCI_GETXOFFSET,0,0),==,0);
 SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);gint line=SEND(sci,SCI_DOCLINEFROMVISIBLE,50,0);gint target=sci_get_position_from_line(sci,line);action("window-top");selection(target,target);
 SEND(sci,SCI_SETSEL,8,2);SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);display=50+MAX(1,SEND(sci,SCI_LINESONSCREEN,0,0))-1;line=SEND(sci,SCI_DOCLINEFROMVISIBLE,display,0);target=sci_get_position_from_line(sci,line);action("window-bottom");selection(target,target);
 SEND(sci,SCI_SETREADONLY,0,0);sci_set_text(sci,"");SEND(sci,SCI_EMPTYUNDOBUFFER,0,0);SEND(sci,SCI_SETSAVEPOINT,0,0);
 action("window-top");action("window-bottom");action("scroll-half-down");action("scroll-line-up");selection(0,0);g_assert_cmpint(SEND(sci,SCI_GETLENGTH,0,0),==,0);g_assert_false(SEND(sci,SCI_CANUNDO,0,0));g_assert_false(SEND(sci,SCI_GETMODIFY,0,0));
 g_print("Viewport wrapped-row mapping, horizontal wrap guard and empty-buffer EOF passed\n");
 gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean step(gpointer data)
{
 (void)data;GeanyDocument *doc=document_get_current();if(!doc) return G_SOURCE_CONTINUE;
 sci=doc->editor->sci;window=gtk_widget_get_toplevel(GTK_WIDGET(sci));
 SEND(sci,SCI_SETWRAPMODE,SC_WRAP_NONE,0);SEND(sci,SCI_SETSCROLLWIDTH,10000,0);
 SEND(sci,SCI_SETSEL,8,2);SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);
 gint first=SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),page=MAX(1,SEND(sci,SCI_LINESONSCREEN,0,0)/2);
 action("scroll-line-down");g_assert_cmpint(SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),==,first+1);selection(8,2);
 action("scroll-line-up");g_assert_cmpint(SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),==,first);
 action("scroll-half-down");g_assert_cmpint(SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),==,first+page);selection(8,2);
 action("scroll-half-up");g_assert_cmpint(SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),==,first);
 SEND(sci,SCI_SETXOFFSET,0,0);action("scroll-left");g_assert_cmpint(SEND(sci,SCI_GETXOFFSET,0,0),==,0);
 gint pixels=SEND(sci,SCI_GETTABWIDTH,0,0)*SEND(sci,SCI_TEXTWIDTH,STYLE_DEFAULT,(sptr_t)" ");action("scroll-right");g_assert_cmpint(SEND(sci,SCI_GETXOFFSET,0,0),==,pixels);selection(8,2);action("scroll-left");g_assert_cmpint(SEND(sci,SCI_GETXOFFSET,0,0),==,0);
 SEND(sci,SCI_SETREADONLY,1,0);action("scroll-half-down");selection(8,2);
 SEND(sci,SCI_ADDSELECTION,16,12);action("scroll-line-down");g_assert_cmpint(SEND(sci,SCI_GETSELECTIONS,0,0),==,2);g_assert_cmpint(SEND(sci,SCI_GETSELECTIONNANCHOR,1,0),==,12);g_assert_cmpint(SEND(sci,SCI_GETSELECTIONNCARET,1,0),==,16);
 SEND(sci,SCI_CLEARSELECTIONS,0,0);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);SEND(sci,SCI_SETSEL,8,2);action("scroll-line-up");selection(8,2);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
 SEND(sci,SCI_SETFIRSTVISIBLELINE,0,0);action("scroll-line-up");g_assert_cmpint(SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),==,0);
 SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);SEND(sci,SCI_HIDELINES,1,30);first=SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0);action("scroll-line-down");g_assert_cmpint(SEND(sci,SCI_GETFIRSTVISIBLELINE,0,0),==,first+1);SEND(sci,SCI_SHOWLINES,1,30);
 SEND(sci,SCI_SETSEL,8,2);SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);
 gint target=sci_get_position_from_line(sci,50);action("window-top");selection(target,target);
 SEND(sci,SCI_SETSEL,8,2);SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);
 gint bottom=50+MAX(1,SEND(sci,SCI_LINESONSCREEN,0,0))-1;target=sci_get_position_from_line(sci,bottom);action("window-bottom");selection(target,target);
 GtkWidget *nav=gtk_menu_item_get_submenu(GTK_MENU_ITEM(named(window,"insight-selection-navigation")));
 SEND(sci,SCI_SETSEL,8,2);g_signal_emit_by_name(nav,"show");gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-extend-mode")),TRUE);
 SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);action("window-top");selection(8,sci_get_position_from_line(sci,50));g_assert_true(SEND(sci,SCI_GETMOVEEXTENDSSELECTION,0,0));
 gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(named(window,"insight-extend-mode")),FALSE);
 SEND(sci,SCI_HIDELINES,1,30);SEND(sci,SCI_SETFIRSTVISIBLELINE,50,0);target=sci_get_position_from_line(sci,SEND(sci,SCI_DOCLINEFROMVISIBLE,50,0));action("window-top");selection(target,target);SEND(sci,SCI_SHOWLINES,1,30);
 SEND(sci,SCI_SETSEL,8,2);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_RECTANGLE,0);action("window-top");selection(8,2);SEND(sci,SCI_SETSELECTIONMODE,SC_SEL_STREAM,0);
 SEND(sci,SCI_SETSEL,8,2);SEND(sci,SCI_ADDSELECTION,16,12);gint caret=SEND(sci,SCI_GETCURRENTPOS,0,0);action("window-bottom");g_assert_cmpint(SEND(sci,SCI_GETCURRENTPOS,0,0),==,caret);SEND(sci,SCI_CLEARSELECTIONS,0,0);
 SEND(sci,SCI_SETSEL,0,0);SEND(sci,SCI_SETWRAPMODE,SC_WRAP_CHAR,0);SEND(sci,SCI_SETFIRSTVISIBLELINE,0,0);action("window-top");selection(0,0);
 g_assert_false(SEND(sci,SCI_CANUNDO,0,0));g_assert_false(SEND(sci,SCI_GETMODIFY,0,0));
 SEND(sci,SCI_SETREADONLY,0,0);g_print("Viewport GUI: line/half-page/pixel scroll, reverse/multiple/rectangular selections, read-only, folded rows and bounds passed\n");
 g_timeout_add(200,wrapped_checks,NULL);return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer data) { (void)data;g_error("Viewport GUI timeout");return G_SOURCE_REMOVE; }
int main(void)
{
 if(!g_getenv("GEANY_RUN_GUI_TESTS") || !g_getenv("DISPLAY")) return 77;
 gchar *directory=g_dir_make_tmp("viewport-XXXXXX",NULL),*config=g_build_filename(directory,"geany.conf",NULL),*source=g_build_filename(directory,"source.txt",NULL);g_assert_true(g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL));
 GString *text=g_string_new(NULL);for(guint i=0;i<300;i++) { g_string_append_printf(text,"αβ %03u ",i);for(guint j=0;j<30;j++) g_string_append(text,"long viewport test line ");g_string_append(text,"\r\n"); }g_assert_true(g_file_set_contents(source,text->str,text->len,NULL));g_string_free(text,TRUE);
 gchar *args[]={"viewport-test","--new-instance","--no-session","--config",directory,source,NULL};g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(500,step,NULL);g_timeout_add_seconds(30,timeout,NULL);return main_lib(6,args);
}
