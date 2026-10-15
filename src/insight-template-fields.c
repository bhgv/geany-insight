/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "insight-template-fields.h"
#include "Scintilla.h"
#include <string.h>
#define SSM(s,m,w,l) scintilla_send_message(s,m,w,l)
#define FIELDS_KEY "insight-editable-template-fields"
#define FIELD_INDICATOR 27
typedef struct { GArray *fields;guint current;gsize finish;gboolean active,painting; } FieldSession;
static void session_free(gpointer data) { FieldSession *session=data;g_array_unref(session->fields);g_free(session); }
static void paint(ScintillaObject *sci,FieldSession *session) {
 session->painting=TRUE;SSM(sci,SCI_SETINDICATORCURRENT,FIELD_INDICATOR,0);SSM(sci,SCI_INDICATORCLEARRANGE,0,SSM(sci,SCI_GETLENGTH,0,0));
 if(session->active) for(guint i=0;i<session->fields->len;i++) { InsightSnippetField field=g_array_index(session->fields,InsightSnippetField,i);if(field.end>field.start) SSM(sci,SCI_INDICATORFILLRANGE,field.start,field.end-field.start); }
 session->painting=FALSE;
}
static void stop(ScintillaObject *sci,FieldSession *session) { session->active=FALSE;paint(sci,session);g_array_set_size(session->fields,0); }
static void select_field(ScintillaObject *sci,FieldSession *session) { InsightSnippetField field=g_array_index(session->fields,InsightSnippetField,session->current);SSM(sci,SCI_SETSEL,field.start,field.end);SSM(sci,SCI_SCROLLCARET,0,0); }
static void modified(ScintillaObject *sci,gint code,gpointer notification,gpointer data) {
 (void)code;FieldSession *session=data;SCNotification *event=notification;if(!session->active || session->painting || event->nmhdr.code!=SCN_MODIFIED || !(event->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) return;
 if(event->modificationType&(SC_PERFORMED_UNDO|SC_PERFORMED_REDO) || event->position<0 || event->length<0 || SSM(sci,SCI_GETSELECTIONS,0,0)!=1) { stop(sci,session);return; }
 gsize at=(gsize)event->position,removed=event->modificationType&SC_MOD_DELETETEXT ? (gsize)event->length : 0,inserted=event->modificationType&SC_MOD_INSERTTEXT ? (gsize)event->length : 0;
 InsightSnippetField *active=&g_array_index(session->fields,InsightSnippetField,session->current);
 if(at<active->start || at>active->end || removed>active->end-at || inserted>262144 || (gsize)SSM(sci,SCI_GETLENGTH,0,0)>2*1024*1024) { stop(sci,session);return; }
 gint64 delta=(gint64)inserted-(gint64)removed;active->end=(gsize)((gint64)active->end+delta);
 for(guint i=session->current+1;i<session->fields->len;i++) { InsightSnippetField *field=&g_array_index(session->fields,InsightSnippetField,i);field->start=(gsize)((gint64)field->start+delta);field->end=(gsize)((gint64)field->end+delta); }
 if(session->finish>=at+removed) session->finish=(gsize)((gint64)session->finish+delta);else if(session->finish>=at) session->finish=at+inserted;
 paint(sci,session);
}
void insight_template_fields_begin(ScintillaObject *sci,const GArray *fields,gsize offset,gsize finish) {
 FieldSession *session=g_object_get_data(G_OBJECT(sci),FIELDS_KEY);
 if(!session) { session=g_new0(FieldSession,1);session->fields=g_array_new(FALSE,FALSE,sizeof(InsightSnippetField));g_object_set_data_full(G_OBJECT(sci),FIELDS_KEY,session,session_free);g_signal_connect(sci,"sci-notify",G_CALLBACK(modified),session); }
 stop(sci,session);gsize length=(gsize)SSM(sci,SCI_GETLENGTH,0,0);if(!fields || !fields->len || fields->len>1024 || offset>length || finish>length-offset) return;
 gsize previous=0;for(guint i=0;i<fields->len;i++) { InsightSnippetField field=g_array_index(fields,InsightSnippetField,i);if(field.start<previous || field.end<field.start || field.end>length-offset) { g_array_set_size(session->fields,0);return; }previous=field.end;field.start+=offset;field.end+=offset;g_array_append_val(session->fields,field); }
 session->current=0;session->finish=offset+finish;session->active=TRUE;
 SSM(sci,SCI_INDICSETSTYLE,FIELD_INDICATOR,INDIC_ROUNDBOX);SSM(sci,SCI_INDICSETFORE,FIELD_INDICATOR,0xc07a20);SSM(sci,SCI_INDICSETALPHA,FIELD_INDICATOR,35);SSM(sci,SCI_INDICSETOUTLINEALPHA,FIELD_INDICATOR,160);SSM(sci,SCI_INDICSETUNDER,FIELD_INDICATOR,TRUE);paint(sci,session);select_field(sci,session);
}
gboolean insight_template_fields_key(GtkWidget *widget,GdkEventKey *event) {
 if(!widget || !IS_SCINTILLA(widget)) return FALSE;
 ScintillaObject *sci=SCINTILLA(widget);FieldSession *session=g_object_get_data(G_OBJECT(sci),FIELDS_KEY);if(!session || !session->active) return FALSE;
 guint modifiers=event->state&(GDK_CONTROL_MASK|GDK_MOD1_MASK|GDK_SUPER_MASK|GDK_META_MASK);if(modifiers) return FALSE;
 if(event->keyval==GDK_KEY_Escape) { stop(sci,session);return TRUE; }
 if(event->keyval!=GDK_KEY_Tab && event->keyval!=GDK_KEY_ISO_Left_Tab) return FALSE;
 InsightSnippetField field=g_array_index(session->fields,InsightSnippetField,session->current);gsize a=(gsize)SSM(sci,SCI_GETSELECTIONSTART,0,0),b=(gsize)SSM(sci,SCI_GETSELECTIONEND,0,0);
 if(a<field.start || b>field.end || SSM(sci,SCI_GETREADONLY,0,0) || SSM(sci,SCI_GETSELECTIONS,0,0)!=1) { stop(sci,session);return FALSE; }
 gboolean backwards=event->keyval==GDK_KEY_ISO_Left_Tab || (event->state&GDK_SHIFT_MASK);
 if(backwards) { if(session->current) session->current--;select_field(sci,session); }
 else if(session->current+1<session->fields->len) { session->current++;select_field(sci,session); }
 else { gsize finish=session->finish;stop(sci,session);SSM(sci,SCI_SETSEL,finish,finish);SSM(sci,SCI_SCROLLCARET,0,0); }
 return TRUE;
}
