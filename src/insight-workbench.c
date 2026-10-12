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
#include <gio/gio.h>

typedef struct {
 GtkWidget *pane,*box,*title;
 ScintillaObject *preview;
 guint refresh,source_id;
 gchar *source_path; gulong source_line;
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
 if(!word || !*word || !doc) return NULL;
 GPtrArray *tags=tm_get_workspace()->tags_array; TMTag *fallback=NULL;
 for(guint i=0;i<tags->len;i++) {
  TMTag *tag=g_ptr_array_index(tags,i);
  if(!tag->name || strcmp(tag->name,word) || !tag->file || !tag->line) continue;
  if(tag->file==doc->tm_file) return tag;
  if(!fallback) fallback=tag;
 }
 return fallback;
}
static char *read_disk_source(const char *utf8) {
 gchar *native=utils_get_locale_from_utf8(utf8); GFile *file=g_file_new_for_path(native); g_free(native);
 GFileInputStream *stream=g_file_read(file,NULL,NULL); g_object_unref(file);
 if(!stream) return NULL;
 GString *text=g_string_sized_new(16384); char chunk[16384]; gboolean ok=TRUE;
 for(;;) {
  gssize got=g_input_stream_read(G_INPUT_STREAM(stream),chunk,sizeof chunk,NULL,NULL);
  if(got<0 || (got>0 && text->len+(gsize)got>2u*1024u*1024u)) { ok=FALSE; break; }
  if(!got) break;
  g_string_append_len(text,chunk,got);
 }
 g_object_unref(stream);
 if(!ok || !g_utf8_validate(text->str,(gssize)text->len,NULL) || memchr(text->str,0,text->len)) { g_string_free(text,TRUE); return NULL; }
 return g_string_free(text,FALSE);
}
static gboolean show_location(const char *path,guint id,gulong line) {
 GeanyDocument *doc=id ? document_find_by_id(id) : (path ? document_find_by_filename(path) : NULL);
 char *text=NULL; GeanyFiletype *ft=NULL;
 if(doc) {
  gint length=sci_get_length(doc->editor->sci);
  if(length<=2*1024*1024) text=sci_get_contents(doc->editor->sci,length+1);
  if(text && (!g_utf8_validate(text,length,NULL) || memchr(text,0,(gsize)length))) g_clear_pointer(&text,g_free);
  ft=doc->file_type;
 } else if(path) { text=read_disk_source(path); ft=filetypes_detect_from_file(path); }
 if(!text) {
  render_source("",NULL,1); gtk_label_set_text(GTK_LABEL(wb.title),"Context unavailable: requires a UTF-8 source file up to 2 MiB");
  g_clear_pointer(&wb.source_path,g_free); wb.source_id=0; wb.source_line=0; return FALSE;
 }
 gchar *owned_path=g_strdup(path ? path : (doc ? doc->file_name : NULL));
 render_source(text,ft,line); g_free(text);
 g_free(wb.source_path); wb.source_path=owned_path; wb.source_id=doc ? doc->id : 0; wb.source_line=line;
 gchar *label=g_strdup_printf("Context — %s:%lu",owned_path ? owned_path : "untitled",line);
 gtk_label_set_text(GTK_LABEL(wb.title),label); g_free(label); return TRUE;
}
static gboolean refresh_now(gpointer data) {
 (void)data; wb.refresh=0;
 if(!wb.preview) return G_SOURCE_REMOVE;
 GeanyDocument *doc=document_get_current();
 if(!doc) { render_source("",NULL,1); return G_SOURCE_REMOVE; }
 gchar word[1025]={0}; editor_find_current_word_sciwc(doc->editor,sci_get_current_position(doc->editor->sci),word,sizeof word);
 TMTag *definition=word_definition(doc,word);
 if(definition) {
  gchar *path=utils_get_utf8_from_locale(definition->file->file_name);
  gulong line=definition->line; show_location(path,0,line); g_free(path);
 } else show_location(doc->file_name,doc->id,(gulong)sci_get_current_line(doc->editor->sci)+1);
 return G_SOURCE_REMOVE;
}
static gboolean preview_button(GtkWidget *widget,GdkEventButton *event,gpointer data) {
 (void)widget; (void)data;
 if(event->button!=1 || !(event->state&GDK_CONTROL_MASK)) return FALSE;
 gint position=(gint)SSM(wb.preview,SCI_POSITIONFROMPOINTCLOSE,(uptr_t)event->x,(sptr_t)event->y);
 if(position<0) return FALSE;
 gint start=(gint)SSM(wb.preview,SCI_WORDSTARTPOSITION,position,TRUE),end=(gint)SSM(wb.preview,SCI_WORDENDPOSITION,position,TRUE);
 if(end<=start || end-start>1024) return TRUE;
 char word[1025]; for(gint i=start;i<end;i++) word[i-start]=(char)sci_get_char_at(wb.preview,i); word[end-start]=0;
 GeanyDocument *source=wb.source_id ? document_find_by_id(wb.source_id) : document_get_current();
 TMTag *definition=word_definition(source,word);
 if(definition) {
  gchar *path=utils_get_utf8_from_locale(definition->file->file_name); gulong line=definition->line;
  show_location(path,0,line); g_free(path);
 } else ui_set_statusbar(TRUE,"Definition not found: %s",word);
 return TRUE;
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
 g_signal_connect(wb.preview,"button-press-event",G_CALLBACK(preview_button),NULL);
 gtk_widget_set_tooltip_text(GTK_WIDGET(wb.preview),"Ctrl+click an identifier to follow its definition");
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
 g_free(wb.source_path);
 memset(&wb,0,sizeof wb);
}
