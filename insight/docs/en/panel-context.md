# Context

Definition previews without moving the main editor.

## Controls

Single-click a resolved link to change Context only. Double-click to open the shown file and center its line in the editor. History, definition selector, follow-caret, wrapping and search control the preview. Diff/Merge is on the main toolbar and in Tools.

## Short tutorial

Select a symbol in a list. Click another resolved definition in Context: the editor stays in place. Double-click to open the previewed line in the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [context-bookmarks.md](context-bookmarks.md).

Member definition links and editor Follow resolve nominal C/C++ typedef/using chains in declaration scopes, including `this` and qualified namespace receivers. C++ namespace aliases (`namespace Short = Existing;`) and their recorded targets are imported into the native index, including chained and explicitly global targets. Nested nominal owners can also pass through aliases, such as `Alias::Nested` and alias-qualified nested aliases; alias targets use their declaration scope. Total alias expansion is bounded to 32 and nested resolution to 64; reusing an owner alias is allowed, while active expansion cycles decline. Ambiguous/cyclic types decline. Indexed inherited members and nominal field chains such as `object.child->value`, `this->child->value` and `Alias::shared->value` also resolve. A declaration hides the same name on its base branch; matches from different declaring scopes decline, while overloads in one scope remain in the selector. Repeated paths to the same declaring scope share one navigation target. Missing bases, cycles and bounds (32 inheritance/receiver levels, 4096 scopes or same-name candidates) decline safely. C-family call results also resolve, such as `factory()->value`, `object.get()->value` and alias-qualified static methods, when every visible callable candidate has the same indexed nominal return owner. Inherited unqualified methods take precedence over enclosing namespace functions; local callable objects/function pointers and indexed macros decline. Strings and comments are masked from the start of the source snapshot; Context reuses that mask. Backward call scanning is bounded to 4096 bytes and a 256-bracket stack, so a long comment cannot introduce a false callee. Editor snapshots are limited to 2 MiB; unavailable or invalid snapshots decline. Calls use the captured C/C++/CUDA parser profile, including custom extensions. Argument-type/overload selection, template substitution, access checking, callable-object results and indexed receivers remain unsupported.

When an editor caret/edit update arrives before the editor receives focus, Context Follow resumes on the subsequent focus-in even if the caret has not moved again. Turning Follow off manually and returning at an unchanged caret without an intervening edit preserves that manual setting.
