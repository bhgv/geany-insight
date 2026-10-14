# Lua API 0.8

`INSIGHT_API_VERSION` is `"0.8"`. Check a host's actual capabilities with `editor.has(name)` inside commands. No document is available while loading; document methods report false, while `has("has")` always returns true.

## Commands and shortcuts

Scripts → Run script… executes once in a fresh Lua state. Scripts → Load extension… loads registered commands:

```lua
commands.register("selection.wrap", "Wrap selection", function()
    editor.replace_selection("(" .. editor.selection() .. ")")
end, "<Primary><Alt>w")
```

The optional fourth argument is a GTK accelerator string, at most 63 bytes. Primary is the platform's main modifier. Invalid shortcuts and duplicates within an extension are not assigned. Shortcuts do not integrate with Geany keyboard preferences; check plugin/Geany conflicts in the GUI. Primary+Shift+P is reserved for the command palette.

The palette searches titles/IDs case-insensitively; Enter runs the selection. Menus remain available without shortcuts.

IDs contain 1–63 ASCII characters: first a Latin letter, then letters, digits, `_`, `.` or `-`. Titles contain 1–255 bytes without NUL, preferably UTF-8; invalid UTF-8 displays the ID. There are at most 128 commands. Duplicate IDs and registration from callbacks are rejected.

Variables/closures persist between calls, not restarts. Successful loading replaces the extension; Lua or settings-write failure retains the old session. The path is remembered for startup. Reload saved extension rereads disk; Unload disables the extension and startup loading. Limits: 32 KiB settings, 16 KiB path, 256 KiB Lua file. Ordinary local files only. Startup registers functions; editor/project are unavailable during loading. Multiple modules are not currently supported. The built-in host stores `<configdir>/insight/lua-extension.conf`; the separate plugin uses `<configdir>/plugins/insight/extension.conf`. Format version 1 encodes the path in base64 (not encryption). An executing command prevents extension replacement/reload.

## Document and editing

Positions are **zero-based UTF-8 byte offsets**; lines are **one-based**.

| Method | Purpose |
|---|---|
| `editor.text()` | Complete document text |
| `editor.selection()` | Selected text or an empty string |
| `editor.replace_selection(text)` | Replace selection / insert at caret |
| `editor.position()` | Byte caret position |
| `editor.goto_line(line)` | Navigate with a Geany history entry |
| `editor.filename()` | UTF-8 filename or nil for unnamed document |
| `editor.open(path)` | Open absolute path; later operations use that document |
| `editor.save()` | Save named document; save unnamed documents through UI first |
| `editor.selection_range()` | Return start/end, with exclusive end |
| `editor.set_selection(start, end)` | Select at UTF-8 boundaries |
| `editor.line_count()` | Line count including an empty final line |
| `editor.line_text(line)` | Line with its line ending when present |
| `editor.replace_range(start, end, text)` | Replace a range without temporary selection changes |
| `editor.insert(position, text)` | Insert at a specified position |
| `editor.message(text)` | Status message, at most 4096 bytes |
| `editor.has(name)` | Capability/state check including read-only restrictions |

Android currently provides text/selection/replace_selection/has; additional methods are not connected to its frontend. Unsupported methods raise errors.

Text arguments cannot contain NUL. set_selection/replace_range/insert validate ranges; desktop also validates UTF-8 boundaries. New range insertions must be UTF-8; the older desktop replace_selection does not yet validate it. `string.upper` does not perform Unicode case folding.

One invocation groups edits into Undo. After editor.open, grouping continues in the new document; returning to a previous document creates another group. Errors do not roll back completed edits or Lua variables. Android one-off scripts run against a document copy, so an error does not change UI text.

Disabling io/os does not restrict editor.open/save operations supplied by the host.

## Events

```lua
events.on("document_save", function(event)
    if editor.has("message") then
        editor.message("Saved: " .. event.path)
    end
end)
```

Events: document_open, document_save, document_activate and document_close (before closure). Each event contains name/path; unnamed documents have an empty path. editor refers to the event document, which may differ from the active tab.

Handlers register only while loading, at most 32 per extension. They run in order; one error does not block later handlers. Each has its own instruction budget. Documents are read-only inside handlers: text/messages are permitted, edits, caret movement, opening/saving are forbidden. Notifications raised during another Lua command/handler are skipped rather than queued, preventing recursion without guaranteeing delivery. Android event dispatch is not connected yet.

## Project and navigation

`project.symbols(prefix, exact)` returns at most 256 records and a full match count. Fields: name, path, scope, kind, line, signature, type, inheritance. The final three are TagManager parser strings or empty when unavailable. inheritance is the raw base-type list, not semantic resolution. exact=true selects exact, case-sensitive names. Data is copied into Lua and may become stale.

`project.stats()` returns symbols and files_with_symbols, including open-document tags. files_with_symbols counts paths with at least one symbol, not all project files. The index is a snapshot from invocation start.

Legacy plugin identifier navigation is lexical: one result opens, multiple results appear in search. It does not semantically resolve overloads/namespaces. The legacy browser filters exact/prefix names, scope substrings and exact kind letters; it shows the first 500 rows. Up/Down selects, Enter opens, Escape closes. Its plain-text preview shows up to five neighboring lines, centers the target and prefers unsaved open buffers. Limits are 2 MiB source and 64 KiB preview; tag lines may lag edits. The native editor's Context/Relations interface is described in [the guide](guide.md).

## Limits and diagnostics

Approximately one million Lua instructions per load/command/individual event handler. Extensions have 16 MiB Lua memory over their lifetime; C-host/document/statistics allocations are outside that limit. Instruction hooks do not interrupt C functions. Synchronous operations can delay the UI; this is not a strict sandbox.

Libraries: base, string, table, math, utf8. No io/os/package/debug/coroutine, load/loadfile/dofile, print, collectgarbage or pcall/xpcall. Metatables are available but __gc is forbidden because Lua disables instruction hooks inside finalizers.

Execution errors include traceback; memory exhaustion may also prevent constructing it. Status messages are limited to 512 bytes. The C session is single-threaded and must not be destroyed during execution.

## Buffer occurrences and paging

`local matches, total = editor.occurrences(name, limit)` finds whole lexical identifiers in current text, including unsaved edits. limit defaults to 1000, range 1–10000. Fields: start/finish are zero-based half-open byte ranges; line is one-based; column is zero-based bytes. CR/LF/CRLF are supported. Limits: 2 MiB buffer and 1024-byte name. Check `editor.has("occurrences")`. Comments/strings are included; there is no semantic scope resolution.

`editor.occurrences(name, limit, offset)` optionally skips offset matches (default 0) before filling the table. total always counts the entire buffer. Negative offsets are rejected; beyond-end offsets return an empty table. Every call rereads the buffer, so edits can change paging. Names/text must be valid UTF-8 without NUL.

`project.files(prefix, limit, offset)` returns unique paths and full count. Defaults: empty prefix, limit=256, offset=0; maximum limit=1024. Only files with indexed symbols are included.

`project.symbols(prefix, exact, options)` accepts optional path (exact), scope (substring), kind (exact), limit (1–1024, default 256), offset (default 0). Pages retain the full count; the two-argument call remains supported.
