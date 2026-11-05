# Python debugger

Open a Python buffer and choose **Scripts → Debug Current Python Buffer…**.
The debugger runs a snapshot of the current buffer, including unsaved changes,
in a separate Python 3 process. Set `GEANY_INSIGHT_PYTHON` to select an
interpreter. The original file is used as `__file__`, and its directory is
available for imports. The program runs normally, so it can modify files and
have other side effects. The **Console** tab shows Python's text output from stdout and stderr. When the program calls `input()` or `sys.stdin.readline()`, **Console** opens and accepts a line with **Enter**, including an empty line. **Send EOF** ends text input for the current run.

## Source controls

Click a line number to toggle a breakpoint. **F5** continues, **F10** steps over,
**F11** steps into, and **Shift+F11** steps out. **Pause** requests a stop on the
next traced line. **Shift+F5** stops the child process, and **Ctrl+F5** restarts
the original snapshot. **Go to Source** opens the line when the editor buffer
still matches the snapshot. The current file is the default entry point. Use **Scripts → Debug Python File…** to choose another. With a project open, the debugger follows runtime calls into files under its base directory. **Breakpoints in File…** lets you choose a file and set breakpoints before execution reaches it. Breakpoints are stored per file and restored the next time you debug. Each language keeps separate settings for the Geany project or, outside a project, for the entry file. Breakpoints in unnamed buffers last for the current debug session. Saved line numbers are restored as entered. **All Breakpoints…** lists the breakpoints across files and shows the total. Double-click or choose **Open Source** to jump to the line in the editor. Choose **Remove** to delete the selected breakpoint. The entry and edited open project modules run from snapshots. An imported module must already exist on disk to be found by the normal import search. Python loads other modules normally. Restart keeps the captured snapshots and clears the console.

## Values and watch graph

While paused, hover over the base name in `items[3]` or `config["name"]` to inspect the item when it is present in the graph snapshot. This supports simple unescaped string keys and nonnegative decimal indices without calling `__getitem__`.

While paused, hover over a variable name in the source pane to see a local or
global value in the current frame. **Stack and locals** shows the call stack.
**Watch graph** expands built-in dictionaries, lists, tuples and function
closure cells. Shared objects and cycles reuse one card. Double-click a member
to pin it, or select it and choose **Pin member**. **Pinned only** displays
those paths as roots. Orange marks a change since the previous stop.

The debugger reads built-in values directly. It does not evaluate properties,
arbitrary attributes or expressions for a hover tooltip. Other objects are
shown as opaque values. Use the graph to inspect built-in collection members.
