# Context

Definition previews without moving the main editor.

## Controls

Single-click a resolved link to change Context only. Double-click to open the shown file and center its line in the editor. History, definition selector, follow-caret, wrapping and search control the preview. Diff/Merge is on the main toolbar and in Tools.

## Short tutorial

Select a symbol in a list. Click another resolved definition in Context: the editor stays in place. Double-click to open the previewed line in the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [context-bookmarks.md](context-bookmarks.md).

Member definition links and editor Follow resolve nominal C/C++ typedef/using chains in declaration scopes, including `this` and qualified namespace receivers. Ambiguous/cyclic types decline. Template argument substitution, overload selection and arbitrary chained expressions are not inferred.

Explicit qualified nested types can be inherited: `Derived::Nested`, an alias-qualified inherited alias, and functions returning those types resolve to the recorded declaring owner. Direct nested declarations hide bases. The same declaration reached through a diamond is one type target; different declarations, a non-type declaration hiding the name, missing bases, cycles or limits decline. Base aliases resolve in the owner's declaration scope and eligibility is checked before choosing owners. Inherited nested lookup is bounded to 4096 visited owners, 256 bases per owner, the shared 64-level recursion and 32 alias expansions. Unqualified inherited type lookup in a class's lexical scope, template substitution, access and include/ODR semantics remain open.
