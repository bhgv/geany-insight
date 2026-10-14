# Context and bookmarks

Single click on a symbol in Context follows its definition inside Context only. Double click on a code line opens the currently previewed file at that line in the editor and centers the caret vertically. Single-click navigation waits for the system double-click interval so it cannot redirect the double click.

Double click on an editor line number toggles its bookmark. A blue circle appears to the left of the number. The book icon in the toolbar opens a nonmodal searchable list: `filename:line> text`. Selecting a row opens that line in the editor. Scintilla markers follow inserted and deleted lines; named bookmarks are persisted with source anchors.

Open internal comparison from Tools → Diff / Merge…. External KDiff3 is optional and only runs when its button is clicked.

## File overview and change navigation

The miniature in **View → Overview** shows the file beside the editor.
Blue dots on its left identify bookmarks. Orange bars on its right identify
unsaved changes; green bars identify changes saved during the current file session.
Closing and reopening the file starts a fresh change history. Deleted text is
marked on the surviving line beside the deletion. Undo/Redo updates the marks,
and bookmarks move with the text.

**Edit → Next unsaved change** (`Alt+Num +`) and **Previous unsaved change**
(`Alt+Num -`) move between separate blocks of modified lines. These shortcuts use
the numeric keypad. The commands are also available by right-clicking the miniature.
Navigation skips the current block, does not wrap around, leaves the text unchanged,
and excludes saved changes. It supports files up to 2 MiB.

Right-click → **Overview Options…** controls bookmarks, change marks, saved changes
and hover messages. Hover messages show the line number, source text, change state
and bookmark state. These options persist across restarts. Only marks within the
visible miniature are drawn. Marks in the main editor's left margin have their own
setting.

### Function boundaries in the overview

**Overview Options…** provides **Show current function boundary** and
**Show all function boundaries**. The function containing the entire caret or
selection is outlined in purple; other functions use grey outlines. The current
outline disappears outside a function body. Hover messages include the function
name and its scope when supplied by the parser.

Outlines combine parser definitions with lexical body matching. C, C++, C#, Java,
JavaScript and TypeScript brace-bodied definitions are supported. Prototypes,
incomplete bodies and expression-only definitions receive no outline. Braces in
comments and strings are excluded. These are lexical boundaries, without semantic
handling of templates or preprocessing.

Computation runs in the background for files up to 2 MiB. Editing removes old
outlines until the symbols are refreshed. Further edits or closing the file reject
obsolete results. Both display options persist across restarts.
