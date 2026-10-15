/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "layout_state.h"
static const char *keys[IL_FIELDS]={"left","project","context","follow","wrap","floating","left-split","right-split","context-split","tab","x","y","width","height","detached","host-visible","window-list-set","window-list-visible","window-list-docked","window-list-positioned","window-list-maximized","window-list-x","window-list-y","window-list-width","window-list-height"};
static gboolean boolean_field(guint i) { return i<=IL_FLOAT || (i>=IL_WL_SET && i<=IL_WL_MAXIMIZED); }
gboolean insight_layout_valid(const InsightLayout *r) {
 if(!r || !r->valid) return FALSE;
 for(guint i=0;i<=IL_FLOAT;i++) if(r->values[i]!=0 && r->values[i]!=1) return FALSE;
 for(guint i=IL_LEFT_SPLIT;i<=IL_CONTEXT_SPLIT;i++) if(r->values[i]<0 || r->values[i]>20000) return FALSE;
 for(guint i=IL_WL_SET;i<=IL_WL_MAXIMIZED;i++) if(r->values[i]!=0 && r->values[i]!=1) return FALSE;
 if(r->values[IL_WL_SET] && (r->values[IL_WL_X]<-30000 || r->values[IL_WL_X]>30000 || r->values[IL_WL_Y]<-30000 || r->values[IL_WL_Y]>30000 || r->values[IL_WL_WIDTH]<400 || r->values[IL_WL_WIDTH]>4000 || r->values[IL_WL_HEIGHT]<220 || r->values[IL_WL_HEIGHT]>3000)) return FALSE;
 return r->values[IL_HOST_VISIBLE]>=0 && (r->values[IL_HOST_VISIBLE]&~r->values[IL_DETACHED])==0 && r->values[IL_DETACHED]>=0 && r->values[IL_DETACHED]<=1023 && r->values[IL_TAB]>=0 && r->values[IL_TAB]<8 && r->values[IL_X]>=-30000 && r->values[IL_X]<=30000 && r->values[IL_Y]>=-30000 && r->values[IL_Y]<=30000 && r->values[IL_WIDTH]>=300 && r->values[IL_WIDTH]<=4000 && r->values[IL_HEIGHT]>=200 && r->values[IL_HEIGHT]<=4000;
}
gboolean insight_layout_read(GKeyFile *file,const char *group,InsightLayout *out) {
 if(!file || !group || !out) return FALSE;
 *out=(InsightLayout){0}; GError *error=NULL; gint version=g_key_file_get_integer(file,group,"version",&error);
 if(error || (version!=1 && version!=2 && version!=3)) { g_clear_error(&error); return FALSE; }
 InsightLayout next={.valid=TRUE};
 for(guint i=0;i<(version==1 ? IL_DETACHED : version==2 ? IL_WL_SET : IL_FIELDS);i++) {
  next.values[i]=boolean_field(i) ? g_key_file_get_boolean(file,group,keys[i],&error) : g_key_file_get_integer(file,group,keys[i],&error);
  if(error) { g_clear_error(&error); return FALSE; }
 }
 if(!insight_layout_valid(&next)) return FALSE;
 *out=next; return TRUE;
}
void insight_layout_write(GKeyFile *file,const char *group,const InsightLayout *r) {
 if(!file || !group) return;
 g_key_file_remove_group(file,group,NULL); if(!insight_layout_valid(r)) return;
 g_key_file_set_integer(file,group,"version",3);
 for(guint i=0;i<IL_FIELDS;i++) {
  if(boolean_field(i)) g_key_file_set_boolean(file,group,keys[i],r->values[i]); else g_key_file_set_integer(file,group,keys[i],r->values[i]);
 }
}
