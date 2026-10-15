# Bookmarks

Quick access to marked source lines.

## Controls

Double-click a line number to toggle its blue bookmark circle. The book button opens the list. The list supports navigation, deletion, renaming, filtering and import/export.

## Short tutorial

Mark two lines. Open the list with the book button. Click a file:line > text entry to navigate in the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [context-bookmarks.md](context-bookmarks.md).

## Moving a project

When a project/workspace root is selected, bookmark export writes files inside that root as relative paths (notes format v2). Open/select the destination root before importing into a moved copy of the project. Files outside the root keep absolute paths. Import requires a root for relative bookmarks, rejects traversal paths, and preserves existing notes by rejecting name collisions. Legacy v1 snapshots still import; ordinary saved notes and clip exports remain v1. Clips contain text/language bindings rather than source paths. Project file membership already stores project-local paths relatively.

Start a list filter with `~` for case-insensitive Unicode subsequence matching: `~srd` finds `Shared`. Ordinary queries keep literal substring matching, including spaces and punctuation. A bare `~` matches nothing.
