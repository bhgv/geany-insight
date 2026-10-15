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

Explicit qualified nested types can be inherited: `Derived::Nested`, an alias-qualified inherited alias, and functions returning those types resolve to the recorded declaring owner. Direct nested declarations hide bases. The same declaration reached through a diamond is one type target; different declarations, a non-type declaration hiding the name, missing bases, cycles or limits decline. Base aliases resolve in the owner's declaration scope and eligibility is checked before choosing owners. Inherited nested lookup is bounded to 4096 visited owners, 256 bases per owner, the shared 64-level recursion and 32 alias expansions. Unqualified nominal type names search the nearest class and its inherited types before falling back to enclosing scopes. A direct local type declaration wins, while a hidden or uncertain inherited name declines. Qualified chains select the nearest first component, then stay within that owner: a missing/hidden member does not reopen outer lookup. Reopened namespace declarations share one nominal namespace; namespace members keep use-file preference while class members use declaration context. All attempted alias expansions count toward the work limit, while completed unsuccessful probes do not inflate the displayed alias count. Template substitution, access, include/ODR semantics and exact positional scope for several functions on one source line remain open.

C++ variables whose declaration starts with global `::` qualification are indexed with that prefix retained in the recorded type. Namespace-scope, member and local declaration parsing use the same rule; qualified calls or assignments are not converted into variable declarations.

Nominal function-result lookup respects the callable’s own template parameters: a dependent `T` does not select an unrelated global class. Explicit `::T` and ordinary results use declaration context; value parameters and body-local aliases do not change the result type. Template argument deduction and overload selection remain unsupported.


Full reference: [Search and replacement modes and query languages](search-replace.md).

## Compact right-hand Context tools

Search and definition candidates start hidden. Icon-only commands form a vertical
rail to the right of the source; hover for each command's full name. The rail
scrolls when the preview is short.

| Icon | Action |
| --- | --- |
| Left / right arrow | Previous / next Context history location |
| Open document | Open the preview caret line in the editor; also Enter |
| Magnifier | Show/hide preview search; Ctrl+F in Context opens and focuses it |
| List | Show/hide indexed definition candidates; selection changes Context only |
| Target | Toggle Follow: track the main editor caret |
| Justified lines | Toggle wrapping of long preview lines |
| Copy | Copy the preview caret address as `file:line` |
| Question mark | Open this help page |

Hiding search/candidates preserves their query/model and the editor position.
Press again to reveal them. F3/Shift+F3 repeats search. Enter in source opens its
line; F12 follows a definition inside Context. Double-click opens the editor.

On first launch both side areas open about 100 logical pixels and Context uses
approximately one quarter of the working height. Drag splitters down to a narrow
strip; subsequent launches restore the last positions.

## Checked Python navigation

Function/class scopes use indentation ranges from the captured source, including
nested functions and return to an outer scope after a nested suite ends. Locals
and parameters from sibling functions are excluded; visible closure names remain
available. A method's first `self`/`cls` parameter selects its indexed class, not
another class's same-name field. Nested closures retain that receiver unless a
nearer parameter or local assignment shadows it. A local assignment also hides
an outer receiver before the assignment line. An ordinary nested function's
first parameter is not automatically a class receiver. Directly named classes and indexed annotated
receivers are supported. Preview links, F12 and asynchronous Follow share these
rules. `global name` restricts lookup to the same module; `nonlocal name` selects
an enclosing function binding and excludes module and intervening class members.
These declarations affect their complete suite. Local names also bind the whole
function, so navigation can point to a later assignment rather than an unrelated
outer definition. An unresolved `nonlocal` has no fallback to a module variable.
Declaration parsing supports comma lists, semicolons and explicit line
continuations, ignores strings/comments, and declines malformed/conflicting
statements or exceeded bounds (64 binding statements including imports, 4096 declaration names,
32 MiB scan budget).
The declaration cache is invalidated with the source/index snapshot.

Explicit imports resolve against indexed modules: `from models import work as
task`, `import models as mod` with `mod.work`, and imported classes such as
`from models import Thing as Alias` with `Alias.field`. Nested local imports and
`global`/`nonlocal` binding rules are respected. Module lookup requires a unique
indexed `models.py` or `models/__init__.py` suffix; an explicit relative module
such as `.models` uses the source directory (further leading dots climb parents).
No module is executed or automatically added to the index. Ambiguous module
paths, conflicting aliases and missing targets produce no unrelated fallback.
Local assignments/parameters shadow imports; assignment order and runtime types
are not inferred. Lists, aliases, parenthesized lists and explicit continuations
are parsed, with at most 256 imported names per statement.

F12, links and Follow share this lookup. The destination highlights the real
symbol name even when the source uses an alias. Python index publications refresh
Follow even with an unchanged caret, so an imported definition moving in another
open buffer is reflected. Module paths are cached only for that index snapshot.

This is bounded lexical resolution, not Python execution. Dynamic attributes,
arbitrary chains/call results, wildcard imports, re-export chains, decorators
and general type inference are not evaluated. Dotted imports without an alias
(`import pkg.mod`) and implicit `sys.path`/package execution rules are not
resolved. An unrelated name in another Python file is not an
import. An unknown receiver does not fall back to a random same-name field.
Comments and strings do not become links.

Simple assignments to `self.field` or `cls.field` in a direct class method now
create indexed field definitions in that class, including an annotation when
present (`self.field: Type = value`). The receiver must be that method's first
conventional `self`/`cls` parameter. Arbitrary objects, chained attribute writes,
unconventional receiver names and ordinary nested functions do not create guessed
class fields. Plain `@staticmethod` is recognized even when decorator metadata
output is disabled: it neither declares fields through its parameter nor treats
that parameter as an implicit instance receiver. Other decorator semantics and
general data-flow type inference remain open.

A receiver assigned exactly once as `obj = LocalClass(...)` can use that indexed
same-file class for field navigation. Arguments may span lines. The whole
right-hand side must be that one call; competing bindings, reassignment,
parameter rebinding, factory functions, constructor aliases/imports, chained or
compound expressions are not inferred. This is a nominal source rule, not
execution of `__new__` or flow analysis. Results belong to the source/index
snapshot; changing the constructor expression invalidates the preview cache even
when indexed names and declaration lines stay the same.

Annotation-only declarations (`name: Type`, class fields and `self.field: Type`)
are indexed too. They supply a declaration/type for navigation without implying
that Python created a runtime value or attribute.

The first formal parameter of a direct method may have any identifier (for
example `me`); its position and declaring class determine the receiver. Known
`builtins.staticmethod` and plain `staticmethod` suppress it, including stacks
containing multiline decorator calls. Arbitrary decorator transformations remain
unsupported.
