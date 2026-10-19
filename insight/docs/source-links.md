# Source Links: search results ↔ editor

Enter, double click, **Jump To Link**, or Ctrl+L in Search Results opens the selected match. In the editor, **Ctrl+L** on a linked line returns to its result and reveals Search Results. Unlinked editor lines retain the native key behavior. With several matches on one line, return prefers the last opened result, then the first available one.

![Source link lifetime](media/source-links.svg)

## Navigation and edits

The Search menu provides Jump To Link and First/Previous/Next/Last Link commands. Previous/Next in the results also open the anchored line; navigation wraps. Sorting or removing a result does not reattach other links.

For documents already open when results arrive, anchors are captured only while the search snapshot still matches the buffer. They follow insertions/deletions, including UTF-8 and CRLF, updating the line and column. Editing inside a match retains its line link: the selected range may no longer match the original query. Source text remains the original search snapshot; rerun the search after meaningful content changes.

Deleting the complete target line invalidates its link. Undo restores text but does not resurrect the deleted link; rerun the search. A warning icon identifies invalid links. Opening one does not jump to a neighboring line.

Closed target files open automatically. A saved target closed and reopened is checked against its current snapshot. Changed disk files or discarded unsaved changes cause navigation to refuse the old address. The file may still be opened for this check.

Closing Search Results releases live anchors; result rows remain verifiable snapshots that can be opened again. A new search starts a fresh anchor set. Links are session-only, do not write project data, and do not provide compiler semantic resolution. Compiler output and arbitrary linked text files are not supported yet.

## Validation

The native test runs the actual editor and checks reverse Ctrl+L, UTF-8/CRLF, insertions, undo/redo, sorting, result/target-line removal, a fresh search, saved target reopening and changed-disk refusal. A separate test covers offset arithmetic and overflow.
