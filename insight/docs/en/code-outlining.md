# Code folding and block browser

**View → Code outlining** and the editor popup provide eight commands: expand/collapse the current block; expand/collapse its entire branch; expand/collapse all code; fold to level; Code outline. Assign shortcuts under **Preferences → Keybindings → Document**. New bindings are unassigned by default.

The current block is the caret header or nearest lexer fold parent. Branch operations change every descendant state. Level 1 leaves outer headers visible; level 2 expands these and contracts the next header level. Levels 1–12 are available. Cancelling the dialog leaves folding unchanged.

![Blocks and levels](../media/code-outlining.svg)

**Code outline** is a modeless window with line, depth, state and source header columns. Case-insensitive search filters headers; columns support sorting/resizing. Go To (also double-click), block/branch expansion and contraction, Copy List and explicit Refresh are available. The TSV copy escapes control characters. Refresh preserves the filter.

The list captures the current document. Editing, changing language or switching documents rejects actions until **Refresh**, which captures the then-current document. Closed sources are not reopened. Go To enters native navigation history. Expanding a hidden block follows Scintilla behavior and may move the caret to its header.

Folding uses the current native Geany lexer, not compiler semantics. Languages without fold headers produce an empty list. Read-only sources are supported; operations preserve text, savepoint, Undo history and disk files. Native language folding must be available; margin visibility options remain in Geany. Limits: 2 MiB, 100000 lines, 5000 headers, depth 256. Folding states and browser contents are not separately persisted.

Reference: Source Insight [Expand](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Expand.htm), [Expand All](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Expand_All.htm), [Outlining Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Outlining_Options.htm). The block browser and depth selection extend Geany Insight; Source Insight Expand Special operates on list trees and is not reproduced here.
