# Calls and function tracking

Open View → Panels → Relation Window, or use the blue connected-nodes toolbar button.

* Calls (current function, lexical): enter a parsed function name from the active file and Search. Shows identifier-plus-parenthesis expressions in its body. Single click previews the call site in Context; double click opens it in the editor.
* Call sites (project, lexical): searches supported source files for the identifier followed by `(`. Current buffer only is available; open unsaved buffers are read through the existing search pipeline.
* Follow symbol in editor + Track enclosing function: follows the smallest containing complete brace-bodied function, including positions on whitespace. Turning enclosing tracking off restores identifier tracking. The option and relation mode are saved.

This initial implementation uses lexical masks, not semantic call graphs. Calls excludes language control keywords and masked comments/strings; macros are included. Templates, operator calls and indirect function pointers are incomplete. Nested function declarations and prototypes can appear as lexical call sites. Overloads are not resolved; for duplicate definitions in the current file, put the cursor inside the desired body. Calls caps results at 512 and buffers at 2 MiB. Function tracking requires a supported brace language; Python/Lua indentation or end-delimited bodies are not supported yet. Project call-site searches retain existing result/file/work limits and Stop behavior.

Toolbar icons are original Cairo drawings with blue accents and theme-colored outlines; they do not require an external image loader. The internal merge window also has a toolbar shortcut.

Result filtering matches symbol, file and source text using Unicode case folding. It does not rerun the project search. Column sorting remains available. Definitions and Call sites buttons query the selected result, disable editor tracking and clear the display filter. Definition queries show all indexed candidates instead of guessing an overload. Single click still previews, and double click opens the result location.

The Back arrow restores the previous drill-down query, relation mode, display filter and buffer/code options. Up to 32 drill-down steps are kept for this editor session; editor tracking stays off while browsing this history.

## Graph view

Enable Graph view in Relations. This shows the existing filtered and sorted result model as a one-level graph under the current query. Horizontal/Vertical controls change layout; zoom ranges from 50% to 200%. The first 128 visible results are drawn; the footer reports the full count and the table remains available for all results. The view choice is saved with workbench settings. Layout and zoom are also saved with workbench settings.

Click a node for Context; double click or Enter opens its location in the editor. Arrow keys, Home and End select and scroll to result nodes. Definitions, Call sites, Back and the result filter use the same selection in either view. Nodes show symbol and file:line; blue borders and connectors are original Cairo drawings using the GTK theme text color.

This is an initial graph of search results, not a recursively expanded semantic call tree. Links represent the active relation query, not proven runtime call edges. Existing lexical/tag limitations apply.

Copy image (or Ctrl+C with graph focus) copies the complete currently drawn graph to the clipboard. Save PNG… exports a snapshot taken before the file chooser opens, using the same renderer, scale, selection and model order. Unicode paths use GLib file I/O. Existing files require the usual overwrite confirmation. Images larger than 16384 pixels on either axis or 32 million pixels total are rejected before allocating a raster; reduce zoom or filter the results. Export includes the first 128 results shown by the graph, not the remaining table rows.
