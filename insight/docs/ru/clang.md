# Clang C/C++

> **Clang по умолчанию:** внутренний SDK из `external/llvm-project`, собранный в `_external-clang-sdk`. Системный вариант включается только явно: `-Dclang-provider=system` или `--with-clang-provider=system`. При отсутствии внутреннего SDK подмены системным нет. C++ API обслуживает разбор, символы, подсветку, препроцессор и Sema; libclang не требуется.


Clang-enabled portable builds carry clang-cpp, LLVM and their shared dependencies,
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

Prepare the pinned internal SDK once, and rebuild after editing Clang:

```sh
git submodule update --init --depth 1 external/llvm-project
python3 insight/tools/build_external_clang.py --jobs 2
meson setup build-editor
meson compile -C build-editor
meson install -C build-editor
```

No provider or SDK option is needed for a normal source build. An internal SDK
elsewhere uses `-Dclang-sdk=/absolute/prefix` or `--with-clang=/absolute/prefix`.
Its source manifest must match the pinned revision; its own headers and exact
library paths are selected together.

| Provider | Meson | Autotools |
| --- | --- | --- |
| Internal SDK (default) | `meson setup build-editor` | `./configure` |
| System Clang (explicit) | `-Dclang-provider=system` | `--with-clang-provider=system` |
| Minimal lexical build | `-Dclang=disabled` | `--without-clang` |

Internal Linux/Windows Meson installs automatically carry the SDK runtime,
resource headers and licenses. A system-provider portable installation uses
`-Dclang-bundle=true`. Linux bundling needs `patchelf`. Autotools internal builds
link the selected SDK directly and retain its runtime path on Unix; use Meson
for a self-contained portable installation. Minimal builds need no SDK.
GTK 3.22 / GLib 2.56 remain the minimum versions.

Parsing is bounded to 2 MiB per source, 32 MiB of unsaved overlays, 200000 visited
cursors/tokens, 50000 declarations and 100000 relation records. Cancellation is
checked around the synchronous compiler parse and during traversal; a running
Clang C++ parse is not force-interrupted. Missing declarations, project flags,
compiler extensions and incomplete files still limit semantic results. A compiler
frontend does not determine runtime condition values or arbitrary pointer targets.

**По-русски:** в переносимую поставку входят Clang/LLVM, зависимости и служебные
заголовки. Для проекта нужны его собственные заголовки и параметры сборки.
Положите `compile_commands.json` в корень проекта или `build/`. В «Навигации»
доступны семантическая подсветка и повторный разбор активного C/C++ исходника.
Неизменённые файлы используют базу. Изменение заголовка обновляет зависимые
исходники. Для неполного исходника сохраняется резервный лексический индекс.

Official APIs: [LibTooling](https://clang.llvm.org/docs/LibTooling.html),
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

Автоматическое слежение окна «Контекст» также выбирает определение по USR Clang, включая внешние заголовки. Предпросмотр сохраняет фоновые проверки снимка и отбрасывает ответ, если разбор был заменён или инвалидирован. При отсутствии свежего успешного разбора остаётся прежний механизм тегов.

## Настройки для заголовков C/C++

Заголовок часто не имеет собственной строки в `compile_commands.json`. Если точной команды нет, Clang использует настройки исходника-компаньона: сначала одинаковое имя без расширения, затем близость каталогов. Для `.hpp`, `.hh`, `.hxx` выбираются только команды C++; при равнозначных C и C++ для `.h` выбор отклоняется и явно отмечается неоднозначность. Равнозначные команды одного языка упорядочены по полному имени файла — перестановка строк JSON не меняет выбор. Подбор ограничен 8192 командами. Это эвристика, а не доказательство включения заголовка выбранным исходником; точная команда заголовка имеет приоритет.

Сохраняются стандарт языка, defines, целевая архитектура и include-пути. Относительные пути считаются от `directory` команды, даже если база лежит в `build/`. Язык драйвера `clang++` / `g++` сохраняется, а явный `-x` имеет приоритет. Одинаковые настройки используются для индекса, связей, подсветки и автодополнения заголовка. Изменение базы или подключённых заголовков инвалидирует прежний снимок.

**Навигация → Clang C/C++ → Сведения о разборе Clang…** показывает язык, точный или подобранный источник настроек, ошибки, пределы и сообщения компилятора. Текст отчёта можно выделить и скопировать. При проблеме с графом или отсутствующими символами проверьте этот отчёт и при необходимости добавьте точную команду заголовка в базу.

При неоднозначности языка `.h` результат явно помечается неполным и не заменяет обычный индекс символов. Переходы, графы и автодополнение используют резервный механизм, пока в базе не появится подходящая точная команда. Отсутствие ошибок стандартного C-разбора не считается подтверждением правильности настроек C++ проекта.

Высота всплывающего списка задаётся его прокручиваемой области, с учётом размера шрифта, числа строк и рабочей области экрана. Проверка GUI требует видимости обоих вариантов перегрузки целиком; одного наличия строк в модели недостаточно.


## Открытые файлы без проекта / Open files without a project

Без сохранённого проекта открытые **именованные** файлы образуют временную
рабочую область в памяти. В ней доступны ссылки, деревья вызывающих/вызываемых,
цепочки вызовов и проверка исходников ИИ. Учитываются несохранённые изменения
и открытые заголовки. Неоткрытые файлы из соседних папок в область не добавляются;
Clang по-прежнему читает заголовки, подключённые директивами `#include`.
Закрытый файл исключается из следующего снимка. База проекта и `.geany` не создаются.
Безымянным буферам сначала нужно назначить имя файла.

Without a saved project, named open files form an in-memory source workspace for
references, caller/callee trees, call paths and AI source review. Unsaved edits
and open headers participate. Neighboring unopened sources are not members;
Clang can still read headers included by the source. Closing a file removes it
from the next snapshot. No project file or project database is written. Untitled
buffers need a filename first. At most 256 files are captured; excess files are
marked as an incomplete snapshot. Existing parser/overlay byte limits still apply.

If Clang commands are greyed out, that executable was built without the native Clang C++ backend.
Installing a `clang` executable afterwards cannot enable a compiled-out backend.
**Clang analysis details…** now opens an explanation even in a minimal build;
**Reanalyze** reports missing support or an unsupported file instead of silently
returning. Only the explicitly selected system provider uses installed packages such as `libclang-dev libclang-cpp-dev llvm-dev` on Debian/Ubuntu; the default uses the source SDK.
For intentional minimal builds use `-Dclang=disabled` or `--without-clang`;
`auto` remains an explicit opt-in to optional detection. For portable installs,
keep `-Dclang-bundle=true` so target systems do not need system Clang C++ libraries.


## Editable source and direct C++ AST adapter

The pinned LLVM/Clang source is now in `external/llvm-project` (Git submodule). Build it with `python3 insight/tools/build_external_clang.py`; pass the resulting prefix as `-Dclang-sdk=... -Dclang-cpp=enabled`. Direct C++ AST call edges now feed caller/callee trees and call paths. The same AST supplies symbol-list declarations, semantic tokens, references, function definitions and body spans. Sema supplies completion, and the C++ preprocessor supplies macros and include dependencies. See [parser-engines.md](parser-engines.md) for exact scope, source build instructions and other-language candidates.

## Ссылки в выражениях через C++ AST

При включённом `clang-cpp` прямой проход C++ AST теперь собирает не только вызовы, но и ссылки `DeclRefExpr` и `MemberExpr`. USR различает одноимённые переменные во вложенных областях и перегруженные функции. Диапазон поля покрывает его имя, а не всё выражение с объектом. Смещения хранятся в байтах, колонки для отображения — в символах UTF-8. Эти записи используются существующими переходами и поиском использований.

Обновление атомарное: при отмене, ошибках разбора или превышении лимитов остаётся прежний результат. Макросы поступают из записей C++ препроцессора. Неразрешённые зависимые и динамические конструкции не объявляются доказанными связями; отдельно доступен лексический поиск. Списки символов, подсветка, ссылки, определения и границы тел собираются прямым AST, а автодополнение — Sema. Второго C frontend-а нет. Это следующий этап переноса, а не замена всех сервисов компилятора. Кеш проекта однократно обновляется для новой версии ссылок.

### Вычисление захватов лямбд

Прямой проход AST различает инициализаторы захватов и отложенное тело лямбды. В `auto task = [value = load()] { return work(value); };` у внешней функции есть прямой вызов `load`, но нет прямого вызова `work`. Обе ссылки остаются доступны для поиска. Вложенные отложенные тела также не создают ложных связей с внешней функцией. Момент вызова сохранённой лямбды не предсказывается.

## Переходы к определениям через C++ AST

Для разрешённых ссылок и вызовов прямой проход Clang теперь создаёт записи мест назначения. При наличии определения функции или переменной в текущей единице трансляции выбирается оно; иначе сохраняется переход к объявлению. Перегрузки различаются по USR, метод вне класса ведёт к его телу, поле — к объявлению в классе. Несохранённые открытые заголовки учитываются вместе с текущим буфером исходника.

Одна запись на USR заменяет прежние места назначения этого символа атомарно вместе со ссылками. При ошибке разбора, отмене или превышении лимитов остаётся прежний результат. Макросы обрабатываются C++ препроцессором. Для сгенерированных мест без редактируемой позиции исходника не создаются вымышленные переходы. Определения в других единицах трансляции по-прежнему ищутся в общем индексе проекта или временного набора открытых файлов. Этот проход не компилирует и не исполняет проект.

## Типы, определения функций и точность графа

Прямой C++ AST собирает ссылки на структуры, классы, перечисления, typedef/using, шаблоны классов, параметры типов и квалификаторы пространств имён. Переход от использования структуры предпочитает полное определение перед предварительным объявлением. Зависимые и неразрешённые формы сохраняют прежний запасной путь. Это не обещает разрешения всех возможных C++-типов.

Определения обычных функций и их диапазоны от `{` до `}` теперь доступны из этого же прохода. Сохраняются точные USR, признак внутренней видимости, подписи параметров и физические координаты тела. Списки символов и метаданные функций используют общий форматтер параметров для сопоставления цепочек вызовов. Неподдерживаемые формы тел не выдаются за разрешённые составные блоки.

В `sizeof(sink())`, `noexcept(sink())` и `decltype(sink())` сохраняется ссылка на `sink`, но не создаётся ложный исполняемый вызов. Учитываются вычисляемость `typeid` и особый случай VLA в C `sizeof`. Инициализатор параметра по умолчанию не считается вызовом из тела объявляемой функции; анализ не предсказывает его динамический вызов. Как и раньше, тело отложенной лямбды не приписывается внешней функции.

Обычный frontend строит один AST и создаёт новые снимки символов, ссылок и цветов. Канонические USR кешируются на время обхода, повторные посещения одного места вызова устраняются. В отчёте «Clang analysis details» перечислены задачи AST, препроцессора и Sema. Повторного разбора C frontend-ом нет.

В Debian/Ubuntu пакет `libclang-dev` содержит заголовки Clang C++. Его название не означает, что редактор линкует C-библиотеку libclang. Установите также `libclang-cpp-dev llvm-dev` либо соберите закреплённый SDK из исходников.

Многострочные параметры макросов поддерживают переносы LF и CRLF. В списке ссылок показывается строка вызова, а номер колонки учитывает UTF-8 (в том числе кириллицу). Повторные обращения к одному определению используют одну цель перехода.

`clang-bundle=auto` is the default: it bundles internal Linux/Windows SDKs. `-Dclang-bundle=true` also bundles the system provider; `-Dclang-bundle=false` keeps an SDK-linked development install without copying the runtime.
