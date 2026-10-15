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

## Master File List

To share the project membership through a text file, first use **Export file list…** in Add and Remove Project Files. Choose that existing file in **Master file list** and Apply. Its paths replace the inclusion rules; relative paths are based on the project root. Clearing the field detaches the list. The binding is saved relative to the project root when possible.

The list is checked in a worker about every three seconds. External additions/removals update the project file list and symbol index; project masks/exclusion overlays are respected. Project → Open Project Master File List opens the text, and saving it requests immediate synchronization. Project → Synchronize Master File List requests a check explicitly. Cancel indexing pauses background MFL reads too; explicit synchronization, saving the list, changing its binding or applying membership resumes them.

Manual Add/Remove/Remove all updates the MFL before publishing project membership. Directories and explicit exclusions are flattened to the currently eligible file entries on a manual write, and old explicit exclusion rules are cleared; masks remain. This lets a later external list reintroduce a manually removed file. No source file is deleted. If project settings cannot be saved, the MFL is restored byte-for-byte, provided its new snapshot has not been changed externally. Content digests supplement GIO ETags on filesystems with coarse timestamps. Changed files or stale dialogs decline instead of replacing another version. Save or reload a dirty open MFL before changing membership.

The format is UTF-8 (optional BOM), one literal path per line, with LF or CRLF. Existing directories are recognized; a trailing `/` preserves a missing directory entry. Blank lines are ignored. Limits are 2 MiB, 10000 distinct entries, and a bounded manual directory scan (64 levels, 4096 directories, 100000 entries); generated/hidden directories and symlinks are not followed by that scan. Invalid/missing lists retain the last accepted project. Path Variables and quoted/comment syntax are not expanded. The MFL must differ from the project configuration file. Directory monitoring retains its existing 1024-monitor bound; a manual source rescan is available when monitoring is incomplete.

Reference behavior: [Source Insight 4 Master File List](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Using_a_Master_File_List.htm).
