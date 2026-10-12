/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "extensions.h"
#include <glib/gstdio.h>
#include <string.h>
static char result[128];
static const char *text(void *ctx) { (void)ctx; return "hello"; }
static int replace(void *ctx,const char *s) { (void)ctx; g_strlcpy(result,s,sizeof result); return 1; }
typedef struct { InsightExtensions *manager; const char *source; } Reentry;
static int reenter(void *context,const char *s) {
 (void)s; Reentry *r=context; GError *error=NULL;
 g_assert(insight_session_is_running(insight_extensions_session(r->manager)));
 g_assert(!insight_extensions_select(r->manager,r->source,&error));
 g_assert_error(error,G_IO_ERROR,G_IO_ERROR_BUSY); g_clear_error(&error);
 g_assert(!insight_extensions_unload(r->manager,&error));
 g_assert_error(error,G_IO_ERROR,G_IO_ERROR_BUSY); g_clear_error(&error);
 g_assert(!insight_extensions_restore(r->manager,&error));
 g_assert_error(error,G_IO_ERROR,G_IO_ERROR_BUSY); g_clear_error(&error);
 return 1;
}
static void write_file(const char *path,const char *contents) {
 GError *error=NULL; g_assert(g_file_set_contents(path,contents,-1,&error)); g_assert_no_error(error);
}
static void invoke(InsightExtensions *m,const char *name,const char *expected) {
 InsightHost host={NULL,text,text,replace,NULL}; char error[512];
 g_assert_cmpint(insight_session_invoke(insight_extensions_session(m),name,&host,error,sizeof error),==,0);
 g_assert_cmpstr(result,==,expected);
}
int main(int argc,char **argv) {
 g_assert_cmpint(argc,==,2);
 gchar *directory=g_build_filename(argv[1],"extensions-XXXXXX",NULL); g_assert_nonnull(g_mkdtemp(directory));
 gchar *settings=g_build_filename(directory,"extension.conf",NULL);
 #ifdef G_OS_WIN32
 gchar *source=g_build_filename(directory,"команды.lua",NULL);
#else
 gchar *source=g_build_filename(directory,"команды\n.lua",NULL);
#endif
 gchar *bad=g_build_filename(directory,"bad.lua",NULL);
 const char *program="local n=0; commands.register('count','Counter',function() n=n+1; editor.replace_selection(tostring(n)) end)";
 write_file(source,program); write_file(bad,"not lua !!!");
 InsightExtensions *m=insight_extensions_new(settings); g_assert_nonnull(m); GError *error=NULL;
 g_assert(insight_extensions_restore(m,&error)); g_assert_no_error(error);
 g_assert_cmpuint(insight_session_command_count(insight_extensions_session(m)),==,0);
 g_assert(insight_extensions_select(m,source,&error)); g_assert_no_error(error); invoke(m,"count","1");
 gchar *saved=NULL; gsize saved_length=0;
 g_assert(g_file_get_contents(settings,&saved,&saved_length,&error)); g_assert_no_error(error);
 g_assert(!insight_extensions_select(m,bad,&error)); g_assert_nonnull(error); g_clear_error(&error);
 invoke(m,"count","2");
 gchar *after=NULL; gsize after_length=0;
 g_assert(g_file_get_contents(settings,&after,&after_length,&error)); g_assert_no_error(error);
 g_assert_cmpuint(saved_length,==,after_length); g_assert_cmpmem(saved,saved_length,after,after_length); g_free(after);
 insight_extensions_free(m); m=insight_extensions_new(settings);
 g_assert(insight_extensions_restore(m,&error)); g_assert_no_error(error); invoke(m,"count","1");
 /* A missing source at startup does not rewrite the user's saved selection. */
 g_assert_cmpint(g_unlink(source),==,0);
 g_assert(!insight_extensions_restore(m,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_NOT_FOUND); g_clear_error(&error);
 invoke(m,"count","2");
 write_file(source,"invalid syntax !!");
 g_assert(!insight_extensions_restore(m,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&error);
 g_assert_cmpuint(insight_session_command_count(insight_extensions_session(m)),==,1); write_file(source,program);
 /* Persistence failure does not replace an already running extension. */
 gchar *backup=g_strconcat(settings,".backup",NULL);
 g_assert_cmpint(g_rename(settings,backup),==,0); g_assert_cmpint(g_mkdir(settings,0700),==,0);
 g_assert(!insight_extensions_select(m,source,&error)); g_assert_nonnull(error); g_clear_error(&error);
 invoke(m,"count","3");
 g_assert(!insight_extensions_unload(m,&error)); g_assert_nonnull(error); g_clear_error(&error); invoke(m,"count","4");
 g_assert_cmpint(g_rmdir(settings),==,0); g_assert_cmpint(g_rename(backup,settings),==,0); g_free(backup);
 /* Strict settings parsing: version, canonical base64, NUL and relative paths. */
 const char *invalid[]={"broken", "[extension]\nversion=2\npath-base64=\n",
  "[extension]\nversion=1\npath-base64=!!!\n", "[extension]\nversion=1\npath-base64=YQ==\n",
  "[extension]\nversion=1\npath-base64=LwBh\n", "[extension]\nversion=1\npath-base64= L2E= \n"};
 for(guint n=0;n<G_N_ELEMENTS(invalid);n++) {
  write_file(settings,invalid[n]); g_assert(!insight_extensions_restore(m,&error));
  g_assert_nonnull(error); g_clear_error(&error);
  g_assert_cmpuint(insight_session_command_count(insight_extensions_session(m)),==,1);
 }
 g_assert(g_file_set_contents(settings,"[extension]\0bad",15,&error)); g_assert_no_error(error);
 g_assert(!insight_extensions_restore(m,&error)); g_assert_nonnull(error); g_clear_error(&error);
 g_assert(g_file_set_contents(settings,saved,(gssize)saved_length,&error)); g_assert_no_error(error); g_free(saved);
 /* Directory / oversize inputs are refused before the interpreter sees them. */
 g_assert(!insight_extensions_select(m,directory,&error)); g_assert_nonnull(error); g_clear_error(&error);
 gchar *large=g_malloc0(256*1024+2); memset(large,'x',256*1024+1);
 g_assert(g_file_set_contents(bad,large,256*1024+1,&error)); g_assert_no_error(error); g_free(large);
 g_assert(!insight_extensions_select(m,bad,&error)); g_assert_nonnull(error); g_clear_error(&error);
 g_assert_cmpuint(insight_session_command_count(insight_extensions_session(m)),==,1);
 Reentry reentry={m,source}; InsightHost guarded={&reentry,text,text,reenter,NULL}; char message[512];
 g_assert_cmpint(insight_session_invoke(insight_extensions_session(m),"count",&guarded,message,sizeof message),==,0);
 g_assert(!insight_session_is_running(insight_extensions_session(m)));
 write_file(bad,""); char *empty=NULL; gsize empty_size=123;
 g_assert(insight_read_lua_file(bad,&empty,&empty_size,&error)); g_assert_no_error(error);
 g_assert_nonnull(empty); g_assert_cmpuint(empty_size,==,0); g_free(empty);
 g_assert(insight_extensions_unload(m,&error)); g_assert_no_error(error);
 g_assert_cmpuint(insight_session_command_count(insight_extensions_session(m)),==,0);
 insight_extensions_free(m); m=insight_extensions_new(settings);
 g_assert(insight_extensions_restore(m,&error)); g_assert_no_error(error);
 g_assert_cmpuint(insight_session_command_count(insight_extensions_session(m)),==,0);
 insight_extensions_free(m);
 g_assert_cmpint(g_unlink(settings),==,0); g_assert_cmpint(g_unlink(source),==,0); g_assert_cmpint(g_unlink(bad),==,0);
 g_assert_cmpint(g_rmdir(directory),==,0); g_free(settings); g_free(source); g_free(bad); g_free(directory);
 g_print("extensions: all checks passed\n"); return 0;
}
