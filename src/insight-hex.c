/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-hex.h"
#include "insight-compare-ui.h"
#include "insight-i18n.h"
#include <gio/gio.h>
#include <string.h>
#define LIMIT (16u*1024u*1024u)
#define PAGE 4096u
typedef struct {
 GtkWidget *window,*path[2],*view[3],*status,*page,*word,*endian,*offset,*length,*target,*remove,*edit,*chunk,*chunk_size,*part;
 GBytes *source[2];GByteArray *result;GPtrArray *history;guint position;gboolean dirty,scrolling;
 GtkAdjustment *scroll[2];
} Hex;
static void note(Hex *h,const gchar *text) { gtk_label_set_text(GTK_LABEL(h->status),text);gtk_widget_set_tooltip_text(h->status,text); }
static GtkWidget *spin(GtkWidget *box,const gchar *tip,guint max) {
 GtkWidget *w=gtk_spin_button_new_with_range(0,max,1);gtk_entry_set_width_chars(GTK_ENTRY(w),9);gtk_widget_set_tooltip_text(w,insight_tr(tip));gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,0);return w;
}
static guint value(GtkWidget *w) { return gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(w)); }
static gchar *dump(const guint8 *data,gsize size,gsize start,guint word,gboolean big) {
 GString *s=g_string_new(NULL);gsize end=MIN(size,start+PAGE);
 for(gsize row=start;row<end;row+=16) {
  g_string_append_printf(s,"%08" G_GSIZE_MODIFIER "X  ",row);
  for(guint j=0;j<16;j+=word) {
   for(guint k=0;k<word;k++) { gsize p=row+j+(big ? k:word-1-k);if(p<end) g_string_append_printf(s,"%02X",data[p]);else g_string_append(s,"  "); }g_string_append_c(s,' ');
  }
  g_string_append(s," | ");for(guint j=0;j<16 && row+j<end;j++) { guint8 c=data[row+j];g_string_append_c(s,c>=32 && c<=126 ? c:'.'); }g_string_append_c(s,'\n');
 }
 return g_string_free(s,FALSE);
}
static void render(Hex *h) {
 guint word=1u<<gtk_combo_box_get_active(GTK_COMBO_BOX(h->word));gboolean big=gtk_combo_box_get_active(GTK_COMBO_BOX(h->endian))==1;
 gsize start=(gsize)value(h->page)*PAGE;
 for(guint side=0;side<3;side++) {
  gsize n=0;const guint8 *p=side<2 ? g_bytes_get_data(h->source[side],&n):h->result->data;if(side==2) n=h->result->len;
  gchar *text=dump(p,n,start,word,big);GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(h->view[side]));gtk_text_buffer_set_text(b,text,-1);g_free(text);
  /* Highlight changed byte rows, independently of word display/endian. */
  GtkTextTag *tag=gtk_text_tag_table_lookup(gtk_text_buffer_get_tag_table(b),"changed");if(!tag) tag=gtk_text_buffer_create_tag(b,"changed","background","#f7d69b","foreground","#202020",NULL);
  gsize other_n;const guint8 *other=g_bytes_get_data(h->source[side==0 ? 1:0],&other_n);
  for(gsize row=start;row<MIN(n,start+PAGE);row+=16) { gsize len=MIN((gsize)16,n-row);if(row+len>other_n || memcmp(p+row,other+row,len)) { GtkTextIter a,z;gtk_text_buffer_get_iter_at_line(b,&a,(row-start)/16);z=a;gtk_text_iter_forward_line(&z);gtk_text_buffer_apply_tag(b,tag,&a,&z); } }
 }
 gchar *status=g_strdup_printf("Left: %" G_GSIZE_FORMAT " bytes | Right: %" G_GSIZE_FORMAT " bytes | Result: %u bytes%s | Page %u (4096 bytes)",g_bytes_get_size(h->source[0]),g_bytes_get_size(h->source[1]),h->result->len,h->dirty ? " *":"",value(h->page)+1);note(h,status);g_free(status);
}
static void view_changed(GtkWidget *w,gpointer data) { (void)w;render(data); }
static void remember(Hex *h) {
 while(h->history->len>h->position+1) g_ptr_array_remove_index(h->history,h->history->len-1);
 g_ptr_array_add(h->history,g_bytes_new(h->result->data,h->result->len));h->position=h->history->len-1;
 gsize bytes=0;for(guint i=0;i<h->history->len;i++) bytes+=g_bytes_get_size(g_ptr_array_index(h->history,i));
 while(h->history->len>1 && (bytes>32u*1024u*1024u || h->history->len>65)) { bytes-=g_bytes_get_size(g_ptr_array_index(h->history,0));g_ptr_array_remove_index(h->history,0);h->position--; }
 h->dirty=TRUE;render(h);
}
static void history(GtkWidget *w,gpointer data) {
 Hex *h=data;gboolean redo=GPOINTER_TO_INT(g_object_get_data(G_OBJECT(w),"redo"));
 if((redo && h->position+1>=h->history->len) || (!redo && !h->position)) return;
 h->position+=redo ? 1:-1;GBytes *b=g_ptr_array_index(h->history,h->position);gsize n;const guint8 *p=g_bytes_get_data(b,&n);g_byte_array_set_size(h->result,0);if(n) g_byte_array_append(h->result,p,n);h->dirty=TRUE;render(h);
}
static gboolean replace(Hex *h,const guint8 *p,gsize n) {
 guint at=value(h->target),remove=value(h->remove);
 if(at>h->result->len || remove>h->result->len-at || n>LIMIT-(h->result->len-remove)) { note(h,insight_tr("Range outside Result or result exceeds 16 MiB."));return FALSE; }
 GByteArray *out=g_byte_array_sized_new(h->result->len-remove+n);if(at) g_byte_array_append(out,h->result->data,at);if(n) g_byte_array_append(out,p,n);if(h->result->len>at+remove) g_byte_array_append(out,h->result->data+at+remove,h->result->len-at-remove);g_byte_array_unref(h->result);h->result=out;remember(h);return TRUE;
}
static void take_range(GtkWidget *w,gpointer data) {
 Hex *h=data;guint side=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"side"));gsize n;const guint8 *p=g_bytes_get_data(h->source[side],&n);guint at=value(h->offset),len=value(h->length);
 gsize extent=MAX(g_bytes_get_size(h->source[0]),g_bytes_get_size(h->source[1]));if(at>extent || len>extent-at) { note(h,insight_tr("Source range is outside both files."));return; }len=at<n ? MIN((gsize)len,n-at):0;replace(h,len ? p+at:NULL,len);
}
static void edit_range(GtkWidget *w,gpointer data) {
 (void)w;Hex *h=data;const gchar *s=gtk_entry_get_text(GTK_ENTRY(h->edit));GByteArray *a=g_byte_array_new();gint high=-1;
 for(;*s;s++) { if(g_ascii_isspace(*s)) continue;gint digit=g_ascii_xdigit_value(*s);if(digit<0) { note(h,insight_tr("Enter pairs of hex digits, optionally separated by whitespace."));g_byte_array_unref(a);return; }if(high<0) high=digit;else { guint8 byte=(high<<4)|digit;g_byte_array_append(a,&byte,1);high=-1; } }
 if(high>=0) note(h,insight_tr("An incomplete hex byte cannot be applied."));else replace(h,a->data,a->len);g_byte_array_unref(a);
}
static gboolean confirm(Hex *h) {
 if(!h->dirty) return TRUE;GtkWidget *d=gtk_message_dialog_new(GTK_WINDOW(h->window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,GTK_MESSAGE_QUESTION,GTK_BUTTONS_YES_NO,"%s",insight_tr("Discard unsaved binary result?"));gint r=gtk_dialog_run(GTK_DIALOG(d));gtk_widget_destroy(d);return r==GTK_RESPONSE_YES;
}
static gboolean closing(GtkWidget *w,GdkEvent *e,gpointer data) { (void)w;(void)e;return !confirm(data); }
static void load(GtkWidget *w,gpointer data) {
 (void)w;Hex *h=data;if(!confirm(h)) return;GBytes *b[2]={NULL,NULL};GError *error=NULL;
 for(guint i=0;i<2;i++) {
  const gchar *text=gtk_entry_get_text(GTK_ENTRY(h->path[i]));if(!*text) { b[i]=g_bytes_new(NULL,0);continue; }
  gchar *native=g_filename_from_utf8(text,-1,NULL,NULL,&error);if(!native) goto failed;
  GFile *file=g_file_new_for_path(native);g_free(native);GFileInfo *info=g_file_query_info(file,G_FILE_ATTRIBUTE_STANDARD_TYPE "," G_FILE_ATTRIBUTE_STANDARD_SIZE,G_FILE_QUERY_INFO_NONE,NULL,&error);
  if(!info) { g_object_unref(file);goto failed; }
  gboolean valid=g_file_info_get_file_type(info)==G_FILE_TYPE_REGULAR && g_file_info_get_size(info)<=LIMIT;g_object_unref(info);
  if(!valid) { g_object_unref(file);g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Choose regular files up to 16 MiB.");goto failed; }
  GFileInputStream *stream=g_file_read(file,NULL,&error);g_object_unref(file);if(!stream) goto failed;
  guint8 *bytes=g_malloc(LIMIT+1);gsize n=0;gboolean ok=g_input_stream_read_all(G_INPUT_STREAM(stream),bytes,LIMIT+1,&n,NULL,&error);g_object_unref(stream);
  if(!ok || n>LIMIT) { g_free(bytes);if(!error) g_set_error_literal(&error,G_IO_ERROR,G_IO_ERROR_FAILED,"File grew beyond 16 MiB.");goto failed; }b[i]=g_bytes_new_take(bytes,n);
 }
 for(guint i=0;i<2;i++) { g_bytes_unref(h->source[i]);h->source[i]=b[i]; }
 gsize n;const guint8 *p=g_bytes_get_data(h->source[0],&n);g_byte_array_set_size(h->result,0);if(n) g_byte_array_append(h->result,p,n);g_ptr_array_set_size(h->history,0);g_ptr_array_add(h->history,g_bytes_ref(h->source[0]));h->position=0;h->dirty=FALSE;gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->page),0);render(h);return;
failed:
 note(h,error ? error->message:"Cannot load file");g_clear_error(&error);for(guint i=0;i<2;i++) if(b[i]) g_bytes_unref(b[i]);
}
static void browse(GtkWidget *w,gpointer data) {
 Hex *h=data;guint side=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"side"));GtkWidget *d=gtk_file_chooser_dialog_new(insight_tr("Open binary file"),GTK_WINDOW(h->window),GTK_FILE_CHOOSER_ACTION_OPEN,"Cancel",GTK_RESPONSE_CANCEL,"Open",GTK_RESPONSE_ACCEPT,NULL);
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) { gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));gchar *utf8=g_filename_to_utf8(path,-1,NULL,NULL,NULL);if(utf8) gtk_entry_set_text(GTK_ENTRY(h->path[side]),utf8);g_free(utf8);g_free(path); }gtk_widget_destroy(d);
}
static void save(GtkWidget *w,gpointer data) {
 Hex *h=data;guint mode=GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(w),"mode"));GBytes *snapshot=g_bytes_new(h->result->data,h->result->len);gsize n;const guint8 *p=g_bytes_get_data(snapshot,&n);gchar *encoded=mode ? g_base64_encode(p,n):NULL;
 guint size=value(h->chunk_size);gsize total=mode ? strlen(encoded):n;
 if(mode==2) { gsize start=size ? (gsize)value(h->part)*size:0;if(start>total) note(h,insight_tr("This Base64 part does not exist."));else { gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),encoded+start,MIN(total-start,size ? size:total));note(h,insight_tr("Base64 copied to clipboard.")); }g_free(encoded);g_bytes_unref(snapshot);return; }
 GtkWidget *d=gtk_file_chooser_dialog_new(mode ? insight_tr("Export Base64"):insight_tr("Save binary result"),GTK_WINDOW(h->window),GTK_FILE_CHOOSER_ACTION_SAVE,"Cancel",GTK_RESPONSE_CANCEL,"Save",GTK_RESPONSE_ACCEPT,NULL);gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(d),TRUE);gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(d),mode ? "binary.b64":"merged.bin");
 if(gtk_dialog_run(GTK_DIALOG(d))==GTK_RESPONSE_ACCEPT) {
  gchar *path=gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(d));GError *error=NULL;gboolean ok=TRUE;
  if(mode && size) {
   guint count=MAX((gsize)1,(total+size-1)/size);GPtrArray *created=g_ptr_array_new_with_free_func(g_free);
   /* Exclusive creation protects existing parts; roll back only files created by this export. */
   for(guint i=0;i<count && ok;i++) { gchar *name=g_strdup_printf("%s.part%06u",path,i+1);GFile *f=g_file_new_for_path(name);GFileOutputStream *out=g_file_create(f,G_FILE_CREATE_PRIVATE,NULL,&error);g_object_unref(f);if(!out) { g_free(name);ok=FALSE;break; }g_ptr_array_add(created,name);gsize at=(gsize)i*size;ok=g_output_stream_write_all(G_OUTPUT_STREAM(out),encoded+at,MIN((gsize)size,total-at),NULL,NULL,&error);if(ok) ok=g_output_stream_close(G_OUTPUT_STREAM(out),NULL,&error);g_object_unref(out); }
   if(!ok) for(guint i=0;i<created->len;i++) { GFile *f=g_file_new_for_path(g_ptr_array_index(created,i));g_file_delete(f,NULL,NULL);g_object_unref(f); }g_ptr_array_unref(created);
  } else { GFile *f=g_file_new_for_path(path);ok=g_file_replace_contents(f,mode ? encoded:(p ? (const gchar*)p:""),total,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error);g_object_unref(f); }
  if(ok) { if(!mode) h->dirty=FALSE;render(h);note(h,insight_tr("Export completed.")); }else note(h,error ? error->message:"Export failed");g_clear_error(&error);g_free(path);
 }gtk_widget_destroy(d);g_free(encoded);g_bytes_unref(snapshot);
}
static void chunk_changed(GtkComboBox *combo,gpointer data) { Hex *h=data;const guint sizes[]={0,10*1024,100*1024,1024*1024,10*1024*1024};gint i=gtk_combo_box_get_active(combo);if(i>=0) gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->chunk_size),sizes[i]); }
static void next_difference(GtkWidget *w,gpointer data) {
 (void)w;Hex *h=data;gsize a,b;const guint8 *left=g_bytes_get_data(h->source[0],&a),*right=g_bytes_get_data(h->source[1],&b);gsize start=(gsize)value(h->offset)+value(h->length),max=MAX(a,b),at=start;
 while(at<max && at<a && at<b && left[at]==right[at]) at++;
 if(at==max) { note(h,insight_tr("No later difference. Set source offset and length to zero to restart."));return; }
 gsize end=at;while(end<max && (end>=a || end>=b || left[end]!=right[end])) end++;
 gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->offset),at);gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->length),end-at);gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->target),MIN(at,h->result->len));gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->remove),MIN(end,h->result->len)-MIN(at,h->result->len));gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->page),at/PAGE);
 note(h,insight_tr("Difference selected by byte offset. Review source and Result ranges before merging; shifted data is not automatically aligned."));
}
static gboolean grid_range(GtkWidget *w,GdkEventButton *event,gpointer data) {
 Hex *h=data;if(event->button!=1 || event->window!=gtk_text_view_get_window(GTK_TEXT_VIEW(w),GTK_TEXT_WINDOW_TEXT)) return FALSE;
 guint side=w==h->view[0] ? 0:w==h->view[1] ? 1:2;GtkTextBuffer *b=gtk_text_view_get_buffer(GTK_TEXT_VIEW(w));GtkTextIter a,z;gsize size=side<2 ? g_bytes_get_size(h->source[side]):h->result->len,start,end;
 if(gtk_text_buffer_get_selection_bounds(b,&a,&z)) { start=(gsize)gtk_text_iter_get_line(&a)*16;end=(gsize)(gtk_text_iter_get_line(&z)+(gtk_text_iter_get_line_offset(&z)>0))*16; }
 else {
  gint x,y;gtk_text_view_window_to_buffer_coords(GTK_TEXT_VIEW(w),GTK_TEXT_WINDOW_TEXT,event->x,event->y,&x,&y);gtk_text_view_get_iter_at_location(GTK_TEXT_VIEW(w),&a,x,y);gint col=gtk_text_iter_get_line_offset(&a);guint word=1u<<gtk_combo_box_get_active(GTK_COMBO_BOX(h->word)),ascii=10+(16/word)*(2*word+1)+3;
  start=(gsize)gtk_text_iter_get_line(&a)*16;if(col>=(gint)ascii) { start+=MIN((guint)(col-ascii),15u);end=start+1; }else if(col>=10) { start+=MIN((guint)(col-10)/(2*word+1)*word,16-word);end=start+word; }else { end=start+16; }
 }
 start+=(gsize)value(h->page)*PAGE;end+=(gsize)value(h->page)*PAGE;start=MIN(start,size);end=MIN(end,size);
 if(side<2) { gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->offset),start);gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->length),end-start); }
 else { gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->target),start);gtk_spin_button_set_value(GTK_SPIN_BUTTON(h->remove),end-start); }
 return FALSE;
}
static void sync_scroll(GtkAdjustment *a,gpointer data) { Hex *h=data;if(!h->scroll[0] || !h->scroll[1] || h->scrolling) return;h->scrolling=TRUE;gtk_adjustment_set_value(h->scroll[a==h->scroll[0] ? 1:0],gtk_adjustment_get_value(a));h->scrolling=FALSE; }
static GtkWidget *button(GtkWidget *box,const gchar *label,GCallback cb,Hex *h) { GtkWidget *w=gtk_button_new_with_label(insight_tr(label));gtk_widget_set_tooltip_text(w,insight_tr(label));gtk_box_pack_start(GTK_BOX(box),w,FALSE,FALSE,0);g_signal_connect(w,"clicked",cb,h);return w; }
static GtkWidget *row(GtkWidget *box) { GtkWidget *r=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);gtk_box_pack_start(GTK_BOX(box),r,FALSE,FALSE,0);return r; }
static void free_hex(gpointer data) { Hex *h=data;for(guint i=0;i<2;i++) g_bytes_unref(h->source[i]);g_byte_array_unref(h->result);g_ptr_array_unref(h->history);g_free(h); }
void insight_hex_show(GtkWidget *parent,const gchar *left,const gchar *right) {
 Hex *h=g_new0(Hex,1);h->window=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(h->window,"insight-hex-window");gtk_window_set_title(GTK_WINDOW(h->window),insight_tr("Binary Diff / Merge — Hex editor"));gtk_window_set_default_size(GTK_WINDOW(h->window),INSIGHT_COMPARE_WIDTH,INSIGHT_COMPARE_HEIGHT);if(parent) gtk_window_set_transient_for(GTK_WINDOW(h->window),GTK_WINDOW(gtk_widget_get_toplevel(parent)));
 h->history=g_ptr_array_new_with_free_func((GDestroyNotify)g_bytes_unref);h->result=g_byte_array_new();for(guint i=0;i<2;i++) h->source[i]=g_bytes_new(NULL,0);g_ptr_array_add(h->history,g_bytes_new(NULL,0));
 g_object_set_data_full(G_OBJECT(h->window),"insight-hex-state",h,free_hex);g_signal_connect(h->window,"delete-event",G_CALLBACK(closing),h);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,3);gtk_container_add(GTK_CONTAINER(h->window),box);GtkWidget *tabs=insight_compare_ui_tabs(box,"insight-hex-toolbar-tabs"),*files=insight_compare_ui_page(tabs,insight_tr("Files and comparison")),*merge=insight_compare_ui_page(tabs,insight_tr("Merge and history")),*view=insight_compare_ui_page(tabs,insight_tr("View")),*export=insight_compare_ui_page(tabs,insight_tr("Base64 export"));
 for(guint i=0;i<2;i++) { GtkWidget *r=row(files);gtk_box_pack_start(GTK_BOX(r),gtk_label_new(insight_tr(i ? "Right":"Left")),FALSE,FALSE,0);h->path[i]=gtk_entry_new();gtk_widget_set_tooltip_text(h->path[i],insight_tr("File path; blank means an empty byte stream. Load resets Result to Left."));gtk_box_pack_start(GTK_BOX(r),h->path[i],TRUE,TRUE,0);g_object_set_data(G_OBJECT(button(r,"Open…",G_CALLBACK(browse),h)),"side",GUINT_TO_POINTER(i)); }
 button(row(files),"Load / Compare",G_CALLBACK(load),h);
 GtkWidget *r=row(view);h->word=gtk_combo_box_text_new();const gchar *words[]={"Bytes (8)","Words (16)","Words (32)","Words (64)"};for(guint i=0;i<4;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(h->word),insight_tr(words[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(h->word),0);gtk_box_pack_start(GTK_BOX(r),h->word,FALSE,FALSE,0);h->endian=gtk_combo_box_text_new();gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(h->endian),"Little endian");gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(h->endian),"Big endian");gtk_combo_box_set_active(GTK_COMBO_BOX(h->endian),0);gtk_box_pack_start(GTK_BOX(r),h->endian,FALSE,FALSE,0);h->page=spin(r,"Page number (zero-based), 4096 bytes per page",LIMIT/PAGE);g_signal_connect(h->word,"changed",G_CALLBACK(view_changed),h);g_signal_connect(h->endian,"changed",G_CALLBACK(view_changed),h);g_signal_connect(h->page,"value-changed",G_CALLBACK(view_changed),h);
 r=row(merge);gtk_box_pack_start(GTK_BOX(r),gtk_label_new(insight_tr("Source offset / count")),FALSE,FALSE,0);h->offset=spin(r,"Source byte offset (decimal)",LIMIT);h->length=spin(r,"Source byte count (decimal)",LIMIT);gtk_box_pack_start(GTK_BOX(r),gtk_label_new(insight_tr("Result offset / replace count")),FALSE,FALSE,0);h->target=spin(r,"Result byte offset (decimal)",LIMIT);h->remove=spin(r,"Result bytes to replace; zero inserts (decimal)",LIMIT);r=row(merge);button(r,"Next difference",G_CALLBACK(next_difference),h);
 r=row(merge);for(guint i=0;i<2;i++) g_object_set_data(G_OBJECT(button(r,i ? "Take Right range":"Take Left range",G_CALLBACK(take_range),h)),"side",GUINT_TO_POINTER(i));button(r,"Undo",G_CALLBACK(history),h);g_object_set_data(G_OBJECT(button(r,"Redo",G_CALLBACK(history),h)),"redo",GINT_TO_POINTER(1));button(r,"Save binary result",G_CALLBACK(save),h);
 r=row(merge);h->edit=gtk_entry_new();gtk_entry_set_max_length(GTK_ENTRY(h->edit),65536);gtk_widget_set_tooltip_text(h->edit,insight_tr("Hex bytes to insert at Result offset, replacing Result byte count. Empty input deletes that range."));gtk_box_pack_start(GTK_BOX(r),h->edit,TRUE,TRUE,0);button(r,"Apply hex bytes",G_CALLBACK(edit_range),h);
 r=row(export);h->chunk=gtk_combo_box_text_new();const gchar *sizes[]={"Whole stream","10 KiB","100 KiB","1 MiB","10 MiB"};for(guint i=0;i<5;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(h->chunk),insight_tr(sizes[i]));gtk_combo_box_set_active(GTK_COMBO_BOX(h->chunk),0);gtk_widget_set_tooltip_text(h->chunk,insight_tr("Size of each encoded Base64 part. Concatenate parts before decoding."));gtk_box_pack_start(GTK_BOX(r),h->chunk,FALSE,FALSE,0);h->chunk_size=spin(r,"Encoded bytes per part; zero exports one complete Base64 stream",64*1024*1024);g_signal_connect(h->chunk,"changed",G_CALLBACK(chunk_changed),h);h->part=spin(r,"Clipboard part number (zero-based); ignored for Whole stream",LIMIT);g_object_set_data(G_OBJECT(button(r,"Export Base64",G_CALLBACK(save),h)),"mode",GUINT_TO_POINTER(1));g_object_set_data(G_OBJECT(button(r,"Copy Base64 part",G_CALLBACK(save),h)),"mode",GUINT_TO_POINTER(2));
 GtkWidget *split=gtk_paned_new(GTK_ORIENTATION_VERTICAL),*columns=gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);gtk_box_pack_start(GTK_BOX(box),split,TRUE,TRUE,0);gtk_paned_pack1(GTK_PANED(split),columns,TRUE,FALSE);gtk_paned_set_position(GTK_PANED(split),270);gtk_paned_set_position(GTK_PANED(columns),540);
 for(guint i=0;i<3;i++) { GtkWidget *col=gtk_box_new(GTK_ORIENTATION_VERTICAL,0),*scroll=gtk_scrolled_window_new(NULL,NULL);gtk_box_pack_start(GTK_BOX(col),gtk_label_new(insight_tr(i==0 ? "Left":i==1 ? "Right":"Result")),FALSE,FALSE,0);h->view[i]=gtk_text_view_new();gtk_text_view_set_monospace(GTK_TEXT_VIEW(h->view[i]),TRUE);gtk_widget_set_tooltip_text(h->view[i],insight_tr("Click a hex word or ASCII byte to set its range; drag to select whole rows. Fine-tune byte offsets/counts in Merge and history."));g_signal_connect_after(h->view[i],"button-release-event",G_CALLBACK(grid_range),h);gtk_text_view_set_editable(GTK_TEXT_VIEW(h->view[i]),FALSE);gtk_style_context_add_class(gtk_widget_get_style_context(h->view[i]),"insight-compare-code");gtk_container_add(GTK_CONTAINER(scroll),h->view[i]);gtk_box_pack_start(GTK_BOX(col),scroll,TRUE,TRUE,0);if(i<2) { h->scroll[i]=gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scroll));g_signal_connect(h->scroll[i],"value-changed",G_CALLBACK(sync_scroll),h);if(!i) gtk_paned_pack1(GTK_PANED(columns),col,TRUE,FALSE);else gtk_paned_pack2(GTK_PANED(columns),col,TRUE,FALSE); }else gtk_paned_pack2(GTK_PANED(split),col,TRUE,FALSE); }
 h->status=gtk_label_new(NULL);gtk_label_set_ellipsize(GTK_LABEL(h->status),PANGO_ELLIPSIZE_END);gtk_label_set_xalign(GTK_LABEL(h->status),0);gtk_box_pack_start(GTK_BOX(box),h->status,FALSE,FALSE,0);gtk_widget_show_all(h->window);insight_i18n_apply(h->window);if(left) { gchar *s=g_filename_to_utf8(left,-1,NULL,NULL,NULL);if(s) gtk_entry_set_text(GTK_ENTRY(h->path[0]),s);g_free(s); }if(right) { gchar *s=g_filename_to_utf8(right,-1,NULL,NULL,NULL);if(s) gtk_entry_set_text(GTK_ENTRY(h->path[1]),s);g_free(s); }load(NULL,h);
}
