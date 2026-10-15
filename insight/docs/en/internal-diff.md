Open the internal comparison window from **Tools → Diff / Merge…**. The Context panel also has a Diff / Merge button.

# Internal Diff / Merge (initial version)

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
automatic conflict resolution, intraline highlighting, or synchronized scrolling.
If a manual edit removes several block boundaries, compare again to rebuild the
block list; the list describes the original sources, not a new diff of the result.

Core validation: `python3 insight/tests/diff_test.py` (C99 compiler required).
Native GUI builds include the module in both Meson and Autotools. GTK development
headers are needed to compile and exercise the window.

The line engine recognizes CR, LF and CRLF while retaining their exact bytes. Nonadjacent changes in CR-only sources remain separate blocks. A pure insertion/deletion after trimming shared edges needs one block and no LCS matrix; two-sided comparisons retain the four-million-cell work limit. The UI source-size limit is unchanged.
