# Window List

Open Window → Window List…. This is a reusable nonmodal floating window listing the editor's live documents, including untitled buffers. It does not load extra project files or Context-only disk previews into the editor.

The search matches names, full paths and nearest parsed symbols using Unicode case folding. Columns show the active buffer, file name, modified/read-only status, line count, nearest symbol and path. Click column headers to sort; the selector provides most recently used, name and path order. Recent order is tracked through document open/activation events for this editor session.

Single selection/click previews the buffer at its retained caret in Context; the editor stays on its current document. Double click, Enter from the search/list, or Activate switches the editor to the selected buffer while preserving its caret. Save uses Geany's native save/Save As handling. Close file uses its native unsaved-change prompt, so cancelling keeps the buffer open. Actions resolve stable document IDs at execution time.

The list refreshes after document events and editor updates while visible, keeps the selected document when possible, and uses coalesced updates. Escape hides the window; reopening preserves the search and sort. The window close button also hides it. The nearest-symbol column uses existing parser tags, not a proven enclosing semantic scope. Use Dock in sidebar to move the same list into a sidebar tab. Clear it to return to the floating window. Escape or the tab close button hides only the list; Window → Window List… reveals it again. The mode is saved, and the list opens in that mode when requested after restarting. General edge dragging, snapping and Window List integration into A–D layouts remain open.

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

### Window List in layouts A–D

Save current layout stores Window List visibility, sidebar docking and floating position, size and maximization. Loading a layout restores this state while preserving the list, filter, sorting and columns. A hidden list stays hidden. Layout format v3 reads existing v1/v2 records; those older layouts leave Window List unchanged. Floating geometry is fitted to an available monitor work area. Dragging and snap-to-edge remain open.

## Selected-file operations

![Selected-file workflow](../media/window-batch.svg)

Use Ctrl-click or Shift-click to select several visible rows. **Select all** selects the filtered list; **Invert selection** reverses that selection. **Activate** and Context preview require a single row. **Save**, **Save As…** and **Close file** process selected stable document IDs in visible sort order. Read-only documents are skipped by save commands. A cancelled save/close dialog or failed operation stops the remaining batch; completed saves or closes are retained. Save As uses the normal editor dialog and activates each target tab.

**Copy paths** puts selected named-buffer paths on the clipboard, separated by newlines (maximum 2 MiB). Untitled buffers are skipped; an empty result keeps the clipboard. **All open files / Modified files / Read-only files / Unsaved files** filters intersect the text query. Unsaved means the buffer has no backing disk identity; the state filter is specific to this list instance.

Background refresh compares the displayed metadata before replacing rows. Ordinary caret movement inside the same nearest symbol leaves the model, multi-selection and scroll position intact. Real row changes preserve selected document IDs still visible; if none remain, the current or first visible file is selected. Filtering never changes project membership or scans project directories.

This extends the [Source Insight Pick Window](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Pick_Window.htm) save/close workflow. Geany uses source tabs; per-source minimize/restore and tiling are outside this command.
