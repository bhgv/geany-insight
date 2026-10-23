# Symbol relations

Definitions, references and relationships around a selected symbol.

## Controls

Choose definitions, members, base/derived types, references, calls or call sites. Filter results or use the graph view. Back restores the previous query. Results use Ctags and lexical analysis rather than a full semantic analyzer.

## Short tutorial

Put the caret in a function. Select Calls. Inspect a result, navigate to its target and return with Back.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [relations-calls.md](relations-calls.md).

Start a list filter with `~` for case-insensitive Unicode subsequence matching: `~srd` finds `Shared`. Ordinary queries keep literal substring matching, including spaces and punctuation. A bare `~` matches nothing.

## Reference search regions

In **References**, choose **all text**, **skip comments** or **comments only**.
Skip comments keeps string literals: searching `target` finds it in `"target"`
but not in `// target`. Comments only reverses that choice and ignores strings.
**References: code only** excludes both comments and strings; enabling it resets
the comment selector, and choosing a comment filter turns Code only off.

These are lexical filters for C/C++, JavaScript, Python, Lua and Shell. Python
triple-quoted strings/docstrings remain strings. Unsupported languages are skipped
in filtered modes; use All text to search them. These filters do not evaluate
preprocessor conditions or distinguish unrelated symbols with the same name.
They apply to References, not project text search or replacement. Call sites keep
their existing code-only behavior. Back restores the reference filter; the chosen
filter is saved for the next session. Selecting a result previews the original
source in Context at its original position.

[Relation outline snapshots and bookmark batches / Текстовое дерево и пакет закладок](relation-results.md).


**Next Relation Window View**, in Navigate and beside the Relations view controls, cycles list/tree → horizontal graph → vertical graph → list/tree. It retains zoom, query, filter and loaded branches without launching a new search. The expandable-tree option independently selects the underlying outline.

## Lock and refresh

**Navigate** now includes **Lock Relation Window**, **Refresh Relation Window**, **Relation List / Tree View**, **Horizontal Relation Graph** and **Vertical Relation Graph**. Lock is the inverse of Follow: either control updates the other. Disabling tracking cancels the active automatic query without clearing results or restarting it. Loaded trees remain available for manual expansion.

Refresh reruns the panel query and restores hidden nodes while retaining the lock, filter and selected bottom-shelf tab. It declines in project text-search mode; use the project search commands there. Direct view commands preserve zoom without querying again.

![Lock and refresh](../media/relation-lock.svg)
