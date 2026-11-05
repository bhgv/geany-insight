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

## Debug a Lua project

The current Lua file is the default entry point. Use **Scripts → Debug Lua File…** to choose another. In an open project, `require("net.codec")` looks for `net/codec.lua` or `net/codec/init.lua` under the project base directory. On first load the debugger snapshots the module, including unsaved edits in an open tab. Steps and breakpoints work in the entry and loaded modules. Use **Breakpoints in File…** to set line breakpoints before a module is reached. Breakpoints are stored per file and restored the next time you debug. Each language keeps separate settings for the Geany project or, outside a project, for the entry file. Breakpoints in unnamed buffers last for the current debug session. Saved line numbers are restored as entered. **All Breakpoints…** lists the breakpoints across files and shows the total. Double-click or choose **Open Source** to jump to the line in the editor. Choose **Remove** to delete the selected breakpoint.

The debug VM loads text modules from the project. System package search and native Lua modules are unavailable in this VM. Restart keeps loaded snapshots. Start a new session to pick up edited module sources.

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

## Stop conditions and Lua observers

Open **Conditions and Script…** while paused. Enter `event.locals.counter == 10`
to stop before the next source line when the local reaches 10. For a global,
use `event.globals.counter == 10`. Clear the field to disable the condition.

The lower field accepts a Lua chunk returning an event handler. The handler receives
`line`, `call` and `return` events with `kind`, `file`, `line`, `name`,
`locals`, `args` and `globals`. The name may be empty. Value tables contain bounded
copies of scalar values. Returning `true` requests a stop at the next yieldable line.
**Observer log** refreshes at stops and at exit. A callback error disables the observer
and appears in the log. The condition and script belong to the current debugger window.
Restart resets the callback state and log.

This example logs arguments to `tick` and waits for `1` followed by `2`.

```lua
local previous
return function(event)
    if event.kind ~= "call" or event.name ~= "tick" then
        return
    end
    local counter = event.args.counter
    log("tick(" .. tostring(counter) .. ")")
    local stop = previous == 1 and counter == 2
    previous = counter
    return stop
end
```
