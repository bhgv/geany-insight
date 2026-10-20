#include "layout_state.h"
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
int main(void) {
 GKeyFile *f=g_key_file_new(); InsightLayout r={.valid=TRUE,.values={1,1,1,0,1,1,240,850,500,3,-100,50,440,620}},out;
 g_assert_true(insight_layout_valid(&r));
 insight_layout_write(f,"layout-A",&r); g_assert_true(insight_layout_read(f,"layout-A",&out));
 for(guint i=0;i<IL_FIELDS;i++) g_assert_cmpint(out.values[i],==,r.values[i]);
 g_key_file_set_integer(f,"layout-A","tab",8); g_assert_false(insight_layout_read(f,"layout-A",&out)); g_assert_false(out.valid);
 insight_layout_write(f,"layout-A",&r); g_key_file_set_string(f,"layout-A","left","garbage"); g_assert_false(insight_layout_read(f,"layout-A",&out));
 insight_layout_write(f,"layout-A",&r); g_key_file_remove_key(f,"layout-A","width",NULL); g_assert_false(insight_layout_read(f,"layout-A",&out));
 insight_layout_write(f,"layout-A",&r); g_key_file_set_integer(f,"layout-A","version",9); g_assert_false(insight_layout_read(f,"layout-A",&out));
 insight_layout_write(f,"layout-A",&r); g_key_file_set_integer(f,"layout-A","version",1);
 g_key_file_remove_key(f,"layout-A","detached",NULL); g_key_file_remove_key(f,"layout-A","host-visible",NULL);
 g_assert_true(insight_layout_read(f,"layout-A",&out)); g_assert_cmpint(out.values[IL_DETACHED],==,0);
 r.values[IL_DETACHED]=513; r.values[IL_HOST_VISIBLE]=512; insight_layout_write(f,"layout-A",&r);
 g_assert_true(insight_layout_read(f,"layout-A",&out)); g_assert_cmpint(out.values[IL_HOST_VISIBLE],==,512);
 g_key_file_set_integer(f,"layout-A","host-visible",2); g_assert_false(insight_layout_read(f,"layout-A",&out));
 insight_layout_write(f,"layout-A",&r);g_key_file_set_integer(f,"layout-A","version",2);g_assert_true(insight_layout_read(f,"layout-A",&out));g_assert_cmpint(out.values[IL_WL_SET],==,0);
 r.values[IL_WL_SET]=1;r.values[IL_WL_VISIBLE]=1;r.values[IL_WL_DOCKED]=1;r.values[IL_WL_POSITIONED]=1;r.values[IL_WL_X]=-200;r.values[IL_WL_Y]=120;r.values[IL_WL_WIDTH]=850;r.values[IL_WL_HEIGHT]=430;insight_layout_write(f,"layout-A",&r);g_assert_true(insight_layout_read(f,"layout-A",&out));for(guint i=0;i<IL_FIELDS;i++) g_assert_cmpint(out.values[i],==,r.values[i]);
 g_key_file_remove_key(f,"layout-A","window-list-width",NULL);g_assert_false(insight_layout_read(f,"layout-A",&out));insight_layout_write(f,"layout-A",&r);g_key_file_set_integer(f,"layout-A","window-list-height",0);g_assert_false(insight_layout_read(f,"layout-A",&out));
 r.values[IL_WL_VISIBLE]=2;g_assert_false(insight_layout_valid(&r));r.values[IL_WL_VISIBLE]=1;
 for(guint i=0;i<IL_PANEL_COUNT;i++) r.panels[i]=(InsightPanelLayout){TRUE,i%2,-200+(gint)i*10,80+(gint)i,480+(gint)i,600+(gint)i};
 insight_layout_write(f,"layout-A",&r);g_assert_true(insight_layout_read(f,"layout-A",&out));
 for(guint i=0;i<IL_PANEL_COUNT;i++) { g_assert_cmpint(out.panels[i].set,==,TRUE);g_assert_cmpint(out.panels[i].maximized,==,(gint)i%2);g_assert_cmpint(out.panels[i].x,==,r.panels[i].x);g_assert_cmpint(out.panels[i].width,==,r.panels[i].width);g_assert_cmpint(out.panels[i].height,==,r.panels[i].height); }
 g_key_file_remove_key(f,"layout-A","panel-9-height",NULL);g_assert_false(insight_layout_read(f,"layout-A",&out));g_assert_false(out.valid);
 insight_layout_write(f,"layout-A",&r);g_key_file_set_integer(f,"layout-A","panel-5-width",299);g_assert_false(insight_layout_read(f,"layout-A",&out));
 insight_layout_write(f,"layout-A",&r);g_key_file_set_string(f,"layout-A","panel-5-maximized","bad");g_assert_false(insight_layout_read(f,"layout-A",&out));
 insight_layout_write(f,"layout-A",&r);g_key_file_set_integer(f,"layout-A","version",3);g_assert_true(insight_layout_read(f,"layout-A",&out));for(guint i=0;i<IL_PANEL_COUNT;i++) g_assert_false(out.panels[i].set);
 r.panels[0].width=4001;g_assert_false(insight_layout_valid(&r));r.panels[0].width=480;
 r.values[IL_TRAY_VISIBLE]=1;r.values[IL_HOST_MINIMIZED]=1;insight_layout_write(f,"layout-A",&r);g_assert_true(insight_layout_read(f,"layout-A",&out));g_assert_cmpint(out.values[IL_HOST_MINIMIZED],==,1);
 r.values[IL_HOST_MINIMIZED]=512;g_assert_false(insight_layout_valid(&r));r.values[IL_HOST_MINIMIZED]=2;g_assert_false(insight_layout_valid(&r));r.values[IL_HOST_MINIMIZED]=1;
 insight_layout_write(f,"layout-A",&r);g_key_file_set_integer(f,"layout-A","version",4);g_assert_true(insight_layout_read(f,"layout-A",&out));g_assert_cmpint(out.values[IL_TRAY_VISIBLE],==,1);g_assert_cmpint(out.values[IL_HOST_MINIMIZED],==,0);
 r.values[IL_HOST_MINIMIZED]=0;
 gchar *dir=g_dir_make_tmp("layout-files-XXXXXX",NULL),*path=g_build_filename(dir,"review.geany-layout",NULL);GError *error=NULL;
 g_assert_true(insight_layout_save_file(path,&r,&error));g_assert_no_error(error);g_assert_true(insight_layout_load_file(path,&out,&error));g_assert_no_error(error);g_assert_cmpint(out.panels[9].height,==,r.panels[9].height);
 r.panels[0].width=1;g_assert_false(insight_layout_save_file(path,&r,&error));g_clear_error(&error);r.panels[0].width=480;g_assert_true(insight_layout_load_file(path,&out,&error));g_assert_no_error(error);
 g_assert_true(g_file_set_contents(path,"[layout]\nversion=5\n",-1,NULL));g_assert_false(insight_layout_load_file(path,&out,&error));g_assert_false(out.valid);g_clear_error(&error);
 gchar *large=g_malloc0(65537);memset(large,'x',65537);g_assert_true(g_file_set_contents(path,large,65537,NULL));g_free(large);g_assert_false(insight_layout_load_file(path,&out,&error));g_clear_error(&error);
 g_assert_true(g_file_set_contents(path,"[layout]\0x",10,NULL));g_assert_false(insight_layout_load_file(path,&out,&error));g_clear_error(&error);g_remove(path);g_assert_false(insight_layout_load_file(path,&out,&error));g_clear_error(&error);g_rmdir(dir);g_free(path);g_free(dir);
 r.values[IL_WIDTH]=0; g_assert_false(insight_layout_valid(&r)); insight_layout_write(f,"layout-A",&r); g_assert_false(g_key_file_has_group(f,"layout-A"));
 g_assert_false(insight_layout_read(f,"missing",&out)); g_key_file_unref(f); g_print("Layout state checks passed\n"); return 0;
}
