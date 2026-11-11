#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise production HDL tree builders against real GTK stores without a display.
# RU: Проверяет производственные построители деревьев HDL на GTK-моделях без дисплея.
import ctypes.util
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


# EN: Read the complete named C function from path, retaining its production body.
# RU: Читает целиком функцию name из path, сохраняя её производственный код.
def function(path, name):
    source = (ROOT / path).read_text()
    position = source.index(name + '(')
    start = source.rfind('\nstatic ', 0, position) + 1
    end = source.index('\n}', position) + 2
    return source[start:end]


class HdlTrees(unittest.TestCase):
    # EN: Build and execute the production model helpers with GTK's actual store implementation.
    # RU: Собирает и выполняет построители моделей с реальной реализацией GtkTreeStore.
    def test_signal_and_module_instance_models(self):
        libraries = [ctypes.util.find_library(name) for name in ('gtk-3', 'gobject-2.0', 'glib-2.0', 'gio-2.0')]
        if os.name != 'posix' or not shutil.which('cc') or not all(libraries):
            self.skipTest('Native test needs a C compiler and GTK 3 runtime libraries.')
        helpers = [
            ('src/insight-waveforms/debugger/signal-snapshots.inc', 'signal_instance_insert'),
        ]
        helpers += [('src/insight-workbench/hdl/hdl-instance-tree.inc', name) for name in (
            'hdl_instance_key', 'hdl_instance_definition', 'hdl_instance_under_root',
            'hdl_instance_ensure', 'hdl_instance_fill')]
        helpers += [('src/insight-waveforms/runtime/observer-lifecycle.inc', name) for name in (
            'simulation_stop_clicked', 'simulation_restart_clicked')]
        declarations = r"""
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "insight/core/index.h"
typedef char gchar;
typedef unsigned int guint;
typedef unsigned long gulong;
typedef size_t gsize;
typedef int gboolean;
typedef void *gpointer;
typedef struct GHashTable GHashTable;
typedef struct GtkTreeStore GtkTreeStore;
typedef GtkTreeStore GtkTreeModel;
typedef struct {int stamp; void *a, *b, *c;} GtkTreeIter;
typedef struct {void **pdata; guint len;} GPtrArray;
#define TRUE 1
#define FALSE 0
#define G_TYPE_STRING ((unsigned long)64)
#define G_TYPE_ULONG ((unsigned long)36)
#define insight_tr(text) (text)
extern void *g_malloc(size_t);
extern void *g_malloc0(size_t);
extern void g_free(void *);
extern char *g_strdup(const char *);
extern char *g_strndup(const char *, size_t);
extern char *g_strdup_printf(const char *, ...);
extern unsigned long long g_ascii_strtoull(const char *, char **, unsigned);
extern int g_strcmp0(const char *, const char *);
extern guint g_str_hash(const void *);
extern int g_str_equal(const void *, const void *);
extern GHashTable *g_hash_table_new(guint (*)(const void *), int (*)(const void *,const void *));
extern GHashTable *g_hash_table_new_full(guint (*)(const void *), int (*)(const void *,const void *),
                                       void (*)(void *), void (*)(void *));
extern void *g_hash_table_lookup(GHashTable *, const void *);
extern int g_hash_table_insert(GHashTable *, void *, void *);
extern int g_hash_table_replace(GHashTable *, void *, void *);
extern int g_hash_table_add(GHashTable *, void *);
extern int g_hash_table_contains(GHashTable *, const void *);
extern guint g_hash_table_size(GHashTable *);
extern void g_hash_table_unref(GHashTable *);
extern GPtrArray *g_ptr_array_new(void);
extern void g_ptr_array_add(GPtrArray *, void *);
extern void g_ptr_array_unref(GPtrArray *);
extern GtkTreeStore *gtk_tree_store_new(int, ...);
extern void gtk_tree_store_insert_with_values(GtkTreeStore *, GtkTreeIter *, GtkTreeIter *, int, ...);
extern void gtk_tree_store_set(GtkTreeStore *, GtkTreeIter *, ...);
extern void gtk_tree_store_clear(GtkTreeStore *);
extern void gtk_tree_model_get(GtkTreeModel *, GtkTreeIter *, ...);
extern int gtk_tree_model_iter_n_children(GtkTreeModel *, GtkTreeIter *);
extern int gtk_tree_model_iter_nth_child(GtkTreeModel *, GtkTreeIter *, GtkTreeIter *, int);
extern void g_object_unref(void *);
#define g_new(type, count) ((type *)g_malloc(sizeof(type) * (count)))
#define g_steal_pointer(pointer) steal((void **)(pointer))
static void *steal(void **pointer) {
    void *value = *pointer;
    *pointer = NULL;
    return value;
}
typedef struct {int sensitive; const char *text;} GtkWidget;
typedef GtkWidget GtkButton;
typedef struct GCancellable GCancellable;
extern GCancellable *g_cancellable_new(void);
extern void g_cancellable_cancel(GCancellable *);
extern void g_cancellable_reset(GCancellable *);
extern int g_cancellable_is_cancelled(GCancellable *);
typedef struct {
    GtkWidget *stop_button, *restart_button, *pause_reason;
    GCancellable *cancel;
    gboolean worker_finished, stop_requested, restart_requested;
} WaveSimulation;
static unsigned restart_count;
#define GTK_LABEL(widget) (widget)
#define gtk_widget_set_sensitive fake_sensitive
#define gtk_label_set_text fake_label
static void fake_sensitive(GtkWidget *widget, int sensitive) {
    widget->sensitive = sensitive;
}
static void fake_label(GtkWidget *widget, const char *text) {
    widget->text = text;
}
static void simulation_restart_now(WaveSimulation *run) {
    assert(run->worker_finished);
    restart_count++;
}
struct InsightIndex {const InsightSymbol *rows; size_t count;};
size_t insight_index_count(const InsightIndex *index) {
    return index->count;
}
const InsightSymbol *insight_index_at(const InsightIndex *index, size_t n) {
    return &index->rows[n];
}
"""
        fixture = r"""
static GtkTreeIter child(GtkTreeStore *store, GtkTreeIter *parent, int position) {
    GtkTreeIter result;
    assert(gtk_tree_model_iter_nth_child(store, &result, parent, position));
    return result;
}
static void text_is(GtkTreeStore *store, GtkTreeIter *row, int column, const char *expected) {
    char *actual=NULL;
    gtk_tree_model_get(store,row,column,&actual,-1);
    assert(actual && !strcmp(actual,expected));
    g_free(actual);
}
int main(void) {
    GtkWidget stop = {TRUE, NULL};
    GtkWidget restart = {TRUE, NULL};
    GtkWidget reason = {TRUE, NULL};
    WaveSimulation session = {
        .stop_button = &stop,
        .restart_button = &restart,
        .pause_reason = &reason,
        .cancel = g_cancellable_new()
    };
    simulation_restart_clicked(NULL, &session);
    assert(session.restart_requested && session.stop_requested);
    assert(g_cancellable_is_cancelled(session.cancel));
    assert(!stop.sensitive && !restart.sensitive);
    assert(restart_count == 0);
    session.worker_finished = TRUE;
    g_cancellable_reset(session.cancel);
    simulation_restart_clicked(NULL, &session);
    assert(restart_count == 1);
    assert(!g_cancellable_is_cancelled(session.cancel));
    g_object_unref(session.cancel);
    GtkTreeStore *signals=gtk_tree_store_new(7,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,
                                            G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);
    GHashTable *scopes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,g_free);
    char *a[]={"R","tb.dut.cpu0.state","cpu.v","01"};
    char *b[]={"R","tb.dut.cpu1.state","cpu.v","10"};
    signal_instance_insert(signals,scopes,a,"1",TRUE);
    signal_instance_insert(signals,scopes,b,"2",FALSE);
    assert(gtk_tree_model_iter_n_children(signals,NULL)==1);
    GtkTreeIter tb=child(signals,NULL,0),dut=child(signals,&tb,0);
    assert(gtk_tree_model_iter_n_children(signals,&dut)==2);
    GtkTreeIter cpu=child(signals,&dut,0),state=child(signals,&cpu,0);
    text_is(signals,&cpu,5,"cpu0");
    text_is(signals,&state,0,"tb.dut.cpu0.state");
    text_is(signals,&state,2,"01");
    text_is(signals,&state,3,"Yes");
    cpu = child(signals, &dut, 1);
    state = child(signals, &cpu, 0);
    text_is(signals,&state,0,"tb.dut.cpu1.state");
    text_is(signals,&state,3,"");
    g_hash_table_unref(scopes);
    g_object_unref(signals);
    const InsightSymbol rows[]={
        {"Top","top.v","Top","@hdldef",1,"1","Module",""},
        {"Cpu","cpu.v","Cpu","@hdldef",3,"1","Module",""},
        {"Ram","ram.v","Ram","@hdldef",1,"1","Module",""},
        {"top","top.v","top","@hdldef",1,"1","Instance",""},
        {"cpu0","top.v","top.cpu0","@hdldef",10,"1","Instance",""},
        {"cpu1","top.v","top.cpu1","@hdldef",11,"1","Instance",""},
        {"cpu0","top.v","cpu.v","@hdlref",10,"1","3:1","instance"},
        {"cpu1","top.v","cpu.v","@hdlref",11,"1","3:1","instance"},
        {"ram","cpu.v","top.cpu0.g[0].ram","@hdldef",8,"1","Instance",""},
        {"ram","cpu.v","top.cpu1.g[0].ram","@hdldef",8,"1","Instance",""},
        {"ram","cpu.v","ram.v","@hdlref",8,"1","1:1","instance"}
    };
    const InsightIndex index={rows,sizeof rows/sizeof *rows};
    GtkTreeStore *modules=gtk_tree_store_new(4,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_ULONG);
    const char *status=hdl_instance_fill(modules,&index,"cpu.v",5);
    assert(strstr(status,"Double-click"));
    assert(gtk_tree_model_iter_n_children(modules,NULL)==2);
    for (int n = 0; n < 2; n++) {
        GtkTreeIter root=child(modules,NULL,n),group=child(modules,&root,0),ram=child(modules,&group,0);
        text_is(modules,&root,1,"Cpu");
        text_is(modules,&root,2,"cpu.v");
        text_is(modules,&group,0,"g[0]");
        text_is(modules,&ram,1,"Ram");
        text_is(modules,&ram,2,"ram.v");
        unsigned long line=0;
        gtk_tree_model_get(modules, &root, 3, &line, -1);
        assert(line == 3);
    }
    hdl_instance_fill(modules,&index,"top.v",5);
    assert(gtk_tree_model_iter_n_children(modules,NULL)==1);
    GtkTreeIter root=child(modules,NULL,0);
    text_is(modules,&root,1,"Top");
    assert(gtk_tree_model_iter_n_children(modules,&root)==2);
    status=hdl_instance_fill(modules,&index,"missing.v",1);
    assert(strstr(status,"cursor"));
    assert(gtk_tree_model_iter_n_children(modules,NULL)==0);
    g_object_unref(modules);
    return 0;
}
"""
        source = declarations + '\n'.join(function(path, name) for path, name in helpers) + fixture
        with tempfile.TemporaryDirectory(prefix='hdl-models-') as directory:
            path = Path(directory) / 'test.c'
            path.write_text(source)
            executable = Path(directory) / 'test'
            command = ['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', str(ROOT),
                       str(path), '-o', str(executable)]
            command.extend('-l:' + library for library in libraries)
            subprocess.check_call(command)
            subprocess.check_call([str(executable)])


if __name__ == '__main__':
    unittest.main()
