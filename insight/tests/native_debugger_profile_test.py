#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Execute the production debugger profile validator against GLib and reject corrupt snapshots.
# RU: Выполняет производственную проверку профилей с GLib и отклоняет повреждённые снимки.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function

DECLARATIONS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef char gchar;
typedef unsigned int guint;
typedef int gint;
typedef int gboolean;
typedef long glong;
typedef long gssize;
typedef size_t gsize;
typedef struct GKeyFile GKeyFile;
typedef struct {unsigned domain; int code; char *message;} GError;
#define TRUE 1
#define FALSE 0
#define G_N_ELEMENTS(a) (sizeof(a) / sizeof((a)[0]))
#define G_GSIZE_FORMAT "zu"
#define G_CHECKSUM_SHA256 2
#define G_IO_ERROR g_io_error_quark()
#define G_IO_ERROR_INVALID_DATA 35
#define insight_tr(a) (a)
#define g_clear_error(e) do {if (*(e)) g_error_free(*(e)); *(e)=NULL;} while(0)
extern unsigned g_io_error_quark(void);
extern void g_error_free(GError *);
extern void g_free(void *);
extern char *g_strdup_printf(const char *, ...);
extern char *g_compute_checksum_for_string(int, const char *, gssize);
extern char *g_strconcat(const char *, ...);
extern char *g_build_filename(const char *, ...);
extern int g_utf8_validate(const char *, gssize, const char **);
extern glong g_utf8_strlen(const char *, gssize);
extern void g_set_error_literal(GError **, unsigned, int, const char *);
extern GKeyFile *g_key_file_new(void);
extern void g_key_file_unref(GKeyFile *);
extern int g_key_file_get_integer(GKeyFile *, const char *, const char *, GError **);
extern char *g_key_file_get_string(GKeyFile *, const char *, const char *, GError **);
extern void g_key_file_set_integer(GKeyFile *, const char *, const char *, int);
extern void g_key_file_set_string(GKeyFile *, const char *, const char *, const char *);
'''

MAIN = r'''
/* EN: Check required fields, versions, ports, line breaks and scope isolation without UI side effects.
 * RU: Проверяет обязательные поля, версии, порты, переводы строк и изоляцию областей без изменения UI. */
int main(void)
{
    GKeyFile *profile = g_key_file_new();
    GError *error = NULL;
    assert(!native_profile_validate(profile, &error) && error);
    g_clear_error(&error);
    g_key_file_set_integer(profile, "debugger", "version", 1);
    g_key_file_set_integer(profile, "debugger", "mode", 2);
    g_key_file_set_integer(profile, "debugger", "ssh-port", 0);
    g_key_file_set_integer(profile, "debugger", "server-port", 2345);
    for (guint i = 0; i < G_N_ELEMENTS(native_profile_names); i++)
        g_key_file_set_string(profile, "debugger", native_profile_names[i], "");
    assert(native_profile_validate(profile, &error) && !error);
    g_key_file_set_integer(profile, "debugger", "version", 2);
    assert(!native_profile_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_set_integer(profile, "debugger", "version", 1);
    g_key_file_set_integer(profile, "debugger", "server-port", 0);
    assert(!native_profile_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_set_integer(profile, "debugger", "server-port", 2345);
    g_key_file_set_string(profile, "debugger", "host", "host\ncommand");
    assert(!native_profile_validate(profile, &error));
    g_clear_error(&error);
    g_key_file_set_string(profile, "debugger", "host", "build-host");
    assert(native_profile_validate(profile, &error));
    char too_long[4098];
    memset(too_long, 'x', sizeof too_long - 1);
    too_long[sizeof too_long - 1] = '\0';
    g_key_file_set_string(profile, "debugger", "host", too_long);
    assert(!native_profile_validate(profile, &error));
    g_clear_error(&error);
    char *a = native_profile_path("/config", "/project/a", "/program");
    char *b = native_profile_path("/config", "/project/b", "/program");
    char *c = native_profile_path("/config", "/project/a", "/other-program");
    char *d = native_profile_path("/config", "/project/a", "/program");
    assert(strcmp(a, b) && strcmp(a, c) && !strcmp(a, d));
    assert(strstr(a, "/insight/gdb-profiles/") && strstr(a, ".ini"));
    g_free(a);
    g_free(b);
    g_free(c);
    g_free(d);
    g_key_file_unref(profile);
    return 0;
}
'''


class NativeProfile(unittest.TestCase):
    # EN: Compile actual validation and scope selection with strict warnings, using the GLib/GIO runtime.
    # RU: Компилирует настоящую проверку и выбор области со строгими предупреждениями, используя GLib/GIO.
    def test_validation_and_scope(self):
        libraries = [ctypes.util.find_library(name) for name in ('gio-2.0', 'glib-2.0')]
        if not shutil.which('cc') or not all(libraries):
            self.skipTest('Needs cc and GLib/GIO runtime libraries.')
        model = Path(__file__).resolve().parents[2] / 'src/insight-debuggers/gdb-profile-model.inc'
        names = model.read_text().split('static const gchar *native_profile_names[] = {', 1)[1].split('};', 1)[0]
        helpers = '\n'.join(function('src/insight-debuggers/gdb-profile-model.inc', name)
                            for name in ('native_profile_validate', 'native_profile_path'))
        with tempfile.TemporaryDirectory(dir=model.parents[2]) as directory:
            root = Path(directory)
            source = root / 'profile.c'
            source.write_text(DECLARATIONS + 'static const gchar *native_profile_names[] = {' + names + '};\n' + helpers + MAIN)
            subprocess.run(['cc', '-std=c99', '-Wall', '-Wextra', '-Werror', str(source),
                            '-o', str(root / 'profile'), *['-l:' + name for name in libraries]], check=True)
            subprocess.run([str(root / 'profile')], check=True)


if __name__ == '__main__':
    unittest.main()
