/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL profile compatibility and project-dependent .inc classification.
 * RU: Совместимость HDL-профилей и определение языка .inc по проекту. */
/* EN: Test path for an unambiguous HDL extension; no profile lookup or I/O.
 * RU: Проверяет однозначное HDL-расширение path без поиска профиля и ввода-вывода. */
static gboolean language_hdl_unit(const gchar *path)
{
    const gchar *dot = path ? strrchr(path, '.') : NULL;
    return dot && (!g_ascii_strcasecmp(dot, ".v") ||
        !g_ascii_strcasecmp(dot, ".sv") || !g_ascii_strcasecmp(dot, ".vh") ||
        !g_ascii_strcasecmp(dot, ".svh"));
}

/* EN: Resolve ambiguous .inc path in an HDL workspace, keeping custom profile.
 * Check live membership instead of caching the project-dependent choice.
 * RU: Определяет язык неоднозначного path .inc в HDL-проекте, сохраняя custom.
 * Проверяет текущий состав проекта, не кэшируя зависящий от проекта выбор. */
static Language *language_include_context(Language *custom, const gchar *path)
{
    const gchar *dot = path ? strrchr(path, '.') : NULL;
    if (!dot || g_ascii_strcasecmp(dot, ".inc") || (custom && !custom->seeded))
        return custom;
    GPtrArray *paths = insight_workbench_source_paths(language_hdl_unit, 1);
    gboolean hdl = paths->len > 0;
    g_ptr_array_unref(paths);
    if (!hdl) return custom;
    Language *profile = by_name("SystemVerilog");
    return profile ? profile : custom;
}

/* EN: Return compatible extensions for profile. Generated HDL profiles share
 * Verilog/SystemVerilog sources; custom profiles keep their explicit settings.
 * RU: Возвращает совместимые расширения profile. Созданные HDL-профили разделяют
 * исходники Verilog/SystemVerilog; пользовательские сохраняют явные настройки. */
static const gchar *language_compatible_extensions(const Language *profile)
{
    if (profile->seeded && profile->parser &&
        (!strcmp(profile->parser->name, "Verilog") ||
         !strcmp(profile->parser->name, "SystemVerilog")))
        return "v;vh;sv;svh;inc";
    return profile->compatible;
}
