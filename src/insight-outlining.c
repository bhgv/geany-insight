/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-outlining.h"
#include "document.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "navqueue.h"
#include "insight-help.h"
#include "insight-i18n.h"
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define GENERATION "insight-outlining-generation"
#define TEXT_LIMIT (2*1024*1024)
#define LINE_LIMIT 100000
#define BLOCK_LIMIT 5000

typedef struct { gint line,level,depth;gchar *text; } Block;
typedef struct { guint id;guint64 generation;GeanyFiletype *type;GPtrArray *blocks; } Snapshot;
static GtkWidget *browser,*query,*view,*status;
static GtkListStore *model;
static Snapshot snapshot;
static gboolean alive;
static guint64 *generation(ScintillaObject *sci) {
 guint64 *v=g_object_get_data(G_OBJECT(sci),GENERATION);
 if(!v) { v=g_new0(guint64,1);g_object_set_data_full(G_OBJECT(sci),GENERATION,v,g_free); }
 return v;
}
void insight_outlining_notify(GeanyEditor *editor,SCNotification *nt) {
 if(nt->nmhdr.code==SCN_MODIFIED && (nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) (*generation(editor->sci))++;
}
static void block_free(gpointer data) { Block *b=data;g_free(b->text);g_free(b); }
static void snapshot_clear(Snapshot *s) { g_clear_pointer(&s->blocks,g_ptr_array_unref);memset(s,0,sizeof *s); }
static gboolean capture(Snapshot *s) {
 GeanyDocument *doc=document_get_current();
 if(!DOC_VALID(doc)) return FALSE;
 ScintillaObject *sci=doc->editor->sci;
 if(sci_get_length(sci)>TEXT_LIMIT || sci_get_line_count(sci)>LINE_LIMIT) { ui_set_statusbar(TRUE,"%s",insight_tr("Code outlining supports up to 2 MiB and 100000 lines"));return FALSE; }
 SEND(sci,SCI_COLOURISE,0,-1);
 s->id=doc->id;s->type=doc->file_type;s->generation=*generation(sci);s->blocks=g_ptr_array_new_with_free_func(block_free);
 for(gint line=0;line<sci_get_line_count(sci);line++) {
  gint level=SEND(sci,SCI_GETFOLDLEVEL,line,0);if(!(level&SC_FOLDLEVELHEADERFLAG)) continue;
  if(s->blocks->len==BLOCK_LIMIT) goto limit;
  Block *b=g_new0(Block,1);b->line=line;b->level=level;
  gint parent=line;while((parent=SEND(sci,SCI_GETFOLDPARENT,parent,0))>=0) { if(++b->depth>256) { g_free(b);goto limit; } }
  gchar *text=sci_get_line(sci,line);g_strstrip(text);const gchar *end=text+MIN(strlen(text),(gsize)512);while(end>text && !g_utf8_validate(text,end-text,NULL)) end--;b->text=g_strndup(text,end-text);g_free(text);g_ptr_array_add(s->blocks,b);
 }
 return TRUE;
limit:
 snapshot_clear(s);ui_set_statusbar(TRUE,"%s",insight_tr("Code outline limit reached: 5000 blocks or 256 levels"));return FALSE;
}
static GeanyDocument *valid(Snapshot *s) {
 GeanyDocument *doc=document_find_by_id(s->id);
 if(!DOC_VALID(doc) || doc!=document_get_current() || doc->file_type!=s->type || *generation(doc->editor->sci)!=s->generation) return NULL;
 return doc;
}
static void state_rows(void) {
 GeanyDocument *doc=valid(&snapshot);if(!doc) return;
 GtkTreeIter it;for(gboolean ok=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model),&it);ok;ok=gtk_tree_model_iter_next(GTK_TREE_MODEL(model),&it)) { gint index;gtk_tree_model_get(GTK_TREE_MODEL(model),&it,4,&index,-1);Block *b=g_ptr_array_index(snapshot.blocks,index);gtk_list_store_set(model,&it,2,insight_tr(SEND(doc->editor->sci,SCI_GETFOLDEXPANDED,b->line,0) ? "Expanded" : "Collapsed"),-1); }
}
static void filter(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;gtk_list_store_clear(model);if(!snapshot.blocks) return;
 gchar *needle=g_utf8_casefold(gtk_entry_get_text(GTK_ENTRY(query)),-1);guint shown=0;
 for(guint i=0;i<snapshot.blocks->len;i++) { Block *b=g_ptr_array_index(snapshot.blocks,i);gchar *hay=g_utf8_casefold(b->text,-1);gboolean match=strstr(hay,needle)!=NULL;g_free(hay);if(!match) continue;GtkTreeIter it;gtk_list_store_append(model,&it);gtk_list_store_set(model,&it,0,b->line+1,1,b->depth+1,3,b->text,4,i,-1);shown++; }
 gchar *label=g_strdup_printf("%u / %u",shown,snapshot.blocks->len);gtk_label_set_text(GTK_LABEL(status),label);g_free(label);g_free(needle);state_rows();
}
static void refresh(GtkWidget *widget,gpointer data) {
 (void)widget;(void)data;Snapshot next={0};if(!capture(&next)) { gtk_label_set_text(GTK_LABEL(status),insight_tr("Cannot capture code outline"));return; }snapshot_clear(&snapshot);snapshot=next;GeanyDocument *doc=valid(&snapshot);gchar *name=doc && doc->file_name ? g_path_get_basename(doc->file_name):g_strdup(insight_tr("Untitled"));gchar *title=g_strdup_printf("%s — %s",insight_tr("Code outline"),name);gtk_window_set_title(GTK_WINDOW(browser),title);g_free(title);g_free(name);filter(NULL,NULL);
}
static Block *selected(void) {
 GtkTreeModel *m;GtkTreeIter it;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&m,&it)) return NULL;
 gint index;gtk_tree_model_get(m,&it,4,&index,-1);return snapshot.blocks && index>=0 && (guint)index<snapshot.blocks->len ? g_ptr_array_index(snapshot.blocks,index):NULL;
}
static gchar *escaped_field(const gchar *text) {
 GString *out=g_string_new("");for(const gchar *p=text;*p;p++) { if(*p=='\t') g_string_append(out,"\\t");else if(*p=='\r') g_string_append(out,"\\r");else if(*p=='\n') g_string_append(out,"\\n");else if(*p=='\\') g_string_append(out,"\\\\");else if((guchar)*p<32 || (guchar)*p==127) g_string_append_printf(out,"\\%03o",(guchar)*p);else g_string_append_c(out,*p); }return g_string_free(out,FALSE);
}
static void browser_action(GtkWidget *widget,gpointer data) {
 (void)widget;guint action=GPOINTER_TO_UINT(data);GeanyDocument *doc=valid(&snapshot);Block *b=selected();
 if(!doc || (b && SEND(doc->editor->sci,SCI_GETFOLDLEVEL,b->line,0)!=b->level)) { gtk_label_set_text(GTK_LABEL(status),insight_tr("Source changed or switched; refresh the code outline"));return; }
 if(action==5) { GString *out=g_string_new("");GtkTreeIter it;for(gboolean ok=gtk_tree_model_get_iter_first(GTK_TREE_MODEL(model),&it);ok;ok=gtk_tree_model_iter_next(GTK_TREE_MODEL(model),&it)) { gint index;gtk_tree_model_get(GTK_TREE_MODEL(model),&it,4,&index,-1);Block *block=g_ptr_array_index(snapshot.blocks,index);gchar *text=escaped_field(block->text);g_string_append_printf(out,"%d\t%d\t%s\n",block->line+1,block->depth+1,text);g_free(text); }gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),out->str,out->len);g_string_free(out,TRUE);return; }
 if(!b) return;
 if(action==0) { navqueue_goto_line(document_get_current(),doc,b->line+1);SEND(doc->editor->sci,SCI_ENSUREVISIBLE,b->line,0);document_grab_focus(doc);gtk_window_present(GTK_WINDOW(main_widgets.window));state_rows(); }
 else { SEND(doc->editor->sci,action>=3 ? SCI_FOLDCHILDREN:SCI_FOLDLINE,b->line,action==1 || action==3 ? SC_FOLDACTION_EXPAND:SC_FOLDACTION_CONTRACT);state_rows(); }
}
static void activated(GtkTreeView *tree,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data) { (void)tree;(void)path;(void)column;(void)data;browser_action(NULL,NULL); }
static void destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;browser=query=view=status=NULL;g_clear_object(&model);snapshot_clear(&snapshot); }
static void close_browser(GtkWidget *widget,gpointer data) { (void)widget;(void)data;gtk_widget_destroy(browser); }
static void show_browser(void) {
 if(browser) { gtk_window_present(GTK_WINDOW(browser));return; }
 browser=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(browser,"insight-code-outline-window");gtk_window_set_title(GTK_WINDOW(browser),insight_tr("Code outline"));gtk_window_set_transient_for(GTK_WINDOW(browser),GTK_WINDOW(main_widgets.window));gtk_window_set_destroy_with_parent(GTK_WINDOW(browser),TRUE);gtk_window_set_default_size(GTK_WINDOW(browser),760,460);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_container_set_border_width(GTK_CONTAINER(box),6);gtk_container_add(GTK_CONTAINER(browser),box);query=gtk_search_entry_new();gtk_entry_set_max_length(GTK_ENTRY(query),256);gtk_widget_set_name(query,"insight-code-outline-query");gtk_box_pack_start(GTK_BOX(box),query,FALSE,FALSE,0);
 model=gtk_list_store_new(5,G_TYPE_INT,G_TYPE_INT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(model));gtk_widget_set_name(view,"insight-code-outline-list");const gchar *columns[]={"Line","Level","State","Source"};for(guint i=0;i<4;i++) { GtkTreeViewColumn *c=gtk_tree_view_column_new_with_attributes(insight_tr(columns[i]),gtk_cell_renderer_text_new(),"text",i,NULL);gtk_tree_view_column_set_resizable(c,TRUE);gtk_tree_view_column_set_sort_column_id(c,i);gtk_tree_view_append_column(GTK_TREE_VIEW(view),c); }
 GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);status=gtk_label_new("");gtk_box_pack_start(GTK_BOX(box),status,FALSE,FALSE,0);GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3),*folds=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,3);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),folds,FALSE,FALSE,0);
 const gchar *labels[]={"Go To","Expand block","Collapse block","Expand branch","Collapse branch","Copy List","Refresh","Close"};for(guint i=0;i<8;i++) { GtkWidget *button=gtk_button_new_with_label(insight_tr(labels[i]));gchar *name=g_strdup_printf("insight-code-outline-action-%u",i);gtk_widget_set_name(button,name);g_free(name);gtk_box_pack_start(GTK_BOX(i>=1 && i<=4 ? folds:bar),button,FALSE,FALSE,0);g_signal_connect(button,"clicked",i==6 ? G_CALLBACK(refresh):i==7 ? G_CALLBACK(close_browser):G_CALLBACK(browser_action),GUINT_TO_POINTER(i)); }
 gtk_box_pack_start(GTK_BOX(box),insight_panel_help_button("code-outlining.md"),FALSE,FALSE,0);g_signal_connect(query,"changed",G_CALLBACK(filter),NULL);g_signal_connect(view,"row-activated",G_CALLBACK(activated),NULL);g_signal_connect(browser,"destroy",G_CALLBACK(destroyed),NULL);refresh(NULL,NULL);gtk_widget_show_all(browser);gtk_widget_grab_focus(query);
}
void insight_outlining_command(guint action) {
 if(!alive || action>7) return;if(action==7) { show_browser();return; }
 Snapshot s={0};if(!capture(&s)) return;GeanyDocument *doc=valid(&s);if(!doc) { snapshot_clear(&s);return; }ScintillaObject *sci=doc->editor->sci;
 if(!s.blocks->len) { ui_set_statusbar(TRUE,"%s",insight_tr("The active lexer provides no fold blocks"));snapshot_clear(&s);return; }
 if(action==6) {
  GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Fold to level"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel"),GTK_RESPONSE_CANCEL,insight_tr("Apply"),GTK_RESPONSE_APPLY,NULL);gtk_widget_set_name(dialog,"insight-fold-level-dialog");GtkWidget *spin=gtk_spin_button_new_with_range(1,12,1);gtk_widget_set_name(spin,"insight-fold-level");gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),gtk_label_new(insight_tr("Keep outer levels expanded; collapse deeper blocks")),FALSE,FALSE,4);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),spin,FALSE,FALSE,4);gtk_widget_show_all(dialog);gint response=gtk_dialog_run(GTK_DIALOG(dialog)),depth=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));gtk_widget_destroy(dialog);
  if(response==GTK_RESPONSE_APPLY && valid(&s)) { SEND(sci,SCI_FOLDALL,SC_FOLDACTION_EXPAND,0);for(gint i=(gint)s.blocks->len-1;i>=0;i--) { Block *b=g_ptr_array_index(s.blocks,i);if(b->depth>=depth-1) SEND(sci,SCI_FOLDLINE,b->line,SC_FOLDACTION_CONTRACT); } }
 } else if(action>=4) SEND(sci,SCI_FOLDALL,action==4 ? SC_FOLDACTION_EXPAND:(SC_FOLDACTION_CONTRACT|SC_FOLDACTION_CONTRACT_EVERY_LEVEL),0);
 else { gint line=sci_get_current_line(sci);if(!(SEND(sci,SCI_GETFOLDLEVEL,line,0)&SC_FOLDLEVELHEADERFLAG)) line=SEND(sci,SCI_GETFOLDPARENT,line,0);if(line>=0) SEND(sci,action>=2 ? SCI_FOLDCHILDREN:SCI_FOLDLINE,line,action%2 ? SC_FOLDACTION_CONTRACT:SC_FOLDACTION_EXPAND); }
 snapshot_clear(&s);if(browser) state_rows();
}
static void command(GtkMenuItem *item,gpointer data) { (void)item;insight_outlining_command(GPOINTER_TO_UINT(data)); }
void insight_outlining_init(void) {
 if(alive) return;alive=TRUE;GtkWidget *menus[]={ui_lookup_widget(main_widgets.window,"menu_view1_menu"),main_widgets.editor_menu};const gchar *labels[]={"Expand block","Collapse block","Expand branch","Collapse branch","Expand all code","Collapse all code","Fold to level…","Code outline…"};
 for(guint m=0;m<2;m++) { GtkWidget *root=gtk_menu_item_new_with_label(insight_tr("Code outlining")),*sub=gtk_menu_new();gtk_menu_item_set_submenu(GTK_MENU_ITEM(root),sub);gtk_menu_shell_append(GTK_MENU_SHELL(menus[m]),root);for(guint i=0;i<8;i++) { GtkWidget *item=gtk_menu_item_new_with_label(insight_tr(labels[i]));gchar *name=g_strdup_printf("insight-outlining-%s-%u",m ? "popup":"command",i);gtk_widget_set_name(item,name);g_free(name);gtk_menu_shell_append(GTK_MENU_SHELL(sub),item);g_signal_connect(item,"activate",G_CALLBACK(command),GUINT_TO_POINTER(i));if(!m) keybindings_attach_menu(GEANY_KEY_GROUP_DOCUMENT,GEANY_KEYS_INSIGHT_EXPANDBLOCK+i,item); }gtk_widget_show_all(root); }
}
void insight_outlining_finalize(void) { alive=FALSE;if(browser) gtk_widget_destroy(browser); }
