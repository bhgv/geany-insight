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

## Symbol-relative restoration

Bookmarks remember the nearest preceding indexed function/class (exact name,
scope and kind) plus a line offset. Reopening after external edits relocates the
mark with that declaration, even when another line has the same text at the old
number. Duplicate declarations decline relocation. If the symbol is missing,
only a unique text match is used; an old line-number coincidence cannot win.
Legacy notes remain readable. Anchor metadata survives rename, selected export,
merge and project-relative export. Capture uses current parser generations;
explicit navigation/close can refresh a bookmarked buffer up to 2 MiB. Live
markers still track ordinary editing. Arbitrary edits inside a function may
change the meaning of its saved offset; this is not semantic change tracking.

## Project store and keyboard

Each project uses `<project file>.bookmarks.conf` beside its project file, with
paths relative to the source root. With no project open, bookmarks use the global
`insight/notes.conf`; global clips in that file remain independent. This is Geany
Insight's own INI format, not Source Insight XML.

The label identifies the active store. Reload bookmarks reads external changes
without first overwriting the file. A damaged store blocks writes until repaired
and reloaded. If moving bookmarks cannot be saved while changing projects, the
old store stays in memory, is identified as unsaved and blocks editing. Export
can preserve it. Reload refuses to discard unsaved data.

Columns show name, file, containing symbol and line. Filtering also matches the
symbol name. Symbol anchors retain function/class-relative offsets across source
edits; ambiguous or missing source text does not trigger a guessed jump.

- Enter in an empty field creates A, B, …, Z, A1, B1, … using an unused name.
- Enter in the list or Go to activates the selected bookmark.
- Ctrl+N adds a bookmark or updates the entered name.
- Ctrl+X deletes the selected bookmark.
- Ctrl+O / Ctrl+S open import / export.

A project change during a modal operation prevents writing into the new store.
Failed add/delete/rename operations retain the old list and editor markers.
Source Insight XML import remains unimplemented; import/export uses the native format.

## Select, save and delete several bookmarks

Use Ctrl+click for separate rows and Shift+click for a range. Ctrl+A selects
all visible rows. Delete or Ctrl+X deletes all selected bookmarks in one saved
transaction; a write failure preserves the list, selection and editor markers.
Filtering or refreshing keeps selected names that remain visible.

**Export selected…** and Ctrl+S save only the selected rows. Nothing is exported
without a selection. Right-click or Menu/Shift+F10 opens commands with icons for
navigation, rename, batch delete, import, selected export, **Export all bookmarks…**,
Select all and Clear selection. Export all includes bookmarks hidden by the filter;
Ctrl+A selects only visible rows. Rename requires exactly one row. Enter opens the
selected cursor row when several rows are selected. Right-clicking an already
selected row preserves the group and does not navigate the editor.

Export captures the selected records before the file dialog opens, including
symbol anchors and portable paths. A project/store change while the dialog is
open cancels the export. Clips keep their existing whole-panel export behavior.
The format is Geany Insight notes (`.conf`), not Source Insight XML.
