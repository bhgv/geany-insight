/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright (c) 2026 bhgv
 * EN: Simulation replacement offers for missing modules.
 * RU: Предложения симуляционных замен отсутствующих модулей.
 */

/* EN: Recognize common clock-wizard port names and build a draft rule from pins.
 * RU: Распознаёт типовые порты генератора частоты и собирает черновик правила из pins. */
static gchar *simulation_pll_suggestion(const gchar *module, const gchar *pins)
{
    GRegex *named = g_regex_new("\\.([A-Za-z_][A-Za-z0-9_$]*)\\s*\\(", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(named, pins, 0, &match);
    gchar *input = NULL, *reset = NULL, *locked = NULL;
    GPtrArray *outputs = g_ptr_array_new_with_free_func(g_free);
    while (g_match_info_matches(match))
    {
        gchar *pin = g_match_info_fetch(match, 1);
        if ((!strcmp(pin, "clk_in1") || !strcmp(pin, "clk_in") || !strcmp(pin, "inclk0")) && !input)
            input = g_strdup(pin);
        else if ((!strcmp(pin, "reset") || !strcmp(pin, "rst") || !strcmp(pin, "areset")) && !reset)
            reset = g_strdup(pin);
        else if (!strcmp(pin, "locked") && !locked)
            locked = g_strdup(pin);
        else if (g_regex_match_simple("^(clk_out[0-9]*|clk_o[0-9]+|c[0-9]+)$", pin, 0, 0))
            g_ptr_array_add(outputs, g_strdup(pin));
        g_free(pin);
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(named);

    gchar *suggestion = NULL;
    if (input && outputs->len && outputs->len <= 5)
    {
        GString *rule = g_string_new(NULL);
        g_string_append_printf(rule, "pll-multi %s %s %s %s 4", module, input,
                               reset ? reset : "-", locked ? locked : "-");
        for (guint i = 0; i < 5; ++i)
            g_string_append_printf(rule, " %s - - -", i < outputs->len ? outputs->pdata[i] : "-");
        suggestion = g_string_free(rule, FALSE);
    }
    g_ptr_array_unref(outputs);
    g_free(input);
    g_free(reset);
    g_free(locked);
    return suggestion;
}

/* EN: Offer PLL and HDMI replacement drafts from active HDL in source, using browser for the wizard.
 * RU: Предлагает черновики замен PLL и HDMI из активного HDL source и открывает мастер через browser. */
static void simulation_replacement_offers(const gchar *source, const gchar *directory,
                                  GtkBox *box, PortBrowser *browser)
{
    if (!source)
        return;
    GRegex *comments = g_regex_new("//[^\\n]*|/\\*[\\s\\S]*?\\*/|\"(?:\\\\.|[^\"\\\\])*\"", 0, 0, NULL);
    gchar *clean = g_regex_replace_literal(comments, source, -1, 0, " ", 0, NULL);
    g_regex_unref(comments);
    GPtrArray *defined = simulation_recipe_modules(clean);
    GHashTable *offered = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    GRegex *instances = g_regex_new(
        "\\b(OBUFDS|clk_wiz_[A-Za-z0-9_$]+|pll[A-Za-z0-9_$]*|[A-Za-z_][A-Za-z0-9_$]*_pll[A-Za-z0-9_$]*)"
        "\\s+[A-Za-z_][A-Za-z0-9_$]*\\s*\\(([^;]*)\\)\\s*;", 0, 0, NULL);
    GMatchInfo *match;
    g_regex_match(instances, clean, 0, &match);
    while (g_match_info_matches(match) && g_hash_table_size(offered) < 16)
    {
        gchar *module = g_match_info_fetch(match, 1);
        gboolean exists = g_hash_table_contains(offered, module);
        for (guint i = 0; i < defined->len; ++i)
            exists |= !strcmp(module, defined->pdata[i]);
        if (!exists)
        {
            gchar *pins = g_match_info_fetch(match, 2);
            gboolean differential = !strcmp(module, "OBUFDS");
            gchar *suggestion = differential ? g_strdup("obufds-model OBUFDS")
                                              : simulation_pll_suggestion(module, pins);
            g_free(pins);
            if (suggestion)
            {
                gchar *caption = g_strdup_printf(differential ? insight_tr("Configure TMDS output primitive %s…")
                                                              : insight_tr("Configure missing PLL %s…"), module);
                GtkWidget *button = gtk_button_new_with_label(caption);
                g_free(caption);
                g_object_set_data_full(G_OBJECT(button), "replacement-suggestion", suggestion, g_free);
                g_object_set_data(G_OBJECT(button), "source-directory", (gpointer)directory);
                g_signal_connect(button, "clicked", G_CALLBACK(simulation_device_wizard), browser);
                gtk_box_pack_start(box, button, FALSE, FALSE, 0);
                g_hash_table_add(offered, g_strdup(module));
            }
        }
        g_free(module);
        g_match_info_next(match, NULL);
    }
    g_match_info_free(match);
    g_regex_unref(instances);
    g_hash_table_unref(offered);
    g_ptr_array_unref(defined);
    g_free(clean);
}
