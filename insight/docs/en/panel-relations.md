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
