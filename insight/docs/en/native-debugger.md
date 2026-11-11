# C/C++ debugging with GDB

Build with debug symbols, for example `cc -g -Og main.c -o app` or
`c++ -g -Og main.cpp -o app`. Install GDB and make it available in
`PATH` before starting Geany.

Open **Tools → C/C++ → Debug C/C++ Executable…** and select the built program.
The C/C++ Debugger window opens with the executable directory as the
working directory.

![Controls and console in the C/C++ Debugger](../media/native-debugger.svg)

- Enter `main` or `file.cpp:42`, click **Add Breakpoint**, then **Run**.
  You can also use `break main` in the console.
- Use **Step Into**, **Step Over**, **Step Out**, **Continue**, and **Pause**
  to control execution. **Open Source** opens the current stopped line.
- **Stack** and **Locals** show the current frame information in **Stack and locals**.
  Enter GDB commands such as `print value` or `info threads` there as well.
  The command entry is for GDB. Programs that need terminal input require
  a separate terminal configured through GDB.

## Watches and graph

Open **Watches**, enter an expression such as `marker`, and select
**Add Watch**. GDB refreshes up to 32 expressions at each stop in the
current frame. Use **Remove Watch** with the same expression to remove it.
The **Watch graph** shares the Python and Lua graph controls. It shows GDB
expressions and Lua-published values, with pinning and change highlights
between stops. GDB expression values appear as cards with their text value.

## Lua observer

**Load Lua Observer…** accepts the same callback form as the Python and
Verilog observers. GDB breakpoint stops emit `native-point` with
`event.file`, `event.line`, `event.name` and the snapshot fields
`event.locals.breakpoint` and `event.locals.reason`. Here `event.name`
is the function. Read program locals with **Locals** or GDB commands.

```lua
local hits = 0
return function(event)
    if event.kind ~= "native-point" then return true end
    hits = hits + 1
    watch("Breakpoint hits", hits)
    log(event.name .. ":" .. event.line .. " hit " .. hits)
    return hits >= 2
end
```

`log()` writes to **Observer log** and **Console**. `watch()` updates
**Watches** and **Watch graph**.
Return `false` to continue or `true` to keep the program paused.
Manual pauses and stepping stops stay available for inspection.

## All thread stacks

While paused, open **All thread stacks** to collect a collapsible tree:
threads at the top, their frames underneath. **Expand all** and **Collapse all**
control the branches. Frame numbers, function names and source lines use distinct
colors. Double-click a frame to select its thread and frame, open its source,
and refresh locals, watches and the watch graph in that context.

This view collects at most 64 threads and 64 frames per thread. It refreshes
at stops while the tab is open; while running it keeps the previous snapshot
and labels it accordingly. A thread whose stack cannot be read is marked
**Stack unavailable**. GDB must supply source paths and line numbers for source
navigation; optimized code may lack locals or source locations.

![Thread and frame tree](../media/native-debugger.svg)

## Launch, pause, stop and restart

Before running, open **Launch settings**. Enter program arguments (quote values
containing spaces) and an existing absolute **Working directory**. An empty
argument field means no arguments. Invalid paths or unmatched quotes open this
tab with an explanation; the target is not started.

**Run** starts an inactive target. **Pause** interrupts it and preserves its
state. **Continue** resumes that state. **Stop** terminates the target while
keeping the debugger open. **Restart** terminates a running or paused target,
then starts a fresh process with the launch settings. Source breakpoints,
conditions, watches and the Lua observer stay configured. Variables, thread
stacks and the stopped-line highlight are cleared when the process exits.
The main editor toolbar also provides **Stop** for the active C/C++ session.

## Manage source breakpoints

**C/C++ breakpoints** lists the GDB snapshot, including points added with
**Add Breakpoint**, F2, the editor margin or GDB console commands. The table
shows the identifier, enabled state, condition, hit count and ignored hits.
Select a row to **Enable / Disable**, **Delete Breakpoint**, set a
**Breakpoint condition**, or configure **Ignore first hits**. An empty
condition makes the point unconditional; zero ignored hits stops on the next
matching hit. Conditions use C/C++ expressions evaluated by GDB. Invalid
expressions are reported by GDB and do not replace the previous condition.

Double-click a resolved point to open its source. Enabled points have red
margin dots, disabled points gray dots, and the current stopped line has a
background highlight. Stops with source coordinates open the editor
automatically. Unresolved points remain in the list until GDB can resolve
them (for example after loading a shared library). Source navigation and dots
require a full source path and line number from GDB. Only the session's own
markers are removed on debugger close.
