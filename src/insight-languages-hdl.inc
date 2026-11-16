/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: HDL profile compatibility and project-dependent .inc classification.
 * RU: Совместимость HDL-профилей и определение языка .inc по проекту. */
/* EN: Resolve ambiguous .inc path from include ownership, preserving custom.
 * NULL/conflicting ownership retains the normal profile. No file I/O occurs.
 * RU: Определяет язык path .inc по владельцам подключений, сохраняя custom.
 * Неизвестный/спорный владелец сохраняет обычный профиль. Файлы не читаются. */
static Language *language_include_context(Language *custom, const gchar *path)
{
    const gchar *dot = path ? strrchr(path, '.') : NULL;
    if (!dot || g_ascii_strcasecmp(dot, ".inc") || (custom && !custom->seeded))
        return custom;
    const gchar *name = insight_workbench_include_language(path);
    Language *profile = name ? by_name(name) : NULL;
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
    if (profile->seeded && profile->parser &&
        (!strcmp(profile->parser->name, "C") || !strcmp(profile->parser->name, "C++")))
        return "c;c++;C;cpp;cxx;cc;h;hpp;hxx;h++;hh;asm;ASM;s;S;sx;inc";
    return profile->compatible;
}

/* EN: Return the generated profile's family: native=1, HDL=2, custom/other=0.
 * RU: Возвращает семейство созданного profile: C/C++=1, HDL=2, своё/прочее=0. */
static guint language_profile_family(const Language *profile)
{
    if (!profile || !profile->seeded || !profile->parser) return 0;
    const gchar *name = profile->parser->name;
    if (!strcmp(name, "Verilog") || !strcmp(name, "SystemVerilog")) return 2;
    if (!strcmp(name, "C") || !strcmp(name, "C++")) return 1;
    return 0;
}

/* EN: Test inferred language name against family; unknown ownership is allowed.
 * RU: Проверяет определённый язык name для family; неизвестная принадлежность допустима. */
static gboolean language_include_family_matches(guint family, const gchar *name)
{
    if (!family || !name) return TRUE;
    return family == 2 ? !strcmp(name, "SystemVerilog") : strcmp(name, "SystemVerilog") != 0;
}
