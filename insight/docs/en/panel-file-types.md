# File Types

Manifest files grouped by their effective language profile, including current editor filetype/forced binding and persisted path/extension profiles. This is more than extension grouping. The first 10000 manifest files are displayed.

## Controls

Filter full paths using fragments, glob (`*.cpp`), leading `?` regex or fuzzy subsequences (`~fced.special`). Empty groups disappear and matching groups expand. The counter reports displayed / manifest files. Invalid regex retains the last valid tree and prevents Enter from opening an old result.

Selection previews in Context; Enter/double-click opens a file. Groups expand/collapse without editor navigation. Right-click selects a row without moving Context/editor and opens its menu. Source actions are disabled on groups and empty results.

The menu opens sources, copies names/paths, creates exact file filters, clears the query, expands/collapses branches or all groups, controls visible columns, resets columns and opens help. Name remains visible. Reordering, widths, visibility and sorting are saved independently of Categories and other lists. Reset keeps the query and sorting.

Tree keys: Ctrl+F filter, Ctrl+C name, Ctrl+Shift+C path, Ctrl+Enter open, Menu/Shift+F10 menu, Escape editor. In the entry, Down selects the first file, Enter opens it and Escape clears the query before returning focus. Use Primary on macOS. Refresh preserves eligible selection and expanded groups.

## Short tutorial

1. Open File Types and inspect your custom language-profile groups.
2. Enter `*.cpp` or a fuzzy path and check the counter.
3. Right-click to copy a path or create its exact filter.
4. Adjust columns, restart and check restoration.
5. Clear the query; use Down and Ctrl+Enter to open a file.

See [languages.md](languages.md). The question icon opens this page in the interface language.
