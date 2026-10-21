# Backup versions

**File → Save New Backup File** writes an independent snapshot of the current buffer, including unsaved edits. It keeps the source filename, text, selection, modified state, Undo/Redo and disk file unchanged. A named read-only source can be backed up too. Save untitled files with a name first.

**File → Open Backup File…** lists that source's versions, newest first, with UTC creation time, byte size and unique version name. Select a row to preview the first 16 KiB. The preview normalizes line endings for display; the stored snapshot and opened editor tab retain exact bytes. Clipboard text transfer may normalize line endings on the target platform.

![Backup versions in the running GTK editor](../media/backup-versions.jpg)

- **Open Backup** opens the complete snapshot in a detached, read-only editor tab. It does not replace or save the source. To use its text, copy it explicitly or export it with Save a Copy.
- **Copy Snapshot** copies the complete selected version, including text beyond the preview.
- **Refresh** rereads the list and selects the newest version. A missing or changed selected file cannot be opened with its old listed identity.
- **Show Folder** opens this source's backup folder in the system file manager. Before the first snapshot, no folder is created just by browsing.
- **Close** is the default dialog response.

The same folder action is available directly in the File menu. All three commands appear in the command browser and Editor keybinding preferences, initially without assigned shortcuts. **Backup Versions Help** opens this guide.

![Manual snapshot workflow](../media/backup-versions.svg)

Snapshots live under `insight-backups/<SHA-256 of canonical absolute source path>/` in the Geany configuration directory. The source identity is lexical: `..` is normalized, but symbolic-link aliases, case differences and renames can produce different histories. Equal basenames in different directories remain separate. No source filename or encoding metadata is written into snapshot text. Version names use UTC microseconds and a random UUID.

Files contain decoded UTF-8 buffer bytes without an added BOM; a literal U+FEFF already present in the buffer is retained. Original disk encoding and BOM flags are not preserved as metadata. Text must be valid UTF-8 without embedded NUL and at most 8 MiB. Checks limit each source family to 200 versions and 64 MiB; directory scans stop with an error above 1000 entries. A snapshot that would exceed a limit is refused and old versions are kept. Manage unneeded versions explicitly in the folder. There is no automatic pruning, automatic backup-on-save, scheduling or crash recovery in this series.

Writes create a private exclusive temporary file, close it, then publish with a no-overwrite move. Interrupted `.pending-` files are not shown as versions. Readers reject symbolic links and nonregular/oversized entries, check listed size/etag/file identity before and after reading, and validate UTF-8. Folder components are checked without intentionally following symbolic links. These portable GIO checks do not hold a filesystem lock; they cannot eliminate concurrent path substitutions or guarantee quotas across simultaneous editor processes. Modified backup files become eligible again only after Refresh and validation.

Linux tests cover exact UTF-8/U+FEFF/mixed-EOL/empty snapshots, source path separation, unique ordering, stale identities and symbolic links, count and byte ceilings, the real preview dialog, copying, read-only opening, refresh, untitled/NUL rejection and unchanged source disk/Undo/selection. The screenshot shows the production dialog captured under Xvfb. Windows and Android were not run.

Reference: Source Insight 4 [Save New Backup File](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Save_New_Backup_File.htm) and [Open Backup File](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Open_Backup_File.htm). Storage layout and detached read-only opening are specific to this implementation. [Checkpoint](checkpoint.md) starts a new editing baseline and clears history; a backup preserves a separate version.
