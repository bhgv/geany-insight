#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Test production snapshot ownership and Verilog lexical extents without GTK development headers.
# RU: Проверяет контекст снимков и лексические диапазоны Verilog из настоящего кода без заголовков GTK.
import ctypes.util
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from native_debugger_models_test import function

STATE = r'''
#include <assert.h>
#include <ctype.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef char gchar;
typedef unsigned char guchar;
typedef size_t gsize;
typedef unsigned int guint;
typedef int gboolean;
typedef struct {void **pdata; guint len;} GPtrArray;
typedef struct {char *name; size_t begin, end;} InsightDiffSymbol;
typedef struct {size_t a_begin,a_end,b_begin,b_end;} InsightDiffHunk;
typedef struct {char *sources[2]; GPtrArray *symbols[2];} Merge;
#define TRUE 1
#define FALSE 0
#define MIN(a,b) ((a)<(b)?(a):(b))
#define G_MAXSIZE SIZE_MAX
#define G_N_ELEMENTS(a) (sizeof(a)/sizeof((a)[0]))
#define g_ascii_isalnum(c) isalnum((unsigned char)(c))
#define g_ascii_isspace(c) isspace((unsigned char)(c))
#define insight_tr(s) (s)
extern char *g_strdup(const char *);
extern char *g_strdup_printf(const char *, ...);
extern int g_strcmp0(const char *, const char *);
extern int g_utf8_validate(const char *, long, const char **);
extern void g_free(void *);
'''

MAIN = r'''
/* EN: Check ownership, ambiguity and boundary cases using production helpers; no external files are accessed.
 * RU: Проверяет принадлежность, неоднозначность и границы настоящих обработчиков без внешних файлов. */
int main(void)
{
    InsightDiffSymbol scoped = {"Top.worker", 0, 2};
    assert(diff_symbol_matches(&scoped, "worker"));
    assert(!diff_symbol_matches(&scoped, "work"));
    assert(!diff_symbol_matches(&scoped, "Top"));
    scoped.name = "Box::work";
    assert(diff_symbol_matches(&scoped, "work"));
    scoped.name = "bad\xff";
    assert(!diff_symbol_matches(&scoped, "bad"));
    char source[] = "abcdefghijklmnopqrst";
    InsightDiffSymbol outer = {"Outer",0,20}, inner = {"Inner",3,12};
    InsightDiffSymbol right = {"Renamed",3,12}, ambiguous = {"Other",3,12};
    void *left_items[] = {&outer,&inner,&ambiguous};
    void *right_items[] = {&right};
    GPtrArray left = {left_items,2}, right_ranges = {right_items,1};
    Merge m = {{source,source},{&left,&right_ranges}};
    assert(!strcmp(diff_symbols_owner(&m,0,4,7),"Inner"));
    assert(!strcmp(diff_symbols_owner(&m,0,12,17),"Outer"));
    assert(!diff_symbols_owner(&m,0,4,4));
    InsightDiffHunk h = {4,7,4,7};
    char *label = diff_symbols_hunk(&m,&h);
    assert(!strcmp(label,"Inner → Renamed"));
    g_free(label);
    left.len = 3;
    assert(!diff_symbols_owner(&m,0,4,7));
    left.len = 2;
    inner.end = 99;
    assert(!strcmp(diff_symbols_owner(&m,0,4,7),"Outer"));
    inner.end = 12;
    inner.name = "\xff";
    assert(!strcmp(diff_symbols_owner(&m,0,4,7),"Outer"));
    h.a_begin = h.a_end;
    label = diff_symbols_hunk(&m,&h);
    assert(!strcmp(label,"Right: Renamed"));
    g_free(label);
    m.symbols[0] = m.symbols[1] = NULL;
    label = diff_symbols_hunk(&m,&h);
    assert(!strcmp(label,"No symbol context"));
    g_free(label);
    const char *module = "module Top;\n`define FAKE endmodule\nwire \\endmodule ;\nmodule Child; endmodule\nfunction integer value;\nvalue = 1;\nendfunction\nendmodule\n";
    size_t end = 0;
    assert(diff_symbol_verilog(module,strlen(module),0,"Top",&end));
    assert(end == strlen(module)-1);
    const char *task = strstr(module,"function integer");
    assert(diff_symbol_verilog(module,strlen(module),task-module,"value",&end));
    assert(!strncmp(module+end-11,"endfunction",11));
    assert(!diff_symbol_verilog("module X;\n",10,0,"X",&end));
    const char *same_line = "module A; endmodule module B; endmodule";
    assert(!diff_symbol_verilog(same_line,strlen(same_line),0,"B",&end));
    assert(diff_symbol_line_end("}    \r\nnext", "}    \r\nnext",11,1) == 7);
    assert(diff_symbol_line_end("} int b;\n", "} int b;\n",9,1) == 1);
    return 0;
}
'''


class SnapshotSymbolContext(unittest.TestCase):
    # EN: Compile real context and lexical helpers for self and assert safe fallback and Unicode labels.
    # RU: Компилирует настоящие обработчики для self и проверяет безопасный отказ и подписи Unicode.
    def test_context_and_verilog_extents(self):
        library = ctypes.util.find_library('glib-2.0')
        if not shutil.which('cc') or not library:
            self.skipTest('Needs cc and GLib runtime.')
        helpers = ''.join(function('src/insight-diff/symbol-context.inc', name)
                          for name in ('diff_symbols_owner', 'diff_symbols_hunk'))
        helpers += function('src/insight-diff/symbol-navigation.inc', 'diff_symbol_matches')
        helpers += ''.join(function('src/insight-diff-symbols.c', name)
                           for name in ('diff_symbol_word', 'diff_symbol_token', 'diff_symbol_named',
                                        'diff_symbol_line_end', 'diff_symbol_verilog'))
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / 'context.c'
            source.write_text(STATE + helpers + MAIN)
            binary = Path(directory) / 'context'
            subprocess.run(['cc', '-Wall', '-Wextra', '-Werror', str(source), '-o', str(binary),
                            '-l:' + library], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
