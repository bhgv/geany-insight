# Scripting Geany Insight

The desktop editor embeds Lua 5.4.8. Extensions register commands, assign shortcuts and subscribe to events. Editor commands do not require an external Lua installation.

## First extension

Save this UTF-8 file as `my-commands.lua`:

```lua
commands.register("selection.wrap", "Wrap selection", function()
    if not editor.has("replace_selection") then
        editor.message("The document cannot be edited")
        return
    end
    editor.replace_selection("(" .. editor.selection() .. ")")
end, "<Primary><Alt>w")
```

Choose Scripts → Load extension… and select the file. Select a word in the editor and press Ctrl+Alt+W: it becomes `(word)`. The command also appears in Scripts → Commands and the Ctrl+Shift+P palette. Ctrl+Z undoes the edit.

Primary is the platform's main modifier. Check shortcut conflicts with Geany and plugins; use the menu when a shortcut conflicts.

## Loading, rerunning and state

Scripts → Run script… executes a file once in a fresh Lua state. Put registered commands in an extension and one-off edits in a script. Reload saved extension rereads the extension from disk; save it first. Unload extension disables it and its startup loading.

The extension path is remembered. Commands and handlers register at the next launch. Do not access the current document during loading: use editor/project inside command callbacks. Variables and closures persist until extension or editor reload.

## Working with text

```lua
commands.register("line.describe", "Describe position", function()
    local begin, finish = editor.selection_range()
    editor.message("Selection: " .. begin .. " .. " .. finish)
end)
```

Positions are zero-based UTF-8 byte offsets with an exclusive end; line numbers are one-based. Cyrillic characters occupy multiple bytes. `set_selection`, `replace_range` and `insert` validate UTF-8 boundaries.

`editor.text()` returns text; `selection()` returns selected text; `replace_selection(text)` replaces it. `filename()` returns a path or nil for an unnamed buffer. `goto_line(n)` navigates to a line. `save()` saves a named document; first save unnamed documents through the UI.

A command groups its edits into Undo. An error does not roll back completed edits: use Undo and fix the script. Avoid long work in callbacks; commands run on the UI thread.

## Events and index queries

See [Lua API 0.8](lua-api.md) for `events.on`, event names and `project.query`. Document events are observers: document mutation inside them is forbidden. Events raised while another Lua command runs are skipped to prevent recursion.

`editor.has(name)` checks host capabilities and document state. `INSIGHT_API_VERSION` is currently `"0.8"`; do not infer a specific method's availability from the version alone.

## Limits and errors

io, os, package and debug are disabled. External-file modules are not currently loaded. Extensions have 16 MiB of Lua memory, execution instruction budgets and a 256 KiB source-file limit. These are runtime limits, not a promise of strict C-host isolation.

Errors include a traceback. Fix and save the indicated file, then Reload saved extension. A failed load retains the previous working extension.

[API reference](lua-api.md), [tutorials](tutorials.md), [user guide](guide.md).

The bundled `workbench.lua` is in `../../examples/workbench.lua` relative to localized `insight/docs/en`. Windows portable archives also contain root `examples`. Load it through Scripts → Load extension… to list file symbols, navigate to the next occurrence of a selected name and report saves.
