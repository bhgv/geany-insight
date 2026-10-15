# Window List

Open Window → Window List…. This is a reusable nonmodal floating window listing the editor's live documents, including untitled buffers. It does not load extra project files or Context-only disk previews into the editor.

The search matches names, full paths and nearest parsed symbols using Unicode case folding. Columns show the active buffer, file name, modified/read-only status, line count, nearest symbol and path. Click column headers to sort; the selector provides most recently used, name and path order. Recent order is tracked through document open/activation events for this editor session.

Single selection/click previews the buffer at its retained caret in Context; the editor stays on its current document. Double click, Enter from the search/list, or Activate switches the editor to the selected buffer while preserving its caret. Save uses Geany's native save/Save As handling. Close file uses its native unsaved-change prompt, so cancelling keeps the buffer open. Actions resolve stable document IDs at execution time.

The list refreshes after document events and editor updates while visible, keeps the selected document when possible, and uses coalesced updates. Escape hides the window; reopening preserves the search and sort. The window close button also hides it. The nearest-symbol column uses existing parser tags, not a proven enclosing semantic scope. Arbitrary docking is not implemented yet.

## Saved window settings

Resize columns by dragging their edges and reorder them by dragging their headers.
The window remembers its normal size and position, maximized state, search text,
column widths/order, and the actual sort column/direction (including custom header
sorting). It saves on Escape, window close, view destruction or editor shutdown.
Settings are restored the next time the list is created, including after restarting
the editor; the list opens only when requested.

The normal window rectangle is clamped to a monitor's available work area. Invalid
sizes, widths or sort values use defaults; malformed column permutations are ignored.
Preferences are stored atomically in `config/insight/window-list.conf`. The MRU order
of individual buffers remains specific to the current editor session. Changing the
interface language refreshes the displayed file-state labels and status message.

Prefix the filter with `~` for Unicode subsequence matching (`~hlpr` finds `helper`). Ordinary text keeps literal case-insensitive substring matching; the selected query persists when reopening the list.
