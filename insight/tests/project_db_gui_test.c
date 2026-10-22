/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "document.h"
#include "main.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
static gchar *root,*a,*b,*added;static guint phase,stable;static GtkWidget *window;
static GtkWidget *named(GtkWidget *w,const gchar *name) {
 if(!g_strcmp0(gtk_widget_get_name(w),name)) return w;if(GTK_IS_MENU_ITEM(w)) { GtkWidget *m=gtk_menu_item_get_submenu(GTK_MENU_ITEM(w));if(m) { GtkWidget *f=named(m,name);if(f) return f; } }
 if(GTK_IS_CONTAINER(w)) { GList *list=gtk_container_get_children(GTK_CONTAINER(w));GtkWidget *f=NULL;for(GList *p=list;p&&!f;p=p->next) f=named(p->data,name);g_list_free(list);return f; }return NULL;
}
static guint metric(const gchar *key) { return GPOINTER_TO_UINT(g_object_get_data(G_OBJECT(window),key))-1; }
static gboolean ready(guint parsed,guint reused,guint members) {
 return !g_object_get_data(G_OBJECT(window),"insight-db-scanning")&&g_object_get_data(G_OBJECT(window),"insight-db-saved")&&metric("insight-db-parsed")==parsed&&metric("insight-db-reused")==reused&&metric("insight-db-members")==members;
}
static void command(const gchar *name) { GtkWidget *item=named(window,name);g_assert_nonnull(item);gtk_menu_item_activate(GTK_MENU_ITEM(item)); }
static gboolean step(gpointer unused) {
 (void)unused;GList *list=gtk_window_list_toplevels();if(!window) for(GList *p=list;p;p=p->next) if(named(p->data,"insight-project-synchronize")) { window=p->data;break; }g_list_free(list);if(!window) return G_SOURCE_CONTINUE;
 if(g_getenv("INSIGHT_DB_CHILD")&&!strcmp(g_getenv("INSIGHT_DB_CHILD"),"create")) { if(!ready(3,0,4)) return G_SOURCE_CONTINUE;g_print("Initial project: 3 parsed, database saved\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE; }
 if(phase==0) { if(!ready(0,4,4)) return G_SOURCE_CONTINUE;g_print("Reopened project: zero source files parsed, four members reused (including explicit binary)\n");g_file_set_contents(a,"int alpha(void) { return 3; }\n",-1,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(phase==1) { if(!ready(1,3,4)) return G_SOURCE_CONTINUE;g_file_set_contents(added,"int gamma(void) { return 4; }\n",-1,NULL);phase++;return G_SOURCE_CONTINUE; }
 if(phase==2) { if(!ready(1,4,5)) return G_SOURCE_CONTINUE;g_remove(b);phase++;return G_SOURCE_CONTINUE; }
 if(phase==3) { if(!ready(0,4,4)) return G_SOURCE_CONTINUE;command("insight-project-synchronize");phase++;return G_SOURCE_CONTINUE; }
 if(phase==4) { if(!ready(0,4,4)) return G_SOURCE_CONTINUE;command("insight-project-rebuild");phase++;return G_SOURCE_CONTINUE; }
 if(phase==5) { if(!ready(3,0,4)) return G_SOURCE_CONTINUE;GeanyDocument *doc=document_open_file(a,FALSE,NULL,NULL);g_assert_nonnull(doc);phase++;return G_SOURCE_CONTINUE; }
 if(++stable<10) return G_SOURCE_CONTINUE;g_assert_false(g_object_get_data(G_OBJECT(window),"insight-db-scanning")!=NULL);g_print("Persistent project GUI: cold/warm start, changed/add/delete, synchronize, force rebuild and idle stability passed\n");gtk_menu_item_activate(GTK_MENU_ITEM(ui_lookup_widget(window,"menu_quit1")));return G_SOURCE_REMOVE;
}
static gboolean timeout(gpointer unused) { (void)unused;g_error("Persistent project timeout phase %u parsed %u reused %u members %u",phase,window?metric("insight-db-parsed"):0,window?metric("insight-db-reused"):0,window?metric("insight-db-members"):0);return G_SOURCE_REMOVE; }
int main(int argc,char **argv) {
 if(!g_getenv("GEANY_RUN_GUI_TESTS")||!g_getenv("DISPLAY")) return 77;
 if(!g_getenv("INSIGHT_DB_CHILD")) {
  gchar *dir=g_dir_make_tmp("persistent-project-native-XXXXXX",NULL),*src=g_build_filename(dir,"src",NULL),*profile=g_build_filename(dir,"profile",NULL),*project=g_build_filename(dir,"test.geany",NULL);g_mkdir(src,0700);g_mkdir(profile,0700);gchar *config=g_build_filename(profile,"geany.conf",NULL);g_file_set_contents(config,"[geany]\nconfirm_exit=false\n",-1,NULL);gchar *f=g_build_filename(src,"a.c",NULL);g_file_set_contents(f,"int alpha(void) { return 1; }\n",-1,NULL);g_free(f);f=g_build_filename(src,"b.c",NULL);g_file_set_contents(f,"int beta(void) { return 2; }\n",-1,NULL);g_free(f);f=g_build_filename(src,"empty.c",NULL);g_file_set_contents(f,"",0,NULL);g_free(f);f=g_build_filename(src,"asset.png",NULL);const gchar binary[]={ (gchar)0x89,'P','N','G',0,1,2,3 };g_file_set_contents(f,binary,sizeof binary,NULL);g_free(f);gchar *content=g_strdup_printf("[project]\nname=Persistent fixture\nbase_path=%s\n[insight-project-files]\nversion=1\nautomatic=true\nexclude-masks=\nfiles=asset.png;\ntrees=\nexcluded-files=\nexcluded-trees=\n",src);g_file_set_contents(project,content,-1,NULL);g_free(content);
  for(guint n=0;n<2;n++) { gchar **env=g_get_environ();env=g_environ_setenv(env,"INSIGHT_DB_CHILD",n?"reopen":"create",TRUE);env=g_environ_setenv(env,"INSIGHT_DB_ROOT",src,TRUE);gchar *args[]={argv[0],"--new-instance","--no-session","--config",profile,project,NULL};gint status=0;GError *error=NULL;g_assert_true(g_spawn_sync(NULL,args,env,0,NULL,NULL,NULL,NULL,&status,&error));g_assert_no_error(error);g_assert_true(g_spawn_check_wait_status(status,&error));g_assert_no_error(error);g_strfreev(env); }return 0;
 }
 root=g_strdup(g_getenv("INSIGHT_DB_ROOT"));a=g_build_filename(root,"a.c",NULL);b=g_build_filename(root,"b.c",NULL);added=g_build_filename(root,"new.c",NULL);g_log_set_always_fatal(G_LOG_FATAL_MASK|G_LOG_LEVEL_CRITICAL);g_timeout_add(100,step,NULL);g_timeout_add_seconds(45,timeout,NULL);return main_lib(argc,argv);
}
