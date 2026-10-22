/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "remote_io.h"
#include "remote_credentials.h"
#include <glib/gstdio.h>
#include <string.h>
#ifndef G_OS_WIN32
static void sftp_batch(const char *batch) {
 GError *error=NULL; GSubprocess *process=g_subprocess_new(G_SUBPROCESS_FLAGS_STDIN_PIPE|G_SUBPROCESS_FLAGS_STDOUT_PIPE|G_SUBPROCESS_FLAGS_STDERR_MERGE,&error,"sftp","-D","/usr/lib/openssh/sftp-server","-b","-","unused",NULL); g_assert_no_error(error); gchar *output=NULL;
 g_assert_true(g_subprocess_communicate_utf8(process,batch,NULL,&output,NULL,&error)); g_assert_no_error(error);
 if(!g_subprocess_get_successful(process)) g_error("Local SFTP protocol regression: %s",output);
 g_free(output); g_object_unref(process);
}
#endif
int main(void) {
 GError *error=NULL; gchar *cwd=g_get_current_dir(),*cache=g_build_filename(cwd,"remote-cache",NULL);
 /* A client shipped beside the application must work even outside PATH. */
 gchar *bin=g_build_filename(cwd,"remote-client-bin",NULL);g_assert_cmpint(g_mkdir_with_parents(bin,0700),==,0);
#ifdef G_OS_WIN32
 gchar *client=g_build_filename(bin,"insight-bundled-test.exe",NULL);
#else
 gchar *client=g_build_filename(bin,"insight-bundled-test",NULL);
#endif
 g_assert_true(g_file_set_contents(client,"#!/bin/sh\nexit 0\n",-1,&error));g_assert_no_error(error);g_assert_cmpint(g_chmod(client,0700),==,0);
 gchar *resolved=insight_remote_program("insight-bundled-test",bin);g_assert_cmpstr(resolved,==,client);g_assert_true(g_path_is_absolute(resolved));
#ifndef G_OS_WIN32
 GSubprocessLauncher *spawn=g_subprocess_launcher_new(G_SUBPROCESS_FLAGS_NONE);g_subprocess_launcher_setenv(spawn,"PATH",bin,TRUE);const gchar *args[]={resolved,NULL};GSubprocess *process=g_subprocess_launcher_spawnv(spawn,args,&error);g_assert_no_error(error);g_assert_nonnull(process);g_assert_true(g_subprocess_wait_check(process,NULL,&error));g_assert_no_error(error);g_object_unref(process);g_object_unref(spawn);
#endif
 g_free(resolved);g_remove(client);g_rmdir(bin);g_assert_null(insight_remote_program("insight-bundled-test",bin));g_free(client);g_free(bin);
 InsightRemote *r=insight_remote_new("host-alias","login",2222,"","/src",cache,&error); g_assert_no_error(error); g_assert_nonnull(r);
 gchar **argv=insight_remote_argv(r,TRUE); g_assert_cmpstr(argv[0],==,"sftp"); g_assert_cmpstr(argv[g_strv_length(argv)-1],==,"host-alias"); g_assert_true(g_strv_contains((const gchar *const *)argv,"-oUser=login")); g_strfreev(argv);
 gchar *file=g_build_filename(cache,"folder","source.c",NULL),*path=insight_remote_path(r,file); g_assert_cmpstr(path,==,"/src/folder/source.c"); g_free(path); g_free(file);
 file=g_strconcat(cache,"-other/source.c",NULL); g_assert_null(insight_remote_path(r,file)); g_free(file);
 file=g_build_filename(cache,"..","outside.c",NULL); g_assert_null(insight_remote_path(r,file)); g_free(file);
 g_assert_null(insight_remote_new("-oProxyCommand=bad","login",22,"","/src",cache,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&error);
 g_assert_null(insight_remote_download("/src\nrm /x",cache,FALSE,&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&error);
 gchar *batch=insight_remote_download("/src/a \"quoted\"[*].c",cache,FALSE,&error); g_assert_no_error(error); g_assert_nonnull(strstr(batch,"\\\"quoted\\\"")); g_assert_nonnull(strstr(batch,"[*]")); g_free(batch);
 path=insight_remote_normalize("/project/config","../src/./file.c"); g_assert_cmpstr(path,==,"/project/src/file.c"); g_free(path);
 path=insight_remote_normalize("/project","/elsewhere/../src"); g_assert_cmpstr(path,==,"/src"); g_free(path);
 gchar *config=g_build_filename(cwd,"remote-credentials-test.conf",NULL); g_assert_true(g_file_set_contents(config,"# retained\n[project]\nname=portable\nbase_path=.\n",-1,&error)); g_assert_no_error(error);
 gchar *target=g_compute_checksum_for_string(G_CHECKSUM_SHA256,"host:2222:login",-1);
 g_assert_null(insight_remote_password_load(config,target,&error)); g_assert_no_error(error);
 g_assert_true(insight_remote_password_save(config,target,"тест # \\ пароль",&error)); g_assert_no_error(error); gchar *password=insight_remote_password_load(config,target,&error); g_assert_no_error(error); g_assert_cmpstr(password,==,"тест # \\ пароль"); g_free(password);
 GKeyFile *key=g_key_file_new(); g_assert_true(g_key_file_load_from_file(key,config,G_KEY_FILE_KEEP_COMMENTS,&error)); gchar *name=g_key_file_get_string(key,"project","name",&error); g_assert_cmpstr(name,==,"portable"); g_free(name); g_key_file_unref(key);
 g_assert_false(insight_remote_password_save(config,target,"bad\nline",&error)); g_assert_error(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&error);
 g_assert_true(insight_remote_password_forget(config,target,&error)); g_assert_no_error(error); g_assert_null(insight_remote_password_load(config,target,&error)); g_assert_no_error(error);
 g_assert_true(g_file_set_contents(config,"[broken",-1,&error)); g_assert_false(insight_remote_password_save(config,target,"test",&error)); g_assert_nonnull(error); g_clear_error(&error);
 g_remove(config); g_free(config); g_free(target); insight_remote_free(r); g_free(cache);
#ifndef G_OS_WIN32
 gchar *sftp=g_find_program_in_path("sftp");
 if(sftp && g_file_test("/usr/lib/openssh/sftp-server",G_FILE_TEST_IS_EXECUTABLE)) {
  gchar *source=g_build_filename(cwd,"remote test [x]* \"quoted\".c",NULL),*download=g_build_filename(cwd,"remote downloaded.c",NULL);
  g_assert_true(g_file_set_contents(source,"original\n",-1,&error)); g_assert_no_error(error);
  batch=insight_remote_download(source,download,FALSE,&error); g_assert_no_error(error); sftp_batch(batch); g_free(batch); gchar *contents=NULL; g_assert_true(g_file_get_contents(download,&contents,NULL,&error)); g_assert_cmpstr(contents,==,"original\n"); g_free(contents);
  g_assert_true(g_file_set_contents(download,"updated\n",-1,&error)); batch=insight_remote_upload(download,source,"test",&error); g_assert_no_error(error); sftp_batch(batch); g_free(batch); g_assert_true(g_file_get_contents(source,&contents,NULL,&error)); g_assert_cmpstr(contents,==,"updated\n"); g_free(contents); g_remove(download); g_remove(source); g_free(download); g_free(source);
 }
 g_free(sftp);
#endif
 g_free(cwd); g_print("Remote IO and portable credentials checks passed\n"); return 0;
}
