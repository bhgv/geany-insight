# Geany Insight user guide

Geany Insight integrates source navigation, project panels, Lua automation and internal comparison into the native Geany editor. Open this guide from **Help → Geany Insight — Documentation…** or the book button on the main toolbar. The adjacent book button opens [tutorials](tutorials.md). Buttons show icons; hover for their names.

## Where to find commands

**Search / Replace** contains file search and replacement, project/folder/open-document searches and result navigation. Text search panels are hidden at startup. Open them with a command or toolbar button; close them with the cross. Escape in a quick-search field closes its panel and returns focus to the editor.

**Options** contains preferences, document settings, language profiles and the interface-language selector. **View → Panels** controls panels. **Window → Window List…** shows open buffers. **Tools → Diff / Merge…** opens internal comparison. **Scripts** contains Lua extensions and the command palette. **Help** opens the documentation, tutorials, scripting guide and markup viewer.

Choose **Options → Interface language → Automatic (system locale) / Русский / English**. Automatic selects Russian for a Russian system locale and English otherwise. The choice is saved; captions and bundled help follow it. Source text, paths and script identifiers remain unchanged.

## Project and symbol panels

**Project Window** groups Project Symbols, files, folders, file types, categories, bookmarks and clips. Choose a source workspace or open a Geany project. Reindex workspace refreshes the index; indexing publishes results progressively. Find project symbol… filters names and scopes. Find workspace file… filters paths.

**File Symbols** lists symbols of the current buffer, including unsaved edits after the parser updates. Select a symbol to preview it; activate it to navigate in the main editor. Project membership and exclusions are configured through Project → Add and Remove Project Files…. Removing an entry does not delete the disk file.

## Context

**Definitions:** lists indexed candidates for the selected name. Only code identifiers with a compatible, in-scope definition become underlined links. Keywords, unknown identifiers, comments and strings remain ordinary text. Links update as definitions are indexed. Default C/C++ profiles include C/C++ source and headers and ASM/S compatibility, excluding Shell and VHDL. [Language profiles](languages.md) let you change these rules.

Single click follows a link **inside Context only**. Double click in Context opens that file and line in the main editor and centers the location vertically. Context back/forward revisits preview locations. F12 follows a known definition.

Follow cursor updates Context from the editor. Disable it to inspect a preview independently. Open in editor or Enter opens the preview caret line. Context search searches only the preview. If the source changed, refresh before navigating to a saved preview location.

## Bookmarks and clips

Double click an editor line number to toggle its bookmark. A blue circle appears to the left. The bookmarked-book button opens the shared, nonmodal searchable list: `filename:line> text`. Selecting a row opens that line in the editor. Markers follow inserted/deleted lines; named bookmarks retain source anchors.

Clips store small reusable selections. Copy selection / Cut selection creates a clip; Paste inserts it into the current writable buffer. Edit/Rename/Delete manage entries. Import/Export transfers the note store. [Context and bookmarks](context-bookmarks.md).

## Search and replace

**Ctrl+F** or the search button opens Geany's file-search dialog. **Ctrl+H** or the replacement button opens its replace dialog. Match case, whole-word and regular-expression options are available. Quick find in file… opens the compact row with Previous/Next, Match case, Whole word and Regex.

**Search project…** opens the query and Search Results. Select Project files / Open documents / Current document / Chosen files or folder. Search folder… and Search selected files… first choose inputs. Masks such as `*.c;*.h` restrict files. Search starts the operation; Stop retains partial results. Configured language profiles further restrict project/reference text searches to their own, compatible and explicitly bound files.

Keywords / Boolean supports implicit AND, OR, NOT/!, parentheses, `=case` and `?"regex"`. Word variations handles common English endings, not dictionary morphology. Use ordinary literal search when these modes are unnecessary.

Selecting a result previews it. Alt+Up/Down previews adjacent hits; Enter or double click opens the exact hit. Changed sources require another search. Result history keeps the last 32 valid queries; choosing one retains scope and options.

## Relations, Symbol Info and structural navigation

**Relation Window** provides references, definitions/declarations, lexical calls and call sites, members, base and derived types. Its filter, query history and optional graph help inspect results. These are indexed/lexical relationships, not a complete compiler model. [Calls and relations](relations-calls.md).

**Symbol Info…** accepts a name or qualified owner, displays candidates, signature, type, scope, inheritance and source. Preview is independent; Jump/Ctrl+Enter activates the source. [Symbol Info](symbol-info.md).

Edit → Structural selection expands to a block (**Ctrl+Alt+B**), selects matching brackets (**Ctrl+Alt+M**), a line (**Ctrl+Alt+L**) or a function (**Ctrl+Alt+F**). Navigate → Function Up/Down moves between functions (Ctrl+Alt+Page Up/Down). Supported languages and incomplete/large-source limits apply.

## Panels and layouts

View → Panels toggles File Symbols, Project Window and Relations and focuses panels. Layouts A/B/C/D load arrangements; Save current layout saves the selected preset. Float Project Window detaches the project; Dock Project Window restores it. Overview shows a file miniature and moves the visible region. The editor and Project sides can shrink independently. A restored Project divider beyond the available width is recovered once during initial layout so the Project panel stays reachable; valid positions, later manual resizing and explicitly selected layouts are preserved.

Window → Window List… filters open buffers by name, path and nearest known symbol. Selection previews Context; Activate/Enter switches editors. Save and Close file use normal Geany dialogs and respect cancellation. [Window List](window-list.md).

## Internal Diff / Merge

Click the two-column arrow icon or Tools → Diff / Merge…. Choose Left and Right files. Inspect changed blocks, take either side into the editable result, edit manually and save. Undo/Redo belongs to the merge result. External KDiff3 is an optional separate command; internal comparison works without it. [Capabilities and limits](internal-diff.md).

## Automation

The [scripting guide](scripting.md) provides a first extension, loading, shortcuts and events. [Lua API](lua-api.md) describes methods, coordinates and limits. Scripts can access unsaved buffer text.

## Help formats and media

Help is UTF-8 Markdown and reStructuredText. The compact viewer renders headings, emphasis, code, links, tables and local images including animated GIF. The top contents selector jumps to headings; Ctrl+F searches the page, F3/Shift+F3 cycles matches. Help → Preview current Markdown / RST… and the page button preview unsaved text; Reload refreshes it. Multimedia view opens a browser page with images and HTML5 video; playback depends on codecs. [Syntax and limits](markup-viewer.rst).

## Builds and language settings

Check a downloaded archive's REVISION.txt. A commit does not update an already unpacked build. [Running, checking and building](desktop-check-builds.md).

Options → Language profiles… and editor right click → File language / profile… configure recognition, compatible extensions, words, colors and fonts. [Profiles and CSS](languages.md).


Full reference: [Search and replacement modes and query languages](search-replace.md).

## Directory Compare

[Сравнение каталогов / directory comparison](directory-compare.md): Tools → Directory Compare…

[Window layouts and Tab Tray](window-layouts.md) — panel controls, saved arrangements and layout files.

[Git history and read-only colored diff / История Git и цветной дифф](git-history.md) — File → Git History and Diff…

[Project Report / Отчёт по проекту и экспорт списка файлов](project-report.md) — Project → Project Report… / Export Project File List…

[Styled HTML export / Экспорт кода в HTML с оформлением](html-export.md) — File → Export File as HTML… / Export Selection as HTML…


[Console AI assistant: configuration, diagnostics and Diff/Merge review](console-ai.md).


[Editor and project AI: cumulative requests, variants, manual Result and refresh](editor-ai.md).


[Replace Files: preview, individual choices and guarded multi-buffer replacement](replace-files.md).

Replace Files also offers modal Replace/Skip/Remaining/Cancel review, F3/Shift+F3 preview navigation and Open source in editor. Select next 128 files continues the remaining snapshot plan while keeping applied files excluded.

[Renumber: columns and multiple selections](renumber.md) — one source-order sequence, colored before/after preview, per-fragment selection and one Undo/Redo.

[Copy Text to Buffer](file-text.md#copy-text-to-buffer) — selected/whole text, four destination modes, colored preview, explicit reload and one Undo without disk writes.

[Symbol editing](structural-selection.md#paste-symbol-review-and-guarded-editing) — Copy/Cut/Duplicate definitions and Paste Symbol before a selected definition with guarded colored preview.

[Search Web / Поиск в интернете](web-search.md)

[Shared AI Connections / Общие подключения ИИ](ai-connections.md) — shared profiles, optional key storage and parallel answer selection supersede the earlier single-connection setup.

- [Panel activation / Активация панелей](panel-activation.md)

- [Code outlining / Сворачивание кода](code-outlining.md)

- [Interface style / Стиль интерфейса](interface-style.md)
