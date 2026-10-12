/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include "geany.h"
#include "insight-workbench.h"
#include "ui_utils.h"
#include "document.h"
#include "editor.h"
#include "filetypes.h"
#include "highlighting.h"
#include "sciwrappers.h"
#include "utils.h"
#include "navqueue.h"
#include "tagmanager/tm_workspace.h"
#include <string.h>

typedef struct {
 GtkWidget *pane,*box,*title;
 ScintillaObject *preview;
 guint refresh;
} Workbench;
static Workbench wb;
static void render_source(const char *text,GeanyFiletype *filetype,gulong line) {
 highlighting_set_styles(wb.preview,filetype ? filetype : filetypes[GEANY_FILETYPES_NONE]);
 sci_set_readonly(wb.preview,FALSE); sci_set_text(wb.preview,text);
 SSM(wb.preview,SCI_COLOURISE,0,-1);
 sci_set_line_numbers(wb.preview,TRUE);
 SSM(wb.preview,SCI_SETCARETLINEVISIBLE,TRUE,0);
 SSM(wb.preview,SCI_SETCARETLINEBACK,0xe9f0ff,0);
 SSM(wb.preview,SCI_EMPTYUNDOBUFFER,0,0); sci_set_readonly(wb.preview,TRUE);
 gint target=(gint)MIN(line ? line-1 : 0,(gulong)MAX(0,sci_get_line_count(wb.preview)-1));
 SSM(wb.preview,SCI_GOTOLINE,target,0);
 SSM(wb.preview,SCI_VERTICALCENTRECARET,0,0);
}
static TMTag *word_definition(GeanyDocument *doc,const char *word) {
 if(!word || !*word || !doc || !doc->tm_file || !doc->tm_file->tags_array) return NULL;
 GPtrArray *tags=doc->tm_file->tags_array;
 for(guint i=0;i<tags->len;i++) {
  TMTag *tag=g_ptr_array_index(tags,i);
  if(tag->name && !strcmp(tag->name,word) && tag->line) return tag;
 }
 return NULL;
}
static gboolean refresh_now(gpointer data) {
 (void)data; wb.refresh=0;
 if(!wb.preview) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current();
 if(!doc) { render_source("",NULL,1); return G_SOURCE_REMOVE; }
 gint length=sci_get_length(doc->editor->sci);
 if(length>2*1024*1024) { gtk_label_set_text(GTK_LABEL(wb.title),"Context: file exceeds 2 MiB"); return G_SOURCE_REMOVE; }
 gchar *text=sci_get_contents(doc->editor->sci,length+1);
 if(text && g_utf8_validate(text,length,NULL) && !memchr(text,0,(gsize)length))
  {
  gchar word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
  TMTag *definition=word_definition(doc,word);
  gulong line=definition ? definition->line : (gulong)sci_get_current_line(doc->editor->sci)+1;
  render_source(text,doc->file_type,line);
  gchar *label=g_strdup_printf("Context — %s%s • %s:%lu",definition ? word : "Current source",definition ? " (definition)" : "",doc->file_name ? doc->file_name : "untitled",line);
  gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label);
 }
 g_free(text); return G_SOURCE_REMOVE;
}
void insight_workbench_refresh(void) {
 if(!wb.preview) return;
 if(wb.refresh) g_source_remove(wb.refresh);
 wb.refresh=g_timeout_add(60,refresh_now,NULL);
}
void insight_workbench_init(void) {
 GtkWidget *existing=ui_lookup_widget(main_widgets.window,"vpaned1");
 GtkWidget *parent=gtk_widget_get_parent(existing); gint position=0;
 gtk_container_child_get(GTK_CONTAINER(parent),existing,"position",&position,NULL);
 wb.pane=gtk_paned_new(GTK_ORIENTATION_VERTICAL);
 wb.box=gtk_box_new(GTK_ORIENTATION_VERTICAL,2);
 wb.title=gtk_label_new("Context — integrated source navigation");
 gtk_label_set_xalign(GTK_LABEL(wb.title),0.0f);
 gtk_box_pack_start(GTK_BOX(wb.box),wb.title,FALSE,FALSE,4);
 wb.preview=SCINTILLA(scintilla_new());
 sci_set_codepage(wb.preview,SC_CP_UTF8);
 SSM(wb.preview,SCI_SETUNDOCOLLECTION,0,0);
 SSM(wb.preview,SCI_SETMARGINWIDTHN,1,0); SSM(wb.preview,SCI_SETMARGINWIDTHN,2,0);
 SSM(wb.preview,SCI_SETWRAPMODE,SC_WRAP_NONE,0);
 gtk_widget_set_direction(GTK_WIDGET(wb.preview),GTK_TEXT_DIR_LTR);
 gtk_box_pack_start(GTK_BOX(wb.box),GTK_WIDGET(wb.preview),TRUE,TRUE,0);
 gtk_widget_set_size_request(wb.box,-1,170);
 g_object_ref(existing); gtk_container_remove(GTK_CONTAINER(parent),existing);
 gtk_paned_pack1(GTK_PANED(wb.pane),existing,TRUE,FALSE); g_object_unref(existing);
 gtk_paned_pack2(GTK_PANED(wb.pane),wb.box,FALSE,FALSE);
 gtk_box_pack_start(GTK_BOX(parent),wb.pane,TRUE,TRUE,0);
 gtk_box_reorder_child(GTK_BOX(parent),wb.pane,position);
 gtk_paned_set_position(GTK_PANED(wb.pane),430);
 gtk_widget_show_all(wb.pane);
 insight_workbench_refresh();
}
void insight_workbench_finalize(void) {
 if(wb.refresh) g_source_remove(wb.refresh);
 memset(&wb,0,sizeof wb);
}
