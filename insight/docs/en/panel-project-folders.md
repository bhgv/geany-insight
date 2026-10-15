# Project Folders

A tree of indexed project files, built from the manifest rather than all disk contents. The first 10,000 manifest files are displayed.

## Controls

Filter paths using fragments, glob (`*.c`), a leading `?` for regex or `~sc` for fuzzy subsequences. Space-separated conditions are combined. Matching files retain their ancestor folders; active filters expand the branches. Invalid regex marks the entry as an error and retains the last valid result. The counter shows displayed files / manifest files.

Click a file to preview it in Context. Double-click or Enter opens a file; double-click toggles a folder. Right-click selects a row and opens its menu without navigating Context or the editor. File actions are disabled over empty space.

The menu opens a file, copies its full or project-relative path, locates the current file, expands/collapses the selected branch or entire tree, clears the filter and opens help. Paths outside the root, or projects without a root, copy the full path instead.

The target button selects and centers the current file, expanding its ancestors without moving Context or the editor. Follow current file performs this on document changes; the option is saved independently of Context Follow. Hidden or absent files do not clear the filter; manual Locate explains the condition in the status bar.

Tree keys: Ctrl+F focuses the filter; Ctrl+C copies the full path; Ctrl+Shift+C copies the relative path; Ctrl+Enter opens a file; Ctrl+L locates the current file; Menu / Shift+F10 opens the menu; Escape focuses the editor. In the entry, Down selects the first file, Enter opens it and Escape clears the query before returning focus to the editor. Use Primary instead of Ctrl on macOS.

## Short tutorial

1. Open a project and its Folders tab.
2. Enter `*.c` or `~sc` and inspect the counter and expanded branches.
3. Right-click a file and copy its project-relative path.
4. Clear the filter, open another document and enable Follow current file.
5. Collapse its branch and press Locate: the file becomes visible while the editor caret remains unchanged.

The question icon opens this page. Help follows the interface language. See [guide.md](guide.md).

## Browse unindexed disk files

The selector above Folders switches between **Project files** and **Browse disk**. Disk browsing starts at the current workspace root each time it is selected. Edit the directory path and press Enter/Refresh, enter a directory by double-click/Enter, or use Up/Alt+Up. F5 refreshes; Ctrl+L focuses the directory path and Ctrl+F the filter (Primary instead of Ctrl on macOS).

Each directory is read in a cancellable background task. Ordinary/unknown file extensions are included without changing membership or indexing rules. Click a file for Context preview; double-click/Enter opens it in the editor. Show hidden disk entries rereads the directory. Links are labelled and are not automatically traversed as directories. There are no move/delete operations here.

The file filter accepts the existing fragments/glob/regex/~fuzzy syntax; directories stay visible for navigation. Invalid queries retain the last valid filter. Filtering applies to the loaded directory only, not a recursive disk search. At most 2048 eligible entries and 100000 enumerated entries are read per directory; the status explicitly reports truncation. Change to a narrower directory to inspect more. Switching paths, refreshing or closing discards superseded tasks. Disk mode does not add/remove files in the project. Virtual project groups remain separate work.
