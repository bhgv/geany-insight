/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "layout_state.h"
#include <glib/gstdio.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static const char *keys[IL_FIELDS]={"left","project","context","follow","wrap","floating","left-split","right-split","context-split","tab","x","y","width","height","detached","host-visible","window-list-set","window-list-visible","window-list-docked","window-list-positioned","window-list-maximized","window-list-x","window-list-y","window-list-width","window-list-height","tab-tray-visible","host-minimized"};
static gboolean boolean_field(guint i) { return i==IL_TRAY_VISIBLE || i<=IL_FLOAT || (i>=IL_WL_SET && i<=IL_WL_MAXIMIZED); }
gboolean insight_layout_valid(const InsightLayout *r) {
 if(!r || !r->valid) return FALSE;
 const InsightPanelLayout *main=&r->main_window;
 if((main->set!=FALSE && main->set!=TRUE) || (main->maximized!=FALSE && main->maximized!=TRUE)) return FALSE;
 if(main->set && (main->x < -30000 || main->x>30000 || main->y < -30000 || main->y>30000 || main->width<400 || main->width>20000 || main->height<220 || main->height>20000)) return FALSE;
 if((r->values[IL_TRAY_VISIBLE]!=0 && r->values[IL_TRAY_VISIBLE]!=1) || r->values[IL_HOST_MINIMIZED]<0 || (r->values[IL_HOST_MINIMIZED]&~r->values[IL_DETACHED]) || (r->values[IL_HOST_MINIMIZED]&r->values[IL_HOST_VISIBLE])) return FALSE;
 for(guint i=0;i<IL_PANEL_COUNT;i++) {
  const InsightPanelLayout *p=&r->panels[i];
  if((p->set!=FALSE && p->set!=TRUE) || (p->maximized!=FALSE && p->maximized!=TRUE)) return FALSE;
  if(p->set && (p->x < -30000 || p->x>30000 || p->y < -30000 || p->y>30000 || p->width<300 || p->width>4000 || p->height<200 || p->height>4000)) return FALSE;
 }
 for(guint i=0;i<=IL_FLOAT;i++) if(r->values[i]!=0 && r->values[i]!=1) return FALSE;
 for(guint i=IL_LEFT_SPLIT;i<=IL_CONTEXT_SPLIT;i++) if(r->values[i]<0 || r->values[i]>20000) return FALSE;
 for(guint i=IL_WL_SET;i<=IL_WL_MAXIMIZED;i++) if(r->values[i]!=0 && r->values[i]!=1) return FALSE;
 if(r->values[IL_WL_SET] && (r->values[IL_WL_X]<-30000 || r->values[IL_WL_X]>30000 || r->values[IL_WL_Y]<-30000 || r->values[IL_WL_Y]>30000 || r->values[IL_WL_WIDTH]<400 || r->values[IL_WL_WIDTH]>4000 || r->values[IL_WL_HEIGHT]<220 || r->values[IL_WL_HEIGHT]>3000)) return FALSE;
 return r->values[IL_HOST_VISIBLE]>=0 && (r->values[IL_HOST_VISIBLE]&~r->values[IL_DETACHED])==0 && r->values[IL_DETACHED]>=0 && r->values[IL_DETACHED]<=1023 && r->values[IL_TAB]>=0 && r->values[IL_TAB]<8 && r->values[IL_X]>=-30000 && r->values[IL_X]<=30000 && r->values[IL_Y]>=-30000 && r->values[IL_Y]<=30000 && r->values[IL_WIDTH]>=300 && r->values[IL_WIDTH]<=4000 && r->values[IL_HEIGHT]>=200 && r->values[IL_HEIGHT]<=4000;
}
gboolean insight_layout_read(GKeyFile *file,const char *group,InsightLayout *out) {
 if(!file || !group || !out) return FALSE;
 *out=(InsightLayout){0}; GError *error=NULL; gint version=g_key_file_get_integer(file,group,"version",&error);
 if(error || (version<1 || version>IL_FORMAT_VERSION)) { g_clear_error(&error); return FALSE; }
 InsightLayout next={.valid=TRUE};
 for(guint i=0;i<(version==1 ? IL_DETACHED : version==2 ? IL_WL_SET : version<5 ? IL_TRAY_VISIBLE : IL_FIELDS);i++) {
  next.values[i]=boolean_field(i) ? g_key_file_get_boolean(file,group,keys[i],&error) : g_key_file_get_integer(file,group,keys[i],&error);
  if(error) { g_clear_error(&error); return FALSE; }
 }
 if(version<5) next.values[IL_TRAY_VISIBLE]=1;
 if(version>=4) for(guint i=0;i<IL_PANEL_COUNT;i++) {
  InsightPanelLayout *p=&next.panels[i];gchar key[48];
  const gchar *fields[]={"set","maximized","x","y","width","height"};gint v[6];
  for(guint j=0;j<G_N_ELEMENTS(fields);j++) { g_snprintf(key,sizeof key,"panel-%u-%s",i,fields[j]);v[j]=j<2 ? g_key_file_get_boolean(file,group,key,&error) : g_key_file_get_integer(file,group,key,&error);if(error) { g_clear_error(&error);return FALSE; } }
  *p=(InsightPanelLayout){v[0],v[1],v[2],v[3],v[4],v[5]};
 }
 if(version>=6) {
  const gchar *fields[]={"main-set","main-maximized","main-x","main-y","main-width","main-height"};gint v[6];
  for(guint j=0;j<G_N_ELEMENTS(fields);j++) { v[j]=j<2 ? g_key_file_get_boolean(file,group,fields[j],&error) : g_key_file_get_integer(file,group,fields[j],&error);if(error) { g_clear_error(&error);return FALSE; } }
  next.main_window=(InsightPanelLayout){v[0],v[1],v[2],v[3],v[4],v[5]};
 }
 if(!insight_layout_valid(&next)) return FALSE;
 *out=next; return TRUE;
}
void insight_layout_write(GKeyFile *file,const char *group,const InsightLayout *r) {
 if(!file || !group) return;
 g_key_file_remove_group(file,group,NULL); if(!insight_layout_valid(r)) return;
 g_key_file_set_integer(file,group,"version",IL_FORMAT_VERSION);
 for(guint i=0;i<IL_FIELDS;i++) {
  if(boolean_field(i)) g_key_file_set_boolean(file,group,keys[i],r->values[i]); else g_key_file_set_integer(file,group,keys[i],r->values[i]);
 }
 for(guint i=0;i<IL_PANEL_COUNT;i++) {
  const InsightPanelLayout *p=&r->panels[i];gchar key[48];
  const gchar *fields[]={"set","maximized","x","y","width","height"};gint v[]={p->set,p->maximized,p->x,p->y,p->width,p->height};
  for(guint j=0;j<G_N_ELEMENTS(fields);j++) { g_snprintf(key,sizeof key,"panel-%u-%s",i,fields[j]);if(j<2) g_key_file_set_boolean(file,group,key,v[j]);else g_key_file_set_integer(file,group,key,v[j]); }
 }
 const InsightPanelLayout *main=&r->main_window;
 const gchar *fields[]={"main-set","main-maximized","main-x","main-y","main-width","main-height"};gint v[]={main->set,main->maximized,main->x,main->y,main->width,main->height};
 for(guint j=0;j<G_N_ELEMENTS(fields);j++) { if(j<2) g_key_file_set_boolean(file,group,fields[j],v[j]);else g_key_file_set_integer(file,group,fields[j],v[j]); }

}

/* The external format contains layout state only, never source paths or text. */
gboolean insight_layout_load_file(const gchar *path,InsightLayout *out,GError **error) {
 if(!out) return FALSE;
 *out=(InsightLayout){0};FILE *file=path ? g_fopen(path,"rb") : NULL;
 if(!file) { gint code=path ? errno : EINVAL;g_set_error_literal(error,G_FILE_ERROR,g_file_error_from_errno(code),g_strerror(code));return FALSE; }
 gchar *text=g_malloc(65537);gsize size=fread(text,1,65537,file);gboolean failed=ferror(file)!=0;gint code=errno;fclose(file);
 if(failed) { g_free(text);g_set_error_literal(error,G_FILE_ERROR,g_file_error_from_errno(code ? code : EIO),"Could not read layout file");return FALSE; }
 if(size>65536 || memchr(text,0,size)) { g_free(text);g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Layout file exceeds 64 KiB or contains NUL bytes");return FALSE; }
 GKeyFile *key=g_key_file_new();gboolean ok=g_key_file_load_from_data(key,text,size,G_KEY_FILE_NONE,error);g_free(text);
 if(ok && !insight_layout_read(key,"layout",out)) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Invalid or incomplete layout file");ok=FALSE; }
 g_key_file_unref(key);return ok;
}
gboolean insight_layout_save_file(const gchar *path,const InsightLayout *record,GError **error) {
 if(!path || !insight_layout_valid(record)) { g_set_error_literal(error,G_FILE_ERROR,G_FILE_ERROR_INVAL,"Layout cannot be saved: invalid panel geometry");return FALSE; }
 GKeyFile *key=g_key_file_new();insight_layout_write(key,"layout",record);gsize size=0;gchar *text=g_key_file_to_data(key,&size,NULL);
 gboolean ok=g_file_set_contents(path,text,(gssize)size,error);g_free(text);g_key_file_unref(key);return ok;
}
