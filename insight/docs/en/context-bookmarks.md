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
