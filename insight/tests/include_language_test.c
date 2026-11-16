/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Verify include ownership in mixed C, C++ and HDL projects.
 * RU: Проверяет принадлежность подключений в смешанных проектах C, C++ и HDL. */
#include "include_language.h"
#include <string.h>

/* EN: Assert the inferred language for root/name, including NULL ambiguity.
 * RU: Проверяет определённый язык root/name, включая NULL при неоднозначности. */
static void language(GHashTable *result, const gchar *root, const gchar *name, const gchar *expected)
{
    gchar *path = g_build_filename(root, name, NULL);
    g_assert_cmpstr(g_hash_table_lookup(result, path), ==, expected);
    g_free(path);
}

/* EN: Resolve literal mixed-language include chains and assert safe ambiguity.
 * Return zero when all graph and comment/quote assertions pass.
 * RU: Разрешает буквальные смешанные цепочки include и проверяет неоднозначность.
 * Возвращает ноль после проверок графа, комментариев и строк. */
int main(void)
{
    gchar *root = g_build_filename(g_get_tmp_dir(), "include-ownership", NULL);
    const gchar *names[] = {"main.cpp", "main.c", "top.sv", "cpp.inc", "c.inc", "hdl.inc",
        "nested.inc", "shared.inc", "shared-nested.inc", "orphan.inc", "fake.inc",
        "a/common.inc", "b/common.inc", "ambiguous.inc", "continued.inc", "cr.inc", "quoted.inc", "hdl-nested.inc", "подключение.inc"};
    GPtrArray *paths = g_ptr_array_new_with_free_func(g_free);
    for (guint n = 0; n < G_N_ELEMENTS(names); n++)
        g_ptr_array_add(paths, g_build_filename(root, names[n], NULL));
    InsightIncludeGraph *graph = insight_include_graph_new(paths, root);
    insight_include_graph_add(graph, g_ptr_array_index(paths, 0),
        "#include \"cpp.inc\"\n#include \"shared.inc\"\n#include \"common.inc\"\n"
        "/*\n#include \"fake.inc\"\n*/\nR\"x(\n#include \"fake.inc\"\n)x\";\n"
        "#inc\\\nlude \\\r\n \"continued.inc\"\n#include \"подключение.inc\"\n");
    insight_include_graph_add(graph, g_ptr_array_index(paths, 1), "#include \"c.inc\"\r#include \"cr.inc\"\r");
    insight_include_graph_add(graph, g_ptr_array_index(paths, 2),
        "`include \"hdl.inc\"\n`include \"shared.inc\"\n");
    insight_include_graph_add(graph, g_ptr_array_index(paths, 3), "R\"raw(\n\"\n#include \"quoted.inc\"\n)raw\";\n#include <nested.inc>\n");
    insight_include_graph_add(graph, g_ptr_array_index(paths, 5),
        "localparam X=1'b0;\n`include \"hdl-nested.inc\"\n");
    insight_include_graph_add(graph, g_ptr_array_index(paths, 6), "#include \"cpp.inc\"\n");
    insight_include_graph_add(graph, g_ptr_array_index(paths, 7),
        "#include \"shared-nested.inc\"\n`include \"shared-nested.inc\"\n");
    GHashTable *result = insight_include_graph_resolve(graph);
    language(result, root, "cpp.inc", "C++");
    language(result, root, "nested.inc", "C++");
    language(result, root, "c.inc", "C");
    language(result, root, "hdl.inc", "SystemVerilog");
    language(result, root, "shared.inc", NULL);
    language(result, root, "shared-nested.inc", NULL);
    language(result, root, "orphan.inc", NULL);
    language(result, root, "fake.inc", NULL);
    language(result, root, "a/common.inc", NULL);
    language(result, root, "b/common.inc", NULL);
    language(result, root, "continued.inc", "C++");
    language(result, root, "cr.inc", "C");
    language(result, root, "quoted.inc", NULL);
    language(result, root, "hdl-nested.inc", "SystemVerilog");
    language(result, root, "подключение.inc", "C++");
    gchar *canonical = g_build_filename(root, "подключение.inc", NULL);
    gchar *upper = g_utf8_strup(canonical, -1);
#ifdef G_OS_WIN32
    g_assert_cmpstr(g_hash_table_lookup(result, upper), ==, "C++");
#else
    g_assert_null(g_hash_table_lookup(result, upper));
#endif
    g_free(upper);
    g_free(canonical);
    g_hash_table_unref(result);
    insight_include_graph_add(graph, g_ptr_array_index(paths, 0), "#include \"cr.inc\"\n");
    result = insight_include_graph_resolve(graph);
    language(result, root, "cpp.inc", NULL);
    language(result, root, "nested.inc", NULL);
    language(result, root, "continued.inc", NULL);
    language(result, root, "cr.inc", "C++");
    language(result, root, "shared.inc", "SystemVerilog");
    g_hash_table_unref(result);
    insight_include_graph_free(graph);
    g_ptr_array_unref(paths);
    g_free(root);
    g_print("Include ownership passed: C/C++/HDL, transitive cycles, shared ambiguity, duplicate basenames, comments/raw strings, line splices/CR/Unicode and replacement\n");
    return 0;
}
