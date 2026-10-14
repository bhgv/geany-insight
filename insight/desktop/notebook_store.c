/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "notebook_store.h"
#include <gio/gio.h>
#include <string.h>
static void note_free(gpointer data) { InsightNote *n=data; g_free(n->name); g_free(n->path); g_free(n->text); g_strfreev(n->languages); g_free(n); }
InsightNotes *insight_notes_new(void) { InsightNotes *n=g_new0(InsightNotes,1); n->items=g_ptr_array_new_with_free_func(note_free); return n; }
void insight_notes_free(InsightNotes *n) { if(n) { g_ptr_array_free(n->items,TRUE); g_free(n); } }
const InsightNote *insight_notes_get(const InsightNotes *n,InsightNoteKind kind,const char *name) { if(!n || !name) return NULL; for(guint i=0;i<n->items->len;i++) { InsightNote *note=g_ptr_array_index(n->items,i); if(note->kind==kind && !strcmp(note->name,name)) return note; } return NULL; }
gboolean insight_notes_remove(InsightNotes *n,InsightNoteKind kind,const char *name) { if(!n || !name) return FALSE; for(guint i=0;i<n->items->len;i++) { InsightNote *note=g_ptr_array_index(n->items,i); if(note->kind==kind && !strcmp(note->name,name)) return g_ptr_array_remove_index(n->items,i)!=NULL; } return FALSE; }
gboolean insight_notes_set(InsightNotes *n,InsightNoteKind kind,const char *name,const char *path,guint64 line,const char *text,GError **error) {
 if(!n || !name || !*name || strlen(name)>256 || !g_utf8_validate(name,-1,NULL) || !text || strlen(text)>65536 || !g_utf8_validate(text,-1,NULL) || (kind!=INSIGHT_BOOKMARK && kind!=INSIGHT_CLIP) || (kind==INSIGHT_BOOKMARK && (!path || !g_path_is_absolute(path) || !g_utf8_validate(path,-1,NULL) || strlen(path)>16384 || !line || line>2147483647))) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid note name, source location or text"); return FALSE; }
 if(!insight_notes_get(n,kind,name) && n->items->len>=64) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NO_SPACE,"Note limit reached (64)"); return FALSE; }
 const InsightNote *old=insight_notes_get(n,kind,name);
 InsightNote *note=g_new0(InsightNote,1); note->languages=old ? g_strdupv(old->languages) : NULL; note->kind=kind; note->name=g_strdup(name); note->path=g_strdup(kind==INSIGHT_BOOKMARK ? path : ""); note->line=kind==INSIGHT_BOOKMARK ? line : 0; note->text=g_strdup(text);
 insight_notes_remove(n,kind,name); g_ptr_array_add(n->items,note); return TRUE;
}
gboolean insight_notes_set_languages(InsightNotes *notes,const char *name,const gchar *const *languages,GError **error) {
 InsightNote *note=(InsightNote *)insight_notes_get(notes,INSIGHT_CLIP,name); if(!note) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Unknown clip"); return FALSE; }
 guint count=0; for(;languages && languages[count];count++) {
  if(count>=32 || !*languages[count] || strlen(languages[count])>256 || !g_utf8_validate(languages[count],-1,NULL)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid template languages (up to 32 profile names)"); return FALSE; }
  for(guint k=0;k<count;k++) if(!strcmp(languages[k],languages[count])) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Duplicate template language"); return FALSE; }
 }
 gchar **copy=languages ? g_strdupv((gchar **)languages) : NULL; g_strfreev(note->languages); note->languages=copy; return TRUE;
}
gboolean insight_note_matches_language(const InsightNote *note,const gchar *profile) {
 if(!note) return FALSE;
 if(!note->languages || !note->languages[0]) return TRUE;
 if(!profile) return FALSE;
 for(guint i=0;note->languages[i];i++) if(!strcmp(note->languages[i],profile)) return TRUE;
 return FALSE;
}
gboolean insight_notes_save(const InsightNotes *n,const char *path,GError **error) {
 if(!n || !path) return FALSE;
 GKeyFile *key=g_key_file_new(); g_key_file_set_integer(key,"notes","version",1); g_key_file_set_integer(key,"notes","count",(gint)n->items->len);
 for(guint i=0;i<n->items->len;i++) { InsightNote *note=g_ptr_array_index(n->items,i); gchar group[32]; g_snprintf(group,sizeof group,"note-%u",i); g_key_file_set_integer(key,group,"kind",note->kind); g_key_file_set_string(key,group,"name",note->name); g_key_file_set_string(key,group,"path",note->path); g_key_file_set_uint64(key,group,"line",note->line); g_key_file_set_string(key,group,"text",note->text); if(note->kind==INSIGHT_CLIP && note->languages && note->languages[0]) g_key_file_set_string_list(key,group,"languages",(const gchar *const *)note->languages,g_strv_length(note->languages)); }
 gsize length; gchar *data=g_key_file_to_data(key,&length,NULL); g_key_file_unref(key); GFile *file=g_file_new_for_path(path);
 gboolean ok=length<=8u*1024u*1024u && g_file_replace_contents(file,data,length,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,error); g_object_unref(file); g_free(data); return ok;
}
InsightNotes *insight_notes_load(const char *path,GError **error) {
 GFile *file=g_file_new_for_path(path); GFileInputStream *stream=g_file_read(file,NULL,error); g_object_unref(file); if(!stream) return NULL;
 GString *data=g_string_new(NULL); char buffer[16384]; gboolean ok=TRUE;
 for(;;) { gssize got=g_input_stream_read(G_INPUT_STREAM(stream),buffer,sizeof buffer,NULL,error); if(got<0) { ok=FALSE; break; } if(!got) break; if(data->len+(gsize)got>8u*1024u*1024u) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Notes file exceeds size limit"); ok=FALSE; break; } g_string_append_len(data,buffer,got); }
 g_object_unref(stream); GKeyFile *key=g_key_file_new(); InsightNotes *notes=insight_notes_new();
 if(ok) ok=!memchr(data->str,0,data->len) && g_utf8_validate(data->str,(gssize)data->len,NULL) && g_key_file_load_from_data(key,data->str,data->len,G_KEY_FILE_NONE,error);
 if(ok) { GError *e=NULL; gint version=g_key_file_get_integer(key,"notes","version",&e),count=-1; if(!e) count=g_key_file_get_integer(key,"notes","count",&e); ok=!e && version==1 && count>=0 && count<=64; g_clear_error(&e);
  for(gint i=0;ok && i<count;i++) { gchar group[32]; g_snprintf(group,sizeof group,"note-%d",i); gchar *name=g_key_file_get_string(key,group,"name",NULL),*p=g_key_file_get_string(key,group,"path",NULL),*text=g_key_file_get_string(key,group,"text",NULL); gint kind=g_key_file_get_integer(key,group,"kind",&e); guint64 line=0; if(!e) line=g_key_file_get_uint64(key,group,"line",&e); ok=!e && (kind!=INSIGHT_CLIP || (line==0 && p && !*p)) && name && p && text && !insight_notes_get(notes,(InsightNoteKind)kind,name) && insight_notes_set(notes,(InsightNoteKind)kind,name,p,line,text,error); if(ok && g_key_file_has_key(key,group,"languages",NULL)) { gsize size=0; gchar **langs=g_key_file_get_string_list(key,group,"languages",&size,&e); ok=!e && ((kind==INSIGHT_CLIP && insight_notes_set_languages(notes,name,(const gchar *const *)langs,error)) || (kind==INSIGHT_BOOKMARK && !size)); g_strfreev(langs); }
   g_free(name); g_free(p); g_free(text); g_clear_error(&e); }
 }
 g_key_file_unref(key); g_string_free(data,TRUE);
 if(!ok) { if(!error || !*error) g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"Invalid notes snapshot"); insight_notes_free(notes); return NULL; } return notes;
}
gboolean insight_bookmark_resolve(const InsightNote *note,const char *text,gsize size,guint64 *line,gboolean *ambiguous) {
 if(!note || note->kind!=INSIGHT_BOOKMARK || !text || size>2u*1024u*1024u || !line || !ambiguous || !g_utf8_validate(text,(gssize)size,NULL) || memchr(text,0,size)) return FALSE;
 guint64 current=1,found=0,count=0; *ambiguous=FALSE; gsize start=0,anchor=strlen(note->text);
 for(gsize i=0;i<=size;i++) if(i==size || text[i]=='\n') { gsize end=i; if(end>start && text[end-1]=='\r') end--; if(end-start==anchor && !memcmp(text+start,note->text,anchor)) { if(current==note->line) { *line=current; return TRUE; } found=current; count++; } start=i+1; current++; }
 if(count!=1) { *ambiguous=count>1; return FALSE; } *line=found; return TRUE;
}
static gboolean notes_copy_kind(InsightNotes *out,const InsightNotes *source,InsightNoteKind kind,GError **error) {
 for(guint i=0;i<source->items->len;i++) {
  const InsightNote *note=g_ptr_array_index(source->items,i);
  if(kind && note->kind!=kind) continue;
  if(insight_notes_get(out,note->kind,note->name)) {
   g_set_error(error,G_IO_ERROR,G_IO_ERROR_EXISTS,"A note named '%s' already exists; rename it before importing",note->name); return FALSE;
  }
  if(!insight_notes_set(out,note->kind,note->name,note->path,note->line,note->text,error)) return FALSE;
  if(note->kind==INSIGHT_CLIP && !insight_notes_set_languages(out,note->name,(const gchar *const *)note->languages,error)) return FALSE;
 }
 return TRUE;
}
InsightNotes *insight_notes_select(const InsightNotes *source,InsightNoteKind kind,GError **error) {
 if(!source || (kind!=INSIGHT_BOOKMARK && kind!=INSIGHT_CLIP)) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid note selection"); return NULL;
 }
 InsightNotes *out=insight_notes_new();
 if(!notes_copy_kind(out,source,kind,error)) { insight_notes_free(out); return NULL; }
 return out;
}
InsightNotes *insight_notes_merge(const InsightNotes *current,const InsightNotes *incoming,InsightNoteKind kind,GError **error) {
 if(!current || !incoming || (kind!=INSIGHT_BOOKMARK && kind!=INSIGHT_CLIP)) {
  g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid note import"); return NULL;
 }
 InsightNotes *out=insight_notes_new();
 if(!notes_copy_kind(out,current,0,error) || !notes_copy_kind(out,incoming,kind,error)) { insight_notes_free(out); return NULL; }
 if(out->items->len==current->items->len) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA,"The file contains no notes for this panel"); insight_notes_free(out); return NULL; }
 return out;
}
InsightNotes *insight_notes_rename(const InsightNotes *current,InsightNoteKind kind,const char *old_name,const char *new_name,GError **error) {
 const InsightNote *note=insight_notes_get(current,kind,old_name);
 if(!note || !new_name) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid note rename"); return NULL; }
 if(strcmp(old_name,new_name) && insight_notes_get(current,kind,new_name)) { g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_EXISTS,"A note with this name already exists"); return NULL; }
 InsightNotes *out=insight_notes_new();
 if(!notes_copy_kind(out,current,0,error)) { insight_notes_free(out); return NULL; }
 insight_notes_remove(out,kind,old_name);
 if(!insight_notes_set(out,kind,new_name,note->path,note->line,note->text,error)) { insight_notes_free(out); return NULL; }
 if(kind==INSIGHT_CLIP && !insight_notes_set_languages(out,new_name,(const gchar *const *)note->languages,error)) { insight_notes_free(out); return NULL; }
 return out;
}
