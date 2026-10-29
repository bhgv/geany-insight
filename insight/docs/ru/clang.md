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

Clang сохраняет имена параметров функций, параметры макросов и исходный тип `typedef` для подсказок и справки. Повторный разбор без изменения объявлений не перестраивает панель символов. В переносимой сборке проверяется разбор с заголовками из включённого комплекта Clang.

## Семантические деревья и автодополнение

«Навигация → Clang C/C++ → Вызываемые/Вызывающие функции» открывает ленивое дерево в «Связях». Перегрузки различаются по USR. Узел показывает место вызова, файл, строку и доступную сигнатуру цели. Двойной щелчок открывает именно вызов; раскрытие продолжает обход. Повторный USR останавливает рекурсивную ветвь. Те же загруженные ветви доступны в графическом виде, фильтре, истории и печати. Пределы: 200 записей на ветвь, 2000 узлов, глубина 32. Это снимок индекса: обновите после завершения фонового разбора. Виртуальные вызовы обозначают статическую цель, а не фактический объект времени выполнения. Файлы с ошибками не объявляются полностью разобранными.

Контекстное автодополнение Clang работает после `.`, `->`, `::`; явная команда дополнения также запрашивает компилятор. Учитываются текущая позиция, тип объекта, наследование, доступность членов, перегрузки, compile_commands.json и несохранённые открытые заголовки. Запрос выполняется в фоне после короткой паузы; одновременно работают не более двух запросов. Изменённый исходник, заголовок, проект или позиция отменяют публикацию устаревшего ответа. При отказе Clang используется прежний механизм тегов. Выключить можно в «Параметры автодополнения».

Там же «Вставлять редактируемые поля параметров функций» включает поля, которые предлагает Clang: замените их выражениями; Tab / Shift+Tab переключает поля, Escape завершает режим. Это места для ввода, не угаданные значения аргументов. Вставка — один Undo/Redo, существующий список аргументов не дублируется. Для обычных тегов без полей сохраняется вставка скобок. Параметр пробелов между аргументами сохраняется в профиле.

Переход к определению использует цель Clang и для заголовков вне manifest проекта; при отсутствии тела доступно объявление. Обычная команда поиска ссылок предпочитает Clang при наличии текущего разбора; лексические режимы доступны отдельно.

![Semantic browsing](../media/semantic-browsing.svg)

Если имя соответствует нескольким функциям, дерево показывает отдельные корни с сигнатурами и файлами. Подготовка снимка выполняется в фоне. Публикация нового разбора обновляет автоматически отслеживаемые связи и контекст даже без изменения списка объявлений. Вызов через указатель не выдаётся за прямой вызов функции.

Список подробного автодополнения объединяет символы и подходящие по языку шаблоны активного хранилища Clips (общего или проекта). У шаблонов отдельные значок и тип строки; одинаковые имена символа и шаблона не сливаются. Выбор использует прежний диалог параметров и один Undo, отмена диалога сохраняет введённый префикс. После `.`, `->`, `::` шаблоны не примешиваются к членам объекта. Опция «Предлагать шаблоны Clips» сохраняется в профиле. Штатные текстовые snippets Geany по Tab остаются отдельным механизмом.

Кнопки со стрелками вверх/вниз в навигации «Связей» начинают поиск вызывающих/вызываемых от выбранного узла с сохранением его точного USR. Назад/вперёд возвращают именно эту перегрузку, даже если курсор исходника уже стоит на другой одноимённой функции.
