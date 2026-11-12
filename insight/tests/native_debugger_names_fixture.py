#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Supply runtime-only GLib ABI declarations for production naming model tests without GTK headers.
# RU: Предоставляет объявления ABI GLib для проверок производственной модели имён без заголовков GTK.
from native_debugger_profile_test import DECLARATIONS
from native_debugger_profile_locations_test import EXTRA

ABI = DECLARATIONS + EXTRA + r'''
#include <stdint.h>
#include <limits.h>
#include <ctype.h>
typedef long gint64;
typedef void GtkWidget;
typedef void GtkListStore;
typedef struct GRegex GRegex;
typedef struct GMatchInfo GMatchInfo;
#define GUINT_TO_POINTER(v) ((void *)(uintptr_t)(v))
#define GPOINTER_TO_UINT(v) ((guint)(uintptr_t)(v))
#define G_MAXUINT64 UINT64_MAX
#define G_MAXINT64 INT64_MAX
#define G_GINT64_MODIFIER "l"
#define G_REGEX_OPTIMIZE (1 << 13)
#define g_new(t,n) ((t *)malloc((n) * sizeof(t)))
#define g_ascii_isspace(c) isspace((unsigned char)(c))
#define g_ascii_isdigit(c) isdigit((unsigned char)(c))
#define g_ascii_isxdigit(c) isxdigit((unsigned char)(c))
#define g_ascii_isalnum(c) isalnum((unsigned char)(c))
extern int g_key_file_has_group(GKeyFile *, const char *);
extern int g_key_file_has_key(GKeyFile *, const char *, const char *, GError **);
extern GString *g_string_append_c(GString *, char);
extern char *g_utf8_casefold(const char *, long);
extern char **g_strsplit(const char *, const char *, int);
extern void g_strfreev(char **);
extern char *g_strchug(char *);
extern char *g_strchomp(char *);
#define g_strstrip(s) g_strchomp(g_strchug(s))
extern void *g_ptr_array_remove_index(GPtrArray *, guint);
extern int g_hash_table_replace(GHashTable *, void *, void *);
extern int g_hash_table_contains(GHashTable *, const void *);
extern int g_hash_table_add(GHashTable *, void *);
extern guint g_hash_table_size(GHashTable *);
extern void g_hash_table_remove_all(GHashTable *);
extern GRegex *g_regex_new(const char *, int, int, GError **);
extern int g_regex_match(GRegex *, const char *, int, GMatchInfo **);
extern void g_regex_unref(GRegex *);
extern int g_match_info_matches(GMatchInfo *);
extern char *g_match_info_fetch(GMatchInfo *, int);
extern int g_match_info_next(GMatchInfo *, GError **);
extern void g_match_info_free(GMatchInfo *);
'''


# EN: Return real model source with caller-provided session declaration; GTK is not executed by this fixture.
# RU: Возвращает настоящие модели с объявлением сеанса session; GTK эта фикстура не выполняет.
def model_source(session):
    from pathlib import Path
    from native_debugger_models_test import function
    directory = Path('src/insight-debuggers/disassembly')
    helpers = '\n'.join(function('src/insight-debuggers/gdb-context.inc', name)
                        for name in ('native_debug_field', 'native_next_tuple'))
    source = ABI + (directory / 'state.inc').read_text() + session + helpers
    source += Path('src/insight-debuggers/gdb-disassembly-model.inc').read_text()
    for name in ('rules.inc', 'resolve.inc', 'annotations.inc', 'symbols.inc'):
        source += (directory / name).read_text()
    return source
