# Context

Definition previews without moving the main editor.

## Controls

Single-click a resolved link to change Context only. Double-click to open the shown file and center its line in the editor. History, definition selector, follow-caret, wrapping and search control the preview. Diff/Merge is on the main toolbar and in Tools.

## Short tutorial

Select a symbol in a list. Click another resolved definition in Context: the editor stays in place. Double-click to open the previewed line in the editor.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [context-bookmarks.md](context-bookmarks.md).

Member definition links and editor Follow resolve nominal C/C++ typedef/using chains in declaration scopes, including `this` and qualified namespace receivers. Ambiguous/cyclic types decline. Template argument substitution, overload selection and arbitrary chained expressions are not inferred.

Explicit qualified nested types can be inherited: `Derived::Nested`, an alias-qualified inherited alias, and functions returning those types resolve to the recorded declaring owner. Direct nested declarations hide bases. The same declaration reached through a diamond is one type target; different declarations, a non-type declaration hiding the name, missing bases, cycles or limits decline. Base aliases resolve in the owner's declaration scope and eligibility is checked before choosing owners. Inherited nested lookup is bounded to 4096 visited owners, 256 bases per owner, the shared 64-level recursion and 32 alias expansions. Unqualified nominal type names search the nearest class and its inherited types before falling back to enclosing scopes. A direct local type declaration wins, while a hidden or uncertain inherited name declines. Qualified chains select the nearest first component, then stay within that owner: a missing/hidden member does not reopen outer lookup. Reopened namespace declarations share one nominal namespace; namespace members keep use-file preference while class members use declaration context. All attempted alias expansions count toward the work limit, while completed unsuccessful probes do not inflate the displayed alias count. Template substitution, access, include/ODR semantics and exact positional scope for several functions on one source line remain open.

C++ variables whose declaration starts with global `::` qualification are indexed with that prefix retained in the recorded type. Namespace-scope, member and local declaration parsing use the same rule; qualified calls or assignments are not converted into variable declarations.
