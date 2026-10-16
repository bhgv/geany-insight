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

The selector above Folders switches between **Project files** and **Browse disk**. Disk browsing initially starts at the workspace root and then resumes the last visited directory. Edit the directory path and press Enter/Refresh, enter a directory by double-click/Enter, or use Up/Alt+Up. F5 refreshes; Ctrl+L focuses the directory path and Ctrl+F the filter (Primary instead of Ctrl on macOS).

Each directory is read in a cancellable background task. Ordinary/unknown file extensions are included without changing membership or indexing rules. Click a file for Context preview; double-click/Enter opens it in the editor. Show hidden disk entries rereads the directory. Links are labelled and are not automatically traversed as directories. Move/rename/trash commands are described below.

The file filter accepts the existing fragments/glob/regex/~fuzzy syntax; directories stay visible for navigation. Invalid queries retain the last valid filter. Filtering applies to the loaded directory only, not a recursive disk search. At most 2048 eligible entries are retained and 100000 entries enumerated per directory; the status explicitly reports truncation. Change to a narrower directory to inspect more. Switching paths, refreshing or closing discards superseded tasks. Disk mode does not add/remove files in the project. Virtual project groups remain separate work.

The directory, last valid query, hidden-entry choice and Project files/Browse disk mode persist in workbench settings. Invalid/relative stored roots and invalid filters are ignored; widths outside 32–2000 pixels and malformed booleans retain defaults. Columns can be resized/reordered, and **Columns → Kind / File Size / File Date** toggles the optional columns; the filename column remains available. Widths, full column order, visibility and sort direction persist too. Old settings retain their Kind-first layout; invalid new order/sort values are ignored. An invalid filter shown at shutdown does not replace the last valid saved query. The explicit Project Files `.`/`..` commands always select the workspace root/parent even if another disk directory was saved.

**Follow current file** in Browse disk follows the active editor's directory and selects its entry without invoking preview/open or moving the editor caret. The option persists separately from project-tree Follow. The current entry remains visible through the file filter and hidden-entry setting without changing either setting. Within the 100000-entry scan bound it is retained even when the 2048-row list is truncated. Switching to Project files suspends disk reads; returning resumes Follow at the latest editor file. Disabling Follow removes its visibility exception through a normal refresh.

**Font & Color…** opens independent appearance settings for the project tree and disk list. Enable Use custom appearance to choose the list font (4–96 points), text color and background. Apply changes only that list; Cancel leaves its settings untouched. Disable custom appearance and Apply to return to the GTK theme. Font and colors persist, invalid stored values are ignored, and selected rows keep the theme's selection colors.

**New folder…** creates one child directory in the displayed disk directory. The dialog shows the parent path; enter a single UTF-8 name (up to 255 bytes), then Create. Empty names, `.`/`..`, path separators, line breaks and oversized paths are rejected. A changed directory requires reopening the dialog. Creation runs asynchronously, never replaces an existing entry and refreshes the same directory if it is still active. Errors are shown in the browser status. The operation does not edit project membership; automatic project discovery keeps its usual rules. Pending creation is cancelled during workbench shutdown or when the browser is destroyed; cancellation does not undo a directory already created. Rename/move/trash are described below.

Right-click a disk row or press Menu/Shift+F10 for commands: Preview in Context, Open (or enter a folder), Copy full path, Copy filename, Up, Refresh, New folder and Font & Color. Right-click selection does not preview/open a file or move the editor. With no row selected, Copy full path copies the current directory; file-only commands are disabled. Folder preview is disabled, while Open navigates into the folder. Menu captions follow the interface language.

### Disk file size and date

File Date is initially visible and File Size hidden, following the Source Insight
Folder Browser options. Dates show modification time in the local timezone as
`YYYY-MM-DD HH:MM:SS`; sizes use human-readable units. A zero-byte file shows zero;
folder/link sizes and unavailable metadata are blank. Metadata is captured in the
existing cancellable directory worker with no target traversal or recursive size
calculation. Refresh/F5 rereads it.

Click a column header to sort ascending, then descending. Size/date sorting uses
64-bit byte counts/timestamps, independent of formatted labels. Sorting applies
to the loaded bounded directory snapshot; filters and Follow retain their usual
behavior. Columns can be dragged/resized; names remain available. Unknown size/
time values sort before known values ascending and after them descending.

Adaptive layout remains separate work. Reference: [Source Insight Folder Browser Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Project_Folder_Browser_Options.htm).

### Folder Browser Options

In Browse disk, use **Folder Browser Options…**, its context-menu item or Ctrl+Q
while focus is inside the disk browser. The dialog collects Kind, File Size,
File Date, Follow current file and Show hidden disk entries. Apply publishes the
checkbox choices together; Cancel leaves them unchanged. Follow/hidden changes
request one refresh using the final settings, without changing project membership.
Existing direct controls remain synchronized and all choices use their existing
persistent settings.

Font & Color opens the existing independent appearance dialog. Its own Apply
commits appearance immediately; cancelling the outer options dialog does not
undo a separately applied appearance. Cancelling the appearance dialog changes
nothing. This dialog configures the disk list; project-tree Follow and appearance
remain independent.


### Rename, move and trash disk entries

The Browse disk context menu now has **Rename…**, **Move to…** and **Move to Trash…**. F2 renames the selected disk row when the file list has focus. Rename takes one UTF-8 filename; Move to takes the complete absolute destination path, including the new name. Trash requires confirmation and uses the system trash only; if the backend cannot trash the entry, the operation fails without permanent deletion. Cancel leaves the entry intact.

Operations run in a background task and do not overwrite existing destinations. Cross-filesystem moves that require a copy/delete fallback are refused. A file ID and, when available, an etag from the displayed directory snapshot are checked before changing the entry. Refresh after an external change; filesystems without an identity attribute cannot use these commands. This is a pre-operation check, not a filesystem lock against concurrent external changes.

Close any open editor documents at the source path or below a source folder first. A changed directory while the dialog is open requires reopening it. On completion the original directory refreshes only if it is still active; navigating elsewhere or closing the browser prevents stale UI updates. Cancellation during shutdown cannot undo a move or trash operation already completed by the filesystem.

These commands change disk contents. They do not rewrite stored project membership paths, bookmarks or other references; update those paths separately after a rename/move. No recursive permanent-delete command is provided. This series does not complete adaptive Folder Browser layout or all Source Insight file-management behavior.
