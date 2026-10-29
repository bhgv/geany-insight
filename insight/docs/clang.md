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

**Navigate → Clang C/C++ semantic colors** toggles identifier colors.
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
