/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "filetype_pairs.h"
#include "Scintilla.h"
#include <gdk/gdkkeysyms.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
#define DATA "insight-filetype-pairs"
typedef struct { gint open,close;gchar left,right; } Pair;
static GArray *pairs(ScintillaObject *sci) { return g_object_get_data(G_OBJECT(sci),DATA); }
void insight_filetype_pairs_clear(ScintillaObject *sci) { GArray *list=pairs(sci);if(list) g_array_set_size(list,0); }
static void modified(ScintillaObject *sci,gint unused,SCNotification *nt,gpointer data)
{
 (void)unused;(void)data;if(nt->nmhdr.code!=SCN_MODIFIED) return;
 GArray *list=pairs(sci);gint kind=nt->modificationType;
 if(kind & (SC_PERFORMED_UNDO|SC_PERFORMED_REDO)) { insight_filetype_pairs_clear(sci);return; }
 if(!(kind & (SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) return;
 /* Scintilla positions are signed, but the native editor uses gint offsets. */
 if(nt->position<0 || nt->length<0 || nt->position>G_MAXINT || nt->length>G_MAXINT-nt->position) { insight_filetype_pairs_clear(sci);return; }
 gint at=nt->position,length=nt->length;
 for(guint i=list->len;i>0;i--) {
  Pair *p=&g_array_index(list,Pair,i-1);
  if(kind & SC_MOD_DELETETEXT) {
   if((p->open>=at && p->open<at+length) || (p->close>=at && p->close<at+length)) { g_array_remove_index(list,i-1);continue; }
   if(p->open>=at+length) p->open-=length;
   if(p->close>=at+length) p->close-=length;
  } else {
   if(p->close>G_MAXINT-length) { g_array_remove_index(list,i-1);continue; }
   if(p->open>=at) p->open+=length;
   if(p->close>=at) p->close+=length;
  }
 }
}
void insight_filetype_pairs_attach(ScintillaObject *sci)
{
 if(pairs(sci)) return;
 g_object_set_data_full(G_OBJECT(sci),DATA,g_array_new(FALSE,FALSE,sizeof(Pair)),(GDestroyNotify)g_array_unref);
 g_signal_connect(sci,"sci-notify",G_CALLBACK(modified),NULL);
}
void insight_filetype_pairs_remember(ScintillaObject *sci,gint open,gint close)
{
 insight_filetype_pairs_attach(sci);if(open<0 || close<=open || close>=SEND(sci,SCI_GETLENGTH,0,0)) return;
 gchar left=SEND(sci,SCI_GETCHARAT,open,0),right=SEND(sci,SCI_GETCHARAT,close,0);
 if(!((left=='(' && right==')') || (left=='[' && right==']') || (left=='{' && right=='}') || (left=='\'' && right=='\'') || (left=='"' && right=='"'))) return;
 GArray *list=pairs(sci);if(list->len==128) g_array_remove_index(list,0);Pair p={open,close,left,right};g_array_append_val(list,p);
}
gboolean insight_filetype_pairs_key(ScintillaObject *sci,guint key,GdkModifierType state,gboolean surround)
{
 GArray *list=pairs(sci);if(!list || SEND(sci,SCI_GETREADONLY,0,0) || SEND(sci,SCI_GETSELECTIONS,0,0)!=1 || SEND(sci,SCI_GETSELECTIONSTART,0,0)!=SEND(sci,SCI_GETSELECTIONEND,0,0) || SEND(sci,SCI_AUTOCACTIVE,0,0)) return FALSE;
 if(state & (gtk_accelerator_get_default_mod_mask() & ~GDK_SHIFT_MASK)) return FALSE;
 gboolean back=key==GDK_KEY_BackSpace;if(back && (state&GDK_SHIFT_MASK)) return FALSE;
 gunichar character=gdk_keyval_to_unicode(key);gint pos=SEND(sci,SCI_GETCURRENTPOS,0,0);
 if(surround && !back && (character=='(' || character=='[' || character=='{') && SEND(sci,SCI_GETCHARAT,pos,0)==character) {
  sptr_t match=SEND(sci,SCI_BRACEMATCH,pos,0);
  if(match>pos && match<G_MAXINT-2) {
   gchar left[]={(gchar)character,0},right[]={character=='(' ? ')' : (character=='[' ? ']' : '}'),0};
   SEND(sci,SCI_BEGINUNDOACTION,0,0);SEND(sci,SCI_INSERTTEXT,match+1,(sptr_t)right);SEND(sci,SCI_INSERTTEXT,pos,(sptr_t)left);insight_filetype_pairs_remember(sci,pos,match+2);SEND(sci,SCI_SETSEL,pos+1,pos+1);SEND(sci,SCI_ENDUNDOACTION,0,0);return TRUE;
  }
 }

 for(guint i=list->len;i>0;i--) {
  Pair p=g_array_index(list,Pair,i-1);if(p.close!=pos) continue;
  if(SEND(sci,SCI_GETCHARAT,p.open,0)!=p.left || SEND(sci,SCI_GETCHARAT,p.close,0)!=p.right) { g_array_remove_index(list,i-1);continue; }
  if(back && p.close==p.open+1) {
   g_array_remove_index(list,i-1);SEND(sci,SCI_BEGINUNDOACTION,0,0);SEND(sci,SCI_DELETERANGE,p.open,2);SEND(sci,SCI_SETSEL,p.open,p.open);SEND(sci,SCI_ENDUNDOACTION,0,0);return TRUE;
  }
  if(!back && character==(gunichar)p.right) { g_array_remove_index(list,i-1);SEND(sci,SCI_SETSEL,pos+1,pos+1);SEND(sci,SCI_SCROLLCARET,0,0);return TRUE; }
 }
 return FALSE;
}
