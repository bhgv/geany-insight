# Calls and function tracking

Open View → Panels → Relation Window, or use the blue connected-nodes toolbar button.

* Calls (current function, lexical): enter a parsed function name from the active file and Search. Shows identifier-plus-parenthesis expressions in its body. Single click previews the call site in Context; double click opens it in the editor.
* Call sites (project, lexical): searches supported source files for the identifier followed by `(`. Current buffer only is available; open unsaved buffers are read through the existing search pipeline.
* Follow symbol in editor + Track enclosing function: follows the smallest containing complete brace-bodied function, including positions on whitespace. Turning enclosing tracking off restores identifier tracking. The option and relation mode are saved.

This initial implementation uses lexical masks, not semantic call graphs. Calls excludes language control keywords and masked comments/strings; macros are included. Templates, operator calls and indirect function pointers are incomplete. Nested function declarations and prototypes can appear as lexical call sites. Overloads are not resolved; for duplicate definitions in the current file, put the cursor inside the desired body. Calls caps results at 512 and buffers at 2 MiB. Function tracking requires a supported brace language; Python/Lua indentation or end-delimited bodies are not supported yet. Project call-site searches retain existing result/file/work limits and Stop behavior.

Toolbar icons are original Cairo drawings with blue accents and theme-colored outlines; they do not require an external image loader. The internal merge window also has a toolbar shortcut.
