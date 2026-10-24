# Incremental search

**Search → Incremental Search / Incremental Search Backward** opens a temporary compact strip at the bottom of the main window. The source remains visible while a native GTK entry receives Unicode text, paste and input-method composition. The two Search keybindings start unassigned; existing F12 definition navigation stays available.

![Incremental search flow](media/incremental-search.svg)

| Action | Result |
| --- | --- |
| Type / Backspace | Refine a literal pattern from the original caret; failed searches retain the last selection |
| Previous / Next, or repeat either assigned command | Search in that direction from the current match; an empty entry loads the last successful pattern |
| Wrap around | Allow one wrap at the file boundary; enabled by default, retained only during this run |
| Enter / Accept | Keep the match and share the last successful literal pattern with normal Find Next/Previous |
| Esc / Cancel | Restore the original anchor, caret, horizontal offset and top source line; leave the normal search pattern unchanged |
| Native command key | Accept and return source focus before normal command dispatch; entry clipboard shortcuts and Backspace keep editing the pattern |

Lowercase-only patterns ignore case. Any uppercase Unicode character enables Match case. This is literal substring search, including a leading `~`; regex, fuzzy matching and whole-word filtering do not apply. Deleting the entire pattern leaves the last selection in place. A new invocation starts empty; repeating its command recalls the last successful pattern, even after cancellation. Previous pattern and wrap policy are not saved between launches.

The current source must have one stream selection without virtual space, valid UTF-8 without NUL and at most 8 MiB. Patterns use at most 1024 Unicode characters / 4096 UTF-8 bytes, on one line. Source edits, switching or closing the source end the mode without restoring stale positions. Moving focus outside the strip accepts the current search. A failed request cannot replace the saved successful pattern. No source edits, clipboard writes, Undo actions, file reads or project-index requests are made. Read-only sources and Browser Mode are supported. Unfolding a found line uses native Scintilla behavior; cancellation does not reconstruct the prior fold tree. Existing linked-scroll participants may follow viewport changes.

The automated Linux check uses the real application, native entry editing signals, configured key events, source widgets and menus; it does not claim manual OS/IME testing. Source Insight references: [Incremental Search](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Incremental_Search.htm) and [Incremental Search Mode](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Incremental_Search_Mode.htm).
