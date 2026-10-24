# Browser Mode

**Edit → Browser Mode**, its editor-popup check item and the compact **Browse** status-bar button toggle temporary read-only code browsing. The status button's right-click menu also offers the toggle and this guide. An initially unassigned **Editor → Browser Mode** shortcut is configurable in the native key preferences and command browser.

![Browser Mode controls and state](media/browser-mode.svg)

| Action while the source editor has focus | Result |
| --- | --- |
| Plain left click on a code identifier | Invoke the existing definition resolver at that position |
| Space | Invoke the resolver at the caret; never insert a space |
| Backspace | Return through native navigation history; never delete text |
| Ctrl/Cmd + click or drag | Keep native selection behavior, bypassing the usual Ctrl+click definition jump |
| Click a margin, whitespace, string or comment | Keep native behavior; no browser definition lookup |

Other fields and source-preview widgets retain their own keyboard and editing behavior. Shift/Alt-modified clicks retain their native handling. Existing forward/history commands remain available. Source identifier lookups require a normal single selection and a buffer up to 2 MiB. Resolution follows the existing F12 resolver, including its language/index/scope limitations; this does not add semantic overload resolution or a new ambiguity chooser.

## Temporary protection and file operations

All existing source buffers and newly opened/created buffers become read-only. Turning the mode off restores each surviving buffer's original document and Scintilla read-only state, including buffers that were already protected. Typing, paste and normal editing commands retain native read-only guards. The ordinary file read-only checkbox cannot remove this temporary protection; exit Browser Mode to change that setting.

Enabling/disabling the mode does not edit, save or reload text, change encoding, discard dirty buffers, clear Undo/Redo or touch the clipboard. Explicit Save As, Reload and Clone retain their native operation semantics and restore browser protection afterward; a writable new copy becomes writable again after leaving the mode. Save As protection is reapplied even if saving fails. Saving an existing protected source still follows the native read-only rule. Leave the mode before continuing ordinary editing/saving of that buffer.

Browser Mode is **session-only** and starts disabled. Session serialization stores each file's underlying read-only flag, never the temporary browser flag. Closed document identities are removed from tracking. This is editor interaction mode, not an access-control boundary for plugins or external filesystem writes.

Reference: [Source Insight Browser Mode](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Browser_Mode.htm).

Navigation records the final selected-symbol caret, so Back returns directly to the originating location instead of an intermediate beginning of the definition line. Undo/Redo become available again after leaving read-only mode; their retained history is unchanged by the toggle.


Scroll related buffers together: [source-window links](scroll-links.md).
