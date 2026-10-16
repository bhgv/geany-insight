Open the internal comparison window from **Tools → Diff / Merge…**. The Context panel also has a Diff / Merge button.

# Internal Diff / Merge

Open **Diff / Merge…** in the Context header. Choose Left and Right files and press
**Compare**. The nonmodal window displays both read-only sources, highlights changed
lines and lists changed blocks. Selecting a block scrolls all three panes to it.
The editable result initially contains Left. **Take Left** / **Take Right** replaces
only the selected block; either version can be chosen repeatedly. Empty blocks
represent insertions or deletions. Manual edits are allowed in the result.

**Undo** / **Redo**, Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y cover block replacements and
manual edits. A block replacement is a single history operation. Editing after
undo discards redo. Result marks move with edits and are restored with history.
History uses full snapshots, bounded to roughly 32 MiB (the newest snapshot is
always retained). **Save result…** writes to a chosen destination with overwrite
confirmation. Closing or starting another comparison asks before discarding a
changed result. Sources are never saved automatically.

**External KDiff3 (optional)** explicitly launches `kdiff3` from PATH with the two
currently selected files, even when internal comparison exceeds its limits. It
never launches automatically. Missing KDiff3 does not affect the internal tool.
External editing has its own history, outside this window.

Initial scope: two-way, line-based comparison and manual merge; UTF-8 text without
embedded NULs, up to 1 MiB per source. CRLF and final-newline differences are
preserved. The dependency-free LCS engine trims common prefixes/suffixes and caps
its matrix at 4 million cells. This is not yet three-way base-aware merge,
automatic conflict resolution, intraline highlighting, or padding that visually aligns both source panes line for line.
If a manual edit removes several block boundaries, compare again to rebuild the
block list; the list describes the original sources, not a new diff of the result.

Core validation: `python3 insight/tests/diff_test.py` (C99 compiler required).
Native GUI builds include the module in both Meson and Autotools. GTK development
headers are needed to compile and exercise the window.

The line engine recognizes CR, LF and CRLF while retaining their exact bytes. Nonadjacent changes in CR-only sources remain separate blocks. A pure insertion/deletion after trimming shared edges needs one block and no LCS matrix; two-sided comparisons retain the four-million-cell work limit. The UI source-size limit is unchanged.
## File Compare navigation and display

**Previous difference** / **Next difference** and **Ctrl+Up / Ctrl+Down** select
and reveal a block in all three panes. Navigation stops at either end; buttons
are disabled at the boundaries. With no selection, Next selects the first block
and Previous selects the last. Clicking a changed source block also selects it.
The status shows the selected difference number and total count.

**Difference list** hides or shows the block list; navigation keeps working while
it is hidden. **Wrap lines** applies word/character wrapping to all three panes.
These display options last for the lifetime of the comparison window.

**Scroll sources together** is enabled initially. It maps the top visible source
line through the diff, so insertions/deletions do not accumulate a line offset.
Inside a changed block it aligns to the start of the opposite block. Disable it
for independent scrolling. The editable Result scrolls independently; selecting
a difference still reveals its current marked range. This is position linking,
not blank-line padding or pixel-perfect alignment of wrapped lines.

Reference: [Source Insight File Compare](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/File_Compare.htm).
The Source Insight commands copy directly between files; this implementation
keeps Take Left/Right directed into the separate result buffer.

Validation for this increment: core suite (9 tests), 3000 randomized diff
reconstructions, bidirectional monotonic offset mapping, UTF-8 boundary checks,
insertions/deletions and the LCS work limit. The production GTK window also passes an isolated Linux/Xvfb fixture with
strict compiler warnings and fatal GLib criticals; this is not a full editor run. Manual check: compare the supplied example, navigate
both ends using buttons and shortcuts, hide the list, click each changed source,
scroll past insertions with linking on/off, toggle wrapping, then take/undo/redo.
