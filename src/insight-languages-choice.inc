/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Populate language options using effective compatibility, including includes.
 * RU: Заполняет настройки языка с действующей совместимостью подключений. */

/* EN: Populate dialog data from choice without losing effective include extensions.
 * Ignore recursive signals and locale updates; leave missing profiles unchanged.
 * RU: Заполняет диалог data из choice, сохраняя действующие расширения подключений.
 * Пропускает повторные сигналы и смену языка интерфейса; неизвестный профиль не меняет. */
static void choice_changed(GtkComboBox *choice, gpointer data)
{
    LanguageDialog *d = data;
    if (d->filling || insight_i18n_is_applying()) return;
    const gchar *name = gtk_combo_box_get_active_id(choice);
    if (!name) return;
    Language *p = by_name(name);
    GeanyFiletype *ft = p ? p->parser : filetypes_lookup_by_name(name);
    if (!ft) return;
    d->filling = TRUE;
    g_free(d->original);
    d->original = p ? g_strdup(p->name) : NULL;
    gtk_entry_set_text(GTK_ENTRY(d->name), p ? p->name : ft->name);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(d->parser), ft->name);
    gchar *extensions = p ? g_strdup(p->extensions) : default_extensions(ft);
    gtk_entry_set_text(GTK_ENTRY(d->extensions), extensions);
    g_free(extensions);
    gtk_entry_set_text(GTK_ENTRY(d->file_filters), p ? p->file_filters : "");
    InsightFiletypeOptions options;
    insight_filetype_options_default(&options);
    insight_filetype_editing_set(d->editing, p ? &p->editing : &options);
    Language fallback = {.parser = ft, .seeded = TRUE, .compatible = ""};
    gtk_entry_set_text(GTK_ENTRY(d->compatible), language_compatible_extensions(p ? p : &fallback));
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->words)), p ? p->words : "", -1);
    gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(d->css)), p ? p->css : "", -1);
    gtk_combo_box_set_active_id(GTK_COMBO_BOX(d->theme), p && *p->theme ? p->theme : "current");
    d->filling = FALSE;
}
