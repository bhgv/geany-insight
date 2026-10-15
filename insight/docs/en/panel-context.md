# Context

Definition previews without moving the main editor.

## Controls

Single-click a resolved link to change Context only. Double-click to open the shown file and center its line in the editor. History, definition selector, follow-caret, wrapping and search control the preview. Diff/Merge is on the main toolbar and in Tools.

## Short tutorial

Select a symbol in a list. Click another resolved definition in Context: the editor stays in place. Double-click to open the previewed line in the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [context-bookmarks.md](context-bookmarks.md).

Member definition links and editor Follow resolve nominal C/C++ typedef/using chains in declaration scopes, including `this` and qualified namespace receivers. Ambiguous/cyclic types decline. Indexed inherited members and nominal field chains such as `object.child->value`, `this->child->value` and `Alias::shared->value` also resolve. A declaration hides the same name on its base branch; matches from different declaring scopes decline, while overloads in one scope remain in the selector. Repeated paths to the same declaring scope share one navigation target. Missing bases, cycles and bounds (32 inheritance/receiver levels, 4096 scopes or same-name candidates) decline safely. Template argument substitution, overload selection, access checking, function-call results and indexed receiver expressions are not inferred.
