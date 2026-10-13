#include "layout_state.h"
#include <glib.h>
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
 r.values[IL_WIDTH]=0; g_assert_false(insight_layout_valid(&r)); insight_layout_write(f,"layout-A",&r); g_assert_false(g_key_file_has_group(f,"layout-A"));
 g_assert_false(insight_layout_read(f,"missing",&out)); g_key_file_unref(f); g_print("Layout state checks passed\n"); return 0;
}
