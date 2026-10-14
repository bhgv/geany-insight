# Geany Insight tutorials

Each tutorial gives a command path and expected result. Begin with a small project copy so you can edit freely and check Undo. Menu names below use English; Options → Interface language also selects Russian.

## 1. Find a definition without losing your place

1. Open a project and wait for indexing.
2. Place the main-editor caret on a function call. Context shows its known definition.
3. Single-click another underlined name in Context. The main editor stays put; Context follows the definition.
4. Double-click the desired Context line. The main editor opens that file and centers the line.
5. Context back revisits the previous preview; F12 performs normal known-definition navigation.

If no definition appears, check project membership, language compatibility and Reindex workspace. The lexical index cannot resolve every reference.

## 2. Collect important lines

1. Double-click a line number: a blue circle marks the bookmark.
2. Add another bookmark in another file.
3. Click the bookmarked-book toolbar button. Both entries and line text appear in a nonmodal window.
4. Select an entry to open its file and line.
5. Double-click that line number again to remove the bookmark.

## 3. File search, replacement and Undo

1. Ctrl+F opens current-document search. Enter a name, find the next match and close it.
2. Ctrl+H opens replacement. Enter old/new text and check case/Regex options.
3. Replace one match first and inspect the result.
4. Ctrl+Z undoes the editor change; Ctrl+Y restores it with standard Geany settings.
5. Search / Replace → Quick find in file… opens the compact row; Escape closes it.

Use menus or toolbar buttons if your Geany shortcuts were reassigned.

## 4. Project or folder search

1. Click Search project / folder / open files or Search / Replace → Search project….
2. Enter a query; retain Project files or choose Open documents.
3. Search folder… selects an external folder. `*.c;*.h` restricts inputs to C sources/headers.
4. Search starts the operation. Selection previews Context; Enter opens the hit.
5. Alt+Down previews the next hit without editor navigation. Stop ends long searches.
6. Close Search Results with its cross; another search command reopens it.

Folder search skips hidden directories, node_modules, __pycache__ and files above 2 MiB. Language profiles also restrict allowed extensions and bindings.

## 5. Inspect a symbol

1. Place the caret on a name and choose Navigate → Symbol Info….
2. Inspect type, scope, file and code; switch the candidate selector when necessary.
3. Leave file open loads a closed file while retaining the active editor.
4. References switches to relationships and project name search.
5. In Relations, switch List to Graph. Selecting a node previews Context; double click opens the editor.
6. Adjust graph zoom/direction. With graph focus, D/C/R queries definitions, calls and references for the selected symbol.

## 6. Merge two files

1. Open Tools → Diff / Merge… or its two-column toolbar button.
2. Select two versions of a small text file.
3. Select a difference and take the desired side into the result.
4. Edit manually if neither side fits entirely.
5. Use Undo/Redo inside the merge window.
6. Save under a separate name and inspect the result in the editor.

This is two-way merge; the internal tool does not yet provide automatic base-aware three-way merge.

## 7. Write a command

Follow the [first Lua extension](scripting.md). It wraps a selection, appears in Scripts → Commands and uses Ctrl+Alt+W. Undo reverses edits. See [Lua API](lua-api.md).

## 8. Create help with media

1. Create UTF-8 `notes.md` with `# My notes`, a link and `![Diagram](scheme.png)`.
2. Put `scheme.png` beside it.
3. Help → Markdown / reStructuredText viewer… opens the file.
4. The same image syntax plays GIF animation in the compact view.
5. Use `![Demonstration](demo.webm)` and Multimedia view for browser video controls.
6. Edit/save the disk file, then Reload document in the viewer.

[Markup syntax and multimedia](markup-viewer.rst).

## 9. Preview an unsaved page

1. Create Markdown/RST with several headings.
2. Click the page button or Help → Preview current Markdown / RST…. Unsaved text appears in a separate window.
3. Choose a heading in the contents selector. Ctrl+F searches; F3/Shift+F3 cycles matches; Escape closes search.
4. Edit the buffer and Reload document. Save beside relative images for predictable paths; GIF remains animated.
5. Multimedia view opens this snapshot with browser video controls.

## 10. Configure a language

Right-click the editor → File language / profile… or Options → Language profiles…. Set a profile name, base parser, own/compatible extensions and optional forced binding. Fonts and CSS configures keyword/comment fonts and colors. Apply saves and reindexes; Reset file binding restores automatic detection. See [profiles](languages.md).

For a code template example and its variables, see [Bookmarks and code templates](context-bookmarks.md#code-templates).
