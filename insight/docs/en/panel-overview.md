# File overview

A miniature code map beside the editor.

## Controls

Click and drag to move the visible range. Ctrl+wheel changes scale; Shift+wheel takes larger steps. The context menu configures overview modes. Markers indicate bookmarks and changes.

## Short tutorial

Scroll a long file, add a bookmark and find its marker in Overview. Click the desired region to navigate there.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [panel-editor.md](panel-editor.md).

Function boundaries also support Python indentation (including async/multiline definitions and nested functions) and Lua nested end/until blocks. Current-function tracking chooses the smallest containing parsed function; comments/strings are masked. The same ranges drive Select Function and lexical Calls.

Ctrl+mouse wheel over the map changes its text scale (5–100%) across open maps without changing source font/zoom. The scale is saved with workbench settings. Native font/style refreshes and tab switches now revalidate every miniature style size and reset accidental map zoom; source text, selection and Undo are unchanged.
