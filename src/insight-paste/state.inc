/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Guard document destinations and load shared persistent paste preferences.
 * RU: Проверяет назначения документов и загружает общие постоянные настройки вставки. */

/* EN: Find the live document owning sci, or NULL for non-document/destroyed widgets.
 * RU: Находит действующий документ sci либо NULL для постороннего/закрытого виджета. */
static GeanyDocument *document_for_sci(ScintillaObject *sci)
{
    for (guint i = 0; documents_array && i < documents_array->len; i++)
    {
        GeanyDocument *doc = g_ptr_array_index(documents_array, i);
        if (doc->is_valid && doc->editor->sci == sci)
            return doc;
    }
    return NULL;
}

/* EN: Get sci-owned edit generation, creating it if absent; no source changes occur.
 * RU: Возвращает счётчик правок во владении sci, создавая при отсутствии; исходник не меняется. */
static State *state(ScintillaObject *sci)
{
    State *value = g_object_get_data(G_OBJECT(sci), STATE);
    if (!value)
    {
        value = g_new0(State, 1);
        g_object_set_data_full(G_OBJECT(sci), STATE, value, g_free);
    }
    return value;
}

/* EN: Count actual text edits reported by nt for editor, including Undo/Redo, to invalidate pending clipboard requests.
 * RU: Считает правки nt для editor, включая Undo/Redo, чтобы отклонять отложенную вставку. */
void insight_paste_notify(GeanyEditor *editor, SCNotification *nt)
{
    if (nt->nmhdr.code == SCN_MODIFIED &&
        (nt->modificationType & (SC_MOD_INSERTTEXT | SC_MOD_DELETETEXT)))
        state(editor->sci)->generation++;
}

/* EN: Return the owned per-computer paste preference filename.
 * RU: Возвращает собственный путь настроек вставки на этом компьютере. */
static gchar *config_path(void)
{
    return g_build_filename(app->configdir, "insight-paste.conf", NULL);
}

/* EN: Load legacy and new paste options once, retaining safe defaults for missing/invalid values.
 * RU: Однократно загружает прежние и новые настройки, сохраняя безопасные значения при отсутствии/ошибках. */
static void load(void)
{
    if (loaded) return;
    loaded = TRUE;
    GKeyFile *key = g_key_file_new();
    gchar *path = config_path();
    if (g_key_file_load_from_file(key, path, 0, NULL))
    {
        enabled = g_key_file_get_boolean(key, "typing", "intelligent-paste", NULL);
        always_select = g_key_file_get_boolean(key, "typing", "always-select-pasted-text", NULL);
        gint position = g_key_file_get_integer(key, "typing", "paste-position", NULL);
        if (position >= 0 && position < PASTE_POSITION_COUNT)
            paste_position = position;
    }
    g_key_file_unref(key);
    g_free(path);
}

/* EN: Permit guarded plain insertion into one writable UTF-8 document selection, independently of intelligent paste.
 * RU: Разрешает защищённую обычную вставку в одно доступное выделение UTF-8 независимо от умной вставки. */
static gboolean plain_allowed(ScintillaObject *sci)
{
    load();
    GeanyDocument *doc = document_for_sci(sci);
    return doc && doc->is_valid && !doc->readonly && !SEND(sci, SCI_GETREADONLY, 0, 0) &&
        SEND(sci, SCI_GETCODEPAGE, 0, 0) == SC_CP_UTF8 && SEND(sci, SCI_GETSELECTIONS, 0, 0) == 1 &&
        sci_get_selection_mode(sci) == SC_SEL_STREAM &&
        !SEND(sci, SCI_GETSELECTIONNCARETVIRTUALSPACE, 0, 0) &&
        !SEND(sci, SCI_GETSELECTIONNANCHORVIRTUALSPACE, 0, 0);
}

/* EN: Return whether post-paste selection needs handling beyond the native caret-at-end default.
 * RU: Проверяет необходимость обработки после вставки сверх штатного курсора в конце. */
static gboolean position_policy(void)
{
    return always_select || paste_position != PASTE_END;
}

/* EN: Permit intelligent transformation only when globally enabled and allowed by sci's language profile.
 * RU: Разрешает умное преобразование лишь при общем включении и разрешении языкового профиля sci. */
static gboolean allowed(ScintillaObject *sci)
{
    if (!plain_allowed(sci) || !enabled) return FALSE;
    GeanyDocument *doc = document_for_sci(sci);
    const InsightFiletypeOptions *options = insight_languages_editing_options(doc->editor);
    return !options || options->allow_intelligent_paste;
}

/* EN: Permit whole-line transformation at sci's caret or a complete-line selection; partial selections stay ordinary text.
 * RU: Разрешает преобразование целых строк у курсора sci либо целого строкового выделения; частичные выделения остаются обычным текстом. */
static gboolean eligible(ScintillaObject *sci)
{
    if (!allowed(sci)) return FALSE;
    gint start = sci_get_selection_start(sci), end = sci_get_selection_end(sci);
    return start == end ||
        (start == sci_get_position_from_line(sci, sci_get_line_from_position(sci, start)) &&
         end == sci_get_position_from_line(sci, sci_get_line_from_position(sci, end)));
}
