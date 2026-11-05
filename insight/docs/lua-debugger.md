# Lua debugger

Open a Lua buffer and choose **Scripts → Debug Current Lua Buffer…**. The
debugger takes a snapshot including unsaved edits and stops at the first
executable line. Use the **?** in the upper-right corner to reopen this guide.

## Execution and breakpoints

| Control | Key | What it does |
| --- | --- | --- |
| Continue | F5 | Run to a breakpoint or to the end of the script |
| Step Over | F10 | Advance without entering a called function |
| Step Into | F11 | Stop at the next Lua line, including inside a call |
| Step Out | Shift+F11 | Continue until control returns to the caller |
| Pause | Pause | Request a stop at the next yieldable Lua line |
| Stop | Shift+F5 | End execution and retain the breakpoint list |
| Restart | Ctrl+F5 | Run the original snapshot from the beginning |
| Go to Source | | Jump to the paused line if the editor buffer still matches the snapshot |

Click a line number to toggle a breakpoint. You can also enter numbers
separated by spaces or commas. The left pane highlights the paused line.
**Stack and locals** shows the call stack and named locals. Execution runs in
short slices between GTK events. A native call must return before the debugger
can pause. The Lua memory budget still applies.

While paused, hover over a variable in the debugger's source pane. The tooltip shows the local, captured value, or a simple field such as `config.device.name`. Values come from the current frame snapshot. Field lookup reads the table directly without invoking metamethods.

## Watch graph

Open **Watch graph** while paused. **Current frame** is the root. Follow the
arrows through local values, table entries, actual metatables and upvalues
captured by closures. For example, a table entry can point to a table also
reachable through a metatable and captured by a lambda. Those links lead to the
same card, and a self-reference draws an arrow back to its own card.

Double-click a member card or select it and choose **Pin member**. Turn on
**Pinned only** to display those members as independent roots. Pins resolve
their paths again at each stop. **Unavailable** counts paths outside the
current scope or snapshot. **Clear pins** removes all watches. Select a card
and hover over the canvas to see the full incoming and outgoing link labels.

Orange cards indicate a new value or a change to its value or direct links
since the previous source stop. Restart clears that comparison history.
The graph uses raw inspection. It does not execute closures or metamethods,
and it skips the implicit `_ENV` upvalue. A metatable link shows stored
references rather than the result of evaluating `__index`. The snapshot is
limited to 96 cards, 224 links, six expansion levels and 16 entries per table.

## Editor and project access

Read-only `editor.*` calls expose the captured text, selection, caret,
filename and lines. `project.*` reads the index while the original project
is active. Opening, saving and editing documents require the normal Lua script
runner. Restart reuses the original snapshot. Start a new debugger window to
pick up edits made after launch.
