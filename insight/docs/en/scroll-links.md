# Linked source-window scrolling

**Window → Link Source Windows…** opens a modeless source-link manager. Choose Source and Target from the open-buffer lists, then **Apply Link**. Choosing endpoints does not switch the active editor; **Refresh** captures the current editor and rebuilds the target list. Select **None — unlink this source** to remove that source's outgoing link.

![Source scroll links](../media/scroll-links.svg)

| Setting or command | Behavior |
| --- | --- |
| Two Way | Add reverse traversal for this pair |
| Link All Source Windows | Scroll every open source buffer together, including newly opened buffers |
| Vertical scrolling | Apply the same change in visible display rows to reachable buffers |
| Horizontal scrolling | Apply the same change in pixel offset to reachable buffers |
| Unlink All | Clear individual links and disable Link All |

Vertical is initially enabled, horizontal initially disabled. Axis choices apply to all links and take effect immediately. Pair changes require Apply Link. Enabling links or changing axes establishes current view baselines without moving the views. Each target retains its own relative offset and clamps at its native scrolling bounds. Scrolling a target in a one-way pair does not scroll its source. Chains and circular graphs are supported; each reachable buffer is updated at most once per scroll event. Native asynchronous notifications are absorbed by cached view samples rather than fed back as another scroll.

## Scope and lifetime

The manager lists explicit links and their directions. Each source has at most one explicit outgoing pair; Two Way supplies an additional reverse path. Existing reverse links from other sources remain independent. Link All supersedes individual routing while enabled; turning it off retains the individual pairs. The Window menu and dialog share the same Link All state. Three initially unassigned Editor shortcuts expose Link Source Windows, Link All Source Windows and Unlink All Source Windows.

Links apply to the main Scintilla view of each open Geany source buffer, including inactive tabs and untitled buffers. They do not create tiled MDI windows or make hidden tabs visible. They do not link Context, terminal, diff/merge, Git-history or AI-preview widgets. Visible rows follow each buffer's own folding/wrapping; this is relative scrolling, not matching-line diff alignment. Scrolling caused by navigation or native view changes follows the same rules. Target text, selection, caret, Undo/Redo, read-only flags and clipboard are not changed. The feature does not read source files or query/rebuild the project index while scrolling.

Links and axis choices are memory-only for the running application. Closing the manager keeps links active. Closing a source or target removes affected pairs; stale dialog endpoints cannot be applied silently. Refresh chooses the current editor explicitly. New Link All members adopt their initial viewport without an immediate group jump. Individual links are capped at 64 pairs; Link All supports 128 open source buffers and disables itself with a status message if that limit is exceeded. Individual pairs remain when Link All is disabled. No project file or source file is written.

References: [Source Insight Link Window](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Link_Window.htm) and [Link All Windows](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Link_All_Windows.htm).

## Manage individual pairs

Choose both endpoints in the dialog. Selecting a table row loads its source,
target and direction for editing; Apply Link commits that change. Remove selected
link removes only that pair. Open/closed documents update both lists; closed
endpoints cannot be applied. Refresh explicitly selects the current editor as
source. Disabling both axes shows a paused-scrolling message. Language changes
retain endpoint choices and an unapplied direction change.
