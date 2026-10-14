# Window List

Open Window → Window List…. This is a reusable nonmodal floating window listing the editor's live documents, including untitled buffers. It does not load extra project files or Context-only disk previews into the editor.

The search matches names, full paths and nearest parsed symbols using Unicode case folding. Columns show the active buffer, file name, modified/read-only status, line count, nearest symbol and path. Click column headers to sort; the selector provides most recently used, name and path order. Recent order is tracked through document open/activation events for this editor session.

Single selection/click previews the buffer at its retained caret in Context; the editor stays on its current document. Double click, Enter from the search/list, or Activate switches the editor to the selected buffer while preserving its caret. Save uses Geany's native save/Save As handling. Close file uses its native unsaved-change prompt, so cancelling keeps the buffer open. Actions resolve stable document IDs at execution time.

The list refreshes after document events and editor updates while visible, keeps the selected document when possible, and uses coalesced updates. Escape hides the window; reopening preserves the search and sort. Closing its window destroys the view; the next invocation creates a fresh one. The nearest-symbol column uses existing parser tags, not a proven enclosing semantic scope. Arbitrary docking and persistent list geometry are not implemented yet.
