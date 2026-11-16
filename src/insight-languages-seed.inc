/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Seed absent native/custom parser profiles after loading their configuration.
 * RU: Создаёт отсутствующие профили встроенных/добавленных парсеров после загрузки настроек. */

/* EN: Write initial profiles and styles. Existing user files win; report write
 * errors as warnings. Load lazy custom parsers before deciding eligibility.
 * RU: Создаёт начальные профили и стили. Существующие файлы имеют приоритет;
 * ошибки записи выводит как предупреждения. Сначала загружает отложенные парсеры. */
static void seed_languages(void)
{
    ScintillaObject *sci = SCINTILLA(scintilla_new());
    g_object_ref_sink(sci);
    for (guint n = 0; n < filetypes_array->len; n++)
    {
        GeanyFiletype *ft = filetypes[n];
        if (ft->lang < 0) filetypes_load_config(ft->id, FALSE);
        if (ft->lang < 0) continue;
        gchar *folder = g_build_filename(root, ft->name, NULL);
        gchar *config = g_build_filename(folder, "language.conf", NULL);
        if (g_file_test(config, G_FILE_TEST_EXISTS))
        {
            repair_seed(sci, ft);
            g_free(folder);
            g_free(config);
            continue;
        }
        Language p = {0};
        insight_filetype_options_default(&p.editing);
        p.name = ft->name;
        p.folder = folder;
        p.parser = ft;
        p.extensions = default_extensions(ft);
        p.compatible = insight_native_language_compatible(TM_PARSER_C, ft->lang) ?
            "c;c++;C;cpp;cxx;cc;h;hpp;hxx;h++;hh;asm;ASM;s;S;sx" : "";
        p.theme = "current";
        p.seeded = TRUE;
        highlighting_set_styles(sci, ft);
        p.words = (gchar *)highlighting_primary_keywords(ft);
        PangoFontDescription *font = pango_font_description_from_string(
            interface_prefs.editor_font ? interface_prefs.editor_font : "Monospace 12");
        const gchar *family = pango_font_description_get_family(font);
        gchar *escaped = font_family_escape(family ? family : "Monospace");
        gchar size[G_ASCII_DTOSTR_BUF_SIZE];
        g_ascii_dtostr(size, sizeof size, pango_font_description_get_size(font) / (double)PANGO_SCALE);
        GString *css = g_string_new(NULL);
        g_string_append_printf(css, "editor { font-family: \"%s\"; font-size: %spt; }\n", escaped, size);
        g_free(escaped);
        pango_font_description_free(font);
        gchar *rules = seed_style_rules(sci);
        g_string_append(css, rules);
        g_free(rules);
        p.css = css->str;
        GError *error = NULL;
        if (!write_language(&p, &error))
            g_warning("Cannot seed language %s: %s", ft->name, error ? error->message : "write failed");
        g_clear_error(&error);
        g_string_free(css, TRUE);
        g_free(p.extensions);
        g_free(folder);
        g_free(config);
    }
    gtk_widget_destroy(GTK_WIDGET(sci));
    g_object_unref(sci);
}
