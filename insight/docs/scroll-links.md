# Linked source windows

**Window → Link Source Windows…** manages project source links. The first table row contains editable **Source** and **Target** drop-down cells. Select a saved relation to load both selectors and its options, then **Apply Link** to change it. **Remove selected link** deletes just that relation.

![Linked source windows](media/scroll-links.svg)

Right-click an editor or its tab and choose **Link source window…**. The first click starts a draft with that file in the Source column. Choose Target in the first row and apply, or repeat the context command on another editor/tab. Selecting the same file does nothing; an existing pair, including the reverse ordering, is selected instead of duplicated. One file can participate in multiple links.

| Option | Effect |
| --- | --- |
| Show together | Selecting either endpoint reopens closed partners and presents the connected group side by side |
| Two Way | Either endpoint drives scrolling; otherwise Source drives Target |
| Vertical / Horizontal scrolling | Synchronize changes in visible rows / horizontal pixel offset |
| Link All Source Windows | Synchronize all open source buffers, including newly opened ones |
| Unlink All | Remove individual relations and turn off Link All |

The side-by-side window contains the actual editor widgets, with syntax highlighting, native editing, Undo/Redo and saving. Focus selects the corresponding document for editor commands. Drag the vertical dividers to resize the panes. Closing this presentation returns editors to their original notebook pages. Closing a file also restores the panes and retains named-file relations. Selecting its table row or subsequently activating a linked partner reopens it. A file that cannot be opened keeps its relation so it can be restored later.

Named-file links, direction, simultaneous-display flags and scroll axes are stored in the **project file**, with relative paths inside the project folder. They survive project close/reopen and application restart. Changes update only the link group without replacing unrelated project settings. Removing a member through the project file manager, master file list or Git membership refresh removes its relations. Untitled buffers and links without a project are session-only.

Scrolling preserves each buffer's relative offset and clamps at its own bounds. Chains and cycles visit each reachable buffer once. This uses display rows, including each file's own folding/wrapping, rather than matching-line diff alignment. Context, terminal, diff/merge and AI previews are not source-link endpoints. No source text is copied or changed by linking. Limits: 64 explicit relations; Link All supports up to 128 open source buffers. Closing the manager keeps links active.
