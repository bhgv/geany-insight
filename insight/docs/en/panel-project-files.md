# Project files

Lists files belonging to the current project.

## Controls

The filter narrows the list; sorting changes name and path order. Add and Remove commands change project membership.

## Short tutorial

Open a project. Type part of a name in the filter. Select a file for context; double-click it to open the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [guide.md](guide.md).


## File list menu and keyboard

Right-click selects a file without moving Context or the editor. The menu opens the file, copies its basename/full path, filters to its exact path, clears the query, resets columns, chooses visible columns and opens this help. Source commands are disabled on empty space and while a snapshot is updating.

In the list, Ctrl+F focuses its query, Ctrl+C copies the basename, Ctrl+Shift+C copies the full path and Ctrl+Enter opens the selected file. Menu or Shift+F10 opens its context menu; Escape returns to the editor. Single selection still previews in Context, and double-click still opens the editor.

## Sorting, columns and history

Sort by basename or full path; the icon-only arrow toggles descending order. Header sorting updates the controls. Resize or reorder headers, and use Visible file columns to hide Path; File remains visible. Reset file columns restores order, widths and visibility, retaining the filter and sorting. Column layout, sorting and filter history are saved independently from the symbol panels. The recent-history button remembers or selects up to 16 valid file queries (256 UTF-8 bytes each), including filename globs. Enter remembers a valid query before normal activation; Clear history preserves the query.

The counter shows loaded matching files and a + if additional matches remain after the 10000-row outline cap; project enumeration also has its own limit. Narrow the query to examine another subset. The full path is available as a row tooltip. Open/save/close/activate events refresh the list after the document change completes. Language compatibility continues to restrict eligible files.

Tutorial: find `*.cpp`, remember the query, select a file and copy its full path with Ctrl+Shift+C. Choose exact-file filtering in the menu, clear it, resize Path and reverse the sort order. Restart to restore the layout and history, then press Ctrl+Enter on a file to open it.
