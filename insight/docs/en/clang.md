# Clang C/C++

Clang-enabled portable builds carry libclang, its shared runtime dependencies,
resource headers and redistribution notices. Neither the `clang` executable nor
Python is needed by the native frontend. Project headers, generated files and
external SDKs still come from the project.

## Project symbols and cache

The project index uses compiler-parsed C/C++ definitions when parsing succeeds.
Compiler symbol identities (USR), reference/call locations and dependencies are
stored in the existing project database. Unchanged files reuse cached results.
An included header or compilation-database change invalidates dependent files.
Sources with compiler errors retain the lexical indexing fallback.

The frontend reads `compile_commands.json` from the project root or `build/`.
Include paths are resolved against the compilation command's working directory;
defines, language standard and target are retained. Command strings are never
executed. Compiler plugins, response files, linking and code generation are not
imported. Without a database, default compiler settings and the project include
root are used. Diagnostics may therefore indicate missing build settings.

## Identifier coloring

**Navigate → Clang C/C++ → Clang C/C++ semantic colors** toggles identifier colors.
**Reanalyze C/C++ buffer with Clang** refreshes the active source and reports
symbol/token counts and the argument source. Types, functions, fields, constants,
variables, namespaces and macros use a separate foreground indicator; comments,
strings and source text retain the original lexer/editor behavior. Parsing runs
in a worker; painting is split into short idle batches. A changed buffer rejects
stale results. Named open files, including unsaved headers, are Clang overlays.

## Build and portable runtime

Meson uses `-Dclang=auto` for source builds. `-Dclang=enabled` requires matching
libclang headers/library and `llvm-config`. Disabled/minimal builds keep lexical
editing. `-Dclang-bundle=true` copies the SDK runtime, transitive dependencies,
resource headers and licenses into the portable installation. Linux bundling
requires `patchelf`. Windows DLL dependencies come from the same compiler SDK.
The native Linux and Windows portable workflows enable both options. Minimum
GTK 3.22 / GLib 2.56 remain unchanged. Autotools linked builds support
`--with-libclang=auto`, `yes`, `no` or an SDK prefix; portable bundling uses Meson.

Parsing is bounded to 2 MiB per source, 32 MiB of unsaved overlays, 200000 visited
cursors/tokens, 50000 declarations and 100000 relation records. Cancellation is
checked around the synchronous compiler parse and during traversal; a running
libclang parse is not force-interrupted. Missing declarations, project flags,
compiler extensions and incomplete files still limit semantic results. A compiler
frontend does not determine runtime condition values or arbitrary pointer targets.

**По-русски:** в переносимую поставку входят Clang/LLVM, зависимости и служебные
заголовки. Для проекта нужны его собственные заголовки и параметры сборки.
Положите `compile_commands.json` в корень проекта или `build/`. В «Навигации»
доступны семантическая подсветка и повторный разбор активного C/C++ исходника.
Неизменённые файлы используют базу. Изменение заголовка обновляет зависимые
исходники. Для неполного исходника сохраняется резервный лексический индекс.

Official APIs: [libclang](https://clang.llvm.org/docs/LibClang.html),
[compilation database](https://clang.llvm.org/docs/JSONCompilationDatabase.html).

## Semantic references and call graphs

**Navigate → Clang C/C++ → Clang references…** selects **References (Clang C/C++)** in Relations.
If the caret's analyzed identifier has a known USR, results use that exact compiler
identity, distinguishing overloads. Otherwise the status explicitly says the
query is by name. Text/comment searches remain separate lexical operations.
References reuse the project database when their sources and dependencies match;
open buffers are parsed for the query. Partial files contribute results with a
skipped/incomplete indication instead of being claimed as complete.

Call paths use compiler-resolved call occurrences and function bodies for fresh
Clang-indexed C/C++ sources, including unsaved open-source/header overlays.
Calls in an uninvoked lambda are not attributed to its enclosing function.
Virtual calls are omitted by default; uncertain mode displays the statically
referenced virtual target with a dashed edge, without claiming its runtime
implementation. Unresolved function-pointer targets remain unresolved. The graph
status distinguishes Clang-resolved calls from lexical fallback. Branch rendering
is still the bounded structural view described in [source routes](source-routes.md).

Project snapshots now allow up to 1 million records / 256 MiB per index and
768 MiB for the complete project snapshot, to accommodate reference records.
These storage bounds do not expand independent UI/report/work budgets.

Successful live C/C++ parses also publish compiler declarations into the native
TagManager through its existing asynchronous tag-publication path. File Symbols,
completion and native definition consumers therefore receive Clang-derived tags;
invalid/incomplete buffers keep their native parser fallback. The existing
workbench definition chooser prefers a fresh compiler identity where available.
Turning off semantic colors only changes painting; compiler analysis continues.

**По-русски:** результаты Clang поступают и в обычный механизм тегов редактора:
панель символов файла, подсказки и переходы получают разобранные компилятором
объявления. «Ссылки Clang…» доступны в «Навигации» и в «Поиск/замена» контекстного
меню. Выключение семантических цветов не выключает разбор символов.

The **Navigate → Clang C/C++** submenu groups coloring, reanalysis, semantic
references and this help page. The color preference persists in the Geany profile
and does not disable compiler-derived symbols or references.

Clang preserves function parameter names, macro parameters and underlying `typedef` types for completion and symbol information. Reanalysis with unchanged declarations does not rebuild the symbol panel. Portable-build tests parse a source using the bundled Clang resource headers.

## Semantic call trees and completion

**Navigate → Clang C/C++ → Callees / Callers (Clang tree)** opens a lazy Relations tree. Exact USRs distinguish overloads. Nodes retain call-site file, line and available target signature. Double-click opens the occurrence; expanding continues traversal. Repeated USRs stop recursive branches. Loaded branches also feed the existing graph, filtering, history and printing. Limits remain 200 occurrences per branch, 2000 nodes and depth 32. These are index snapshots: refresh after background analysis. Virtual edges show a static target, never a proven runtime implementation. Sources with errors are not treated as complete.

Contextual Clang completion runs after `.`, `->`, `::` and on explicit completion. It uses receiver types, inheritance, accessibility, overloads, compilation-database flags and named unsaved header overlays. A short debounce and at most two worker requests avoid parsing on every keystroke. Changed source, overlays, project or caret reject stale replies. Compiler failure retains native tag completion. Completion options can disable this path.

**Insert editable function parameter fields (Clang)** inserts the compiler-provided placeholders: replace them with expressions; Tab / Shift+Tab visits fields and Escape ends the session. These are editable parameter hints, not guessed argument values. The insertion is one Undo/Redo; existing arguments are preserved. Ordinary tags retain parentheses-only insertion. Spacing between inserted parameters is configurable and persisted.

Definition navigation also uses compiler target locations in headers outside project membership; declarations are available when no body exists. The ordinary reference command prefers fresh compiler analysis while lexical modes remain separately selectable.

![Semantic browsing](../media/semantic-browsing.svg)

Ambiguous function names produce separate selectable roots with signatures and file locations. Snapshot preparation runs in a worker. A newly published parse refreshes tracked Relations and Context even when declarations are unchanged. Calls through pointers are not presented as resolved direct function calls.
