# Context and bookmarks

Single click on a symbol in Context follows its definition inside Context only. Double click on a code line opens the currently previewed file at that line in the editor and centers the caret vertically. Single-click navigation waits for the system double-click interval so it cannot redirect the double click.

Double click on an editor line number toggles its bookmark. A blue circle appears to the left of the number. The book icon in the toolbar opens a nonmodal searchable list: `filename:line> text`. Selecting a row opens that line in the editor. Scintilla markers follow inserted and deleted lines; named bookmarks are persisted with source anchors.

Open internal comparison from Tools → Diff / Merge…. External KDiff3 is optional and only runs when its button is clicked.
