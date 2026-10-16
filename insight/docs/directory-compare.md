# Directory Compare

Open **Tools → Directory Compare…**, or **Compare directories…** in File Compare.
Enter Left and Right directories (or Browse), then press **Scan / Refresh**, Enter
in a path, or F5. Scanning, content reading and comparison run in a cancellable
GTask worker. Completed rows are prepared in UI idle slices of roughly 3 ms.
Cancel, a missing root, a scan error or an entry/depth limit leaves the previous
snapshot intact. A late worker completion cannot update a destroyed window.

## Choose what to compare

| Method | Meaning of Same for regular files |
| --- | --- |
| Compare contents | Equal bytes, independently of modification dates |
| Compare modification dates | Equal modification time including microseconds; contents and size are not read |
| Compare sizes and dates | Equal size and modification time; contents are not read |

**Include subdirectories** recursively matches exact relative paths. Turn it off
to inspect direct children only. **Include hidden entries** includes dot names
and entries marked hidden by the filesystem. These options apply on the next
scan. There is no filesystem watcher in this window; F5 refreshes the snapshot.
Filename matching is exact and case sensitive, even when both filesystems fold
case. Directory roots must be representable in the interface filename encoding;
child names retain their native bytes internally and use display names in the UI.

In content mode, **Ignore spaces and tabs** skips ASCII horizontal whitespace
(space, tab, vertical tab, form feed). **Ignore line ending style** normalizes
CR, LF and CRLF to one terminator. Missing final terminators remain significant.
This is a linear normalized-byte comparison, not a language parser. Unlike the
line-based File Compare engine, an unterminated whitespace-only fragment can
normalize to empty content. Text options require valid UTF-8 without NUL and at
most 1 MiB per side; unsupported text receives Skipped, never Same. With text
options off, binary content is compared exactly in bounded streaming reads.

## Read the result

| Status | Meaning |
| --- | --- |
| Same | Regular files satisfy the selected method |
| Different | Regular files differ; the label also indicates the newer side when times differ |
| Left only / Right only | The relative path exists on one side |
| Type mismatch | One side is a file and the other a directory |
| Folder | Both sides are folders; inspect their child rows |
| Skipped | Symlink, special file, unsupported text, file-size or byte-budget limit |
| Read error | File cannot be read or changed while being checked; hover for details |

Folders do not claim that their complete contents are identical. Enumerated
symlinks are shown but not followed; a symlink root is rejected. Size, time,
file identity and etag are rechecked around reading where the filesystem supplies
them. This is a best-effort snapshot, not an atomic filesystem transaction or a
security boundary against concurrent path replacement.

**Differences only** hides Same and paired Folder rows, while retaining skipped
and error rows. **Ctrl+F** focuses a case-insensitive Unicode substring filter on
displayed relative paths. Click Relative path or Status to sort; filter and sort
remain active after refresh. Counts distinguish same, different, one-sided,
type, skipped, errors and paired folders. Left/Right columns show size and local
modification date; the full diagnostic is in the row tooltip.

**Previous / Next different file** or Ctrl+Up / Ctrl+Down selects a non-Same,
non-Folder row in visible sort order, stopping at the ends. Double-click, Enter
or **Compare selected file** opens a pair of regular files in a new File Compare
window. The action uses roots and text flags from the completed snapshot, even
if path fields or options have since changed. Content is re-read there; it can
have changed since this scan. File Compare has its own 1 MiB / UTF-8 and LCS limits.
One-sided paths, folders and links do not open as a fabricated empty file.

The context menu also copies a relative display path or the **visible list**.
Copy visible list preserves the displayed sort/filter and produces UTF-8 TSV with
path, status and left/right metadata. Backslashes, tabs and line breaks inside
fields are escaped as `\\`, `\t`, `\r`, `\n`. It is a human-readable report,
not a lossless native-filename manifest. Snapshot rows are read-only. Copying or
deleting files between directories, known-filetype filtering, path variables,
backup comparison and persistent directory-window settings remain future work.

![Production Directory Compare window in an isolated Linux GTK fixture](media/directory-compare.jpg)

The JPEG is a real production-widget capture with synthetic temporary files,
not a full-editor screenshot. The selected Cyrillic pair compares equal after
horizontal-space and line-ending normalization; it retains different byte sizes.

![Directory matching and pair workflow](media/directory-compare.svg)

## Bounds and verification

Defaults: 20,000 union entries, 100,000 visited entries across both trees, depth
32 and relative paths up to 16,384 bytes. Exact content mode reads paired files
up to 64 MiB per side; normalized text mode reads up to 1 MiB per side. A scan has
a conservative 256 MiB content allowance, charged by both snapshot sizes before
reading; a pair that cannot fit is Skipped. Exact-mode size mismatches need no
content read. Skipped/error counts must be inspected before treating a report
as complete. Subdirectory enumeration errors abort the scan rather than report
an incomplete tree as equal.

Meson tests: **Insight directory comparison snapshots**, **Insight isolated
Directory Compare GUI**, **Insight native Directory Compare routing GUI**.
The first two pass locally on Linux with UBSan, strict warnings and fatal GLib
criticals. They cover content/text/metadata modes, binary bytes, hidden and nested
paths, links, limits, cancellation, filter/sort, clipboard, snapshot-root routing,
empty results and closing while a worker is pending. The native test boots the
complete editor, opens the Tools command, scans a pair and verifies the real
File Compare result. Linux CI includes all three and the existing editor tests.
Windows and Android have not been executed in this series.

Reference: [Source Insight Directory Compare](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Directory_Compare.htm)
and [Directory Compare Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Directory_Compare_Options.htm).

**Columns…** toggles Status, Left metadata and Right metadata for this window.
Relative path stays visible. Hiding a sorted column keeps its sort order;
Copy visible list still exports all four report fields for the visible rows.
