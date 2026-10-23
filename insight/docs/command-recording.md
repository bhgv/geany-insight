# Command recording

**Edit → Command Recording** adds Start Recording, Stop Recording, Play Recording,
Recorded Editor Commands and Clear Recording. Five native Editor shortcuts are
available in Preferences, initially unassigned. Start replaces the previous
recording. Play and the inspection dialog automatically stop recording first.

![Recording flow](media/command-recording.svg)

Record a short sequence in one writable source editor, stop, place the caret in
the desired source location, then Play. Literal input (including UTF-8/newlines),
relative character/word/line movements and their selection variants, Home/End,
document edges, Select All, backspace/deletions, line duplication and case changes
are supported. The inspector lists the exact native command names and a text
excerpt for each step. Playback uses the current selection; native commands apply
the current editor settings. One Undo restores completed edits; Redo reapplies them.
The record itself is not changed by playback.

This is a **bounded native editor command recorder**, not Source Insight's full
application command recorder or macro language. Menus, project actions, file I/O,
completion, auto-pairs, automatic indentation and plugin-generated edits are not
recorded as application actions. Record simple keyboard editing sequences; review
the list and resulting text before reuse. Mouse presses stop before caret movement;
changing or closing the source also stops and keeps the completed prefix. Undo or
Redo while recording discards it. Clipboard commands, searches, absolute positions,
rectangle/multiple-selection commands and other unsupported macro notifications
discard the entire recording. They are never silently skipped during playback.

One recording belongs to each workspace. By default it remains in memory. Enable
**Remember Recording in Workspace** to save the current sequence on the next normal
preferences/project save. Without a project it is stored in `geany.conf`; with a
project it is stored in that project's `.geany` file. Literal source text is saved
there as escaped UTF-8. The option is independent for each workspace and disabled
for new projects. Opening a project stops the current recording and loads only
that project's sequence; closing it restores the global sequence and its option,
including a memory-only global recording. No sequence is played automatically.

Disable the option to remove saved steps on the next normal save; the in-memory
sequence remains usable. Clear followed by a normal save removes the saved steps
while retaining the option. Start replaces the sequence. Review it in Recorded
Editor Commands before explicit Play. Loading rejects unknown versions, commands
outside the recorder whitelist, mismatched arrays, invalid UTF-8 and exceeded
limits as a whole. There is no compatibility with binary Source Insight workspace
files or automatic import of recordings from them.

No clipboard or disk access occurs
from Play. Read-only, rectangular, multiple and virtual-space targets are refused.
The source editor must have focus for Play. Start requires a writable ordinary
selection. Limits: 256 actions, 64 KiB of literal payload, 1 MiB source document,
16 MiB cumulative replay work. Conservative growth checks may stop a sequence
before all commands run; the status bar reports this and one Undo restores the
completed portion. No automatic rollback or repeated-loop execution is provided.

[Literal typing repetition](typing-replay.md) remains separate.

Reference: [Source Insight Start Recording](https://www.sourceinsight.com/doc/v3/ad921370.htm).
