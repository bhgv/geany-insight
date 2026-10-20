# Сопоставление с Source Insight 4

Эталон: https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Window_Tour.htm
и https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Relation_Window.htm

Это проверяемая карта возможностей, а не заявление о полном совпадении.

| Область | Текущее состояние | Незакрытый объём |
| --- | --- | --- |
| File Symbols слева | Поиск, сортировка, общие цвета/значки, группировка и фильтр типов, счётчики, Context, вложенные tag scope по языку; проверен GUI Linux | Семантические владельцы scope |
| Project Files справа | Manifest, поиск, сортировка, Context; диалог добавления/удаления файлов и деревьев, Remove all, исключения, DnD, импорт/экспорт списка, двусторонний фоновый MFL со снимками/откатом, нерекурсивными каталогами известных типов и GUI-проверкой | Расширенные параметры синхронизации и Path Variables |
| Project Folders | Дерево manifest, фильтр/нечёткий поиск с папками-предками, счётчик, меню/клавиатура, копирование путей, Locate/Follow текущего файла, ветки; отдельный фоновый Browse disk с preview/open неиндексированных файлов, навигацией, пределами и сохранением состояния/колонок; переименование/перемещение без перезаписи и системная корзина, file ID/etag, защита открытых документов; core/изолированный GUI Windows/Linux, полная сборка и Browse disk GUI Linux | Полная сверка Folder Browser Options, раскрытие непосещённых веток и перенос сохранённых ссылок при rename/move; аналог виртуальных групп требует подтверждения |
| Project Symbols | Теги Geany, поиск, сортировка, цвета/значки, группировка, вложенные tag scope и фильтр типов, независимые настройки | Семантическое разрешение типов и перегрузок |
| Symbol Categories | Языковые типы/цвета/значки, вложенные scope, прямые члены однозначных классов, фильтр, дозагрузка, счётчик, меню/клавиатура/Info/References, колонки | Семантическое наследование, aliases и неоднозначные владельцы |
| File Types | Профили открытых и закрытых файлов, фильтр путей, счётчик, меню/клавиатура, независимые колонки/сортировка | Детальная сверка всех File Type Options с Source Insight |
| Context снизу | Цветной Scintilla, номера строк, центрирование, подчёркнутые ссылки с курсором-пальцем и обычным кликом, история, поиск | Подробная проверка GUI на целевых ОС |
| Relation Window | Определения/объявления, прямые члены scope, прямые/транзитивные Base/Derived и наследуемые члены по однозначным тегам, лексические ссылки, Calls текущей функции и Call sites проекта, Follow symbol / enclosing function, граф одного уровня с масштабом и двумя ориентациями, ленивое tag-дерево и раскрываемый граф с историей веток и пределами, scoped nominal aliases корней/баз/членов | Семантические вызовы/вызывающие функции, полные типы/перегрузки и include semantics, многоуровневые вызовы/ссылки |
| Overview Scroller | Цветная миниатюра файла, видимая область/выделение, прокрутка мышью и колесом, масштаб/ширина, сохранение параметров, отметки закладок и изменённых/сохранённых строк, переходы по блокам правок, текущая/все функции с фоновыми лексическими границами; проверен GUI Linux | Семантические диапазоны и дополнительные языки за пределами brace/Python/Lua |
| Панели | Десять float/dock, кнопки управления, Tab Tray; восемь проектных панелей объединяются в группы с вкладками через меню и перенос заголовка; состав/порядок/активная вкладка сохраняются | Произвольное закрепление у границ и группы с File Symbols/Context/Window List |
| Symbol Info | Отдельное окно: кандидаты, сведения тегов, цветной исходник, Jump с проверкой снимка, References, Leave file open и копирование, размер завершённых функций и именованных определений C-семейства/Python, nominal-типы aliases из снимка, устойчивый Refresh и RU/EN GUI | Размеры остальных символов/синтаксисов и типы произвольных выражений; сложность не подтверждена как требование команды Symbol Info |
| Window List | Немодальный список открытых буферов, поиск, MRU/имя/путь, ближайший символ, Context, Activate/Save/Close, сохранение геометрии/фильтра/сортировки/колонок; закрепление в sidebar и видимость/docking/геометрия в A–D, миграция раскладок; проверен GUI Linux | Перетаскивание, snap-to-edge |
| Раскладки A–D и файлы | Пользовательские снимки с геометрией десяти панелей, Tab Tray, Window List; сохранение/загрузка дополнительных файлов | Детальная сверка всех параметров окон |
| Bookmarks / Clips | Постоянное хранилище, навигация, вставка и редактирование Clips, переименование, импорт/экспорт выбранной панели, относительные пути проекта при переносе закладок; движущиеся маркеры и смещение от функции/класса с native/GUI-проверкой | XML Source Insight; перенос при переименовании самого символа |
| Шаблоны кода | Clips: создание, ввод пользовательских переменных, дата/файл/проект/выделение/курсор, языковые профили/фильтр списка, отдельные project stores, completion и редактируемые placeholders, один Undo/Redo, импорт/экспорт, общие значки меню/панели | Полная совместимость переменных/синтаксиса шаблонов Source Insight |
| Структурное выделение | Меню Edit, расширение по вложенным скобкам, Select Match, Select Line, Select Function, блоки Python/Lua и Function Up/Down; проверен native GUI, строки и комментарии исключаются лексической маской | Семантические границы; дополнительные языки за пределами brace/Python/Lua |
| Поиск и изменение | Geany Find/Replace, Unicode/regex, Boolean Keywords с контекстом строк, выбор области/масок, сохранение параметров и выбранных путей, отдельная Search Results с Shift+F8/F9, жёлтые совпадения с красной рамкой, подсветка похожего выделения | Словарные формы за пределами регулярных английских окончаний, семантическое переименование и комплексные рефакторинги |
| SSH (расширение) | OpenSSH/SFTP, shell с вводом строк, кэш файлов/папок/.geany проектов, переносимые пароли в конфиге по опции, отдельная автоотправка после Save | Полная VT/PTY эмуляция терминала, live refresh удалённой папки, разрешение конфликтов удалённых правок |
| Lua | Встроенный runtime, команды, события, API редактора и проекта | Расширение API для всех новых панелей |
| Синтаксисы/кодировки/плагины | Сохранены механизмы Geany, plugin API включён | Регрессии на целевых ОС и новые языковые backend |
| ОС | Linux и Windows x64 собираются | Реальная проверка GUI, macOS; Android пока отдельный неполный редактор |

Code only является лексической маской, а не AST/LSP. Не моделируются
выражения интерполяции строк, JavaScript regex literals и препроцессор.
Для остальных диалектов доступен прежний поиск всех употреблений.
Парсеры Geany продолжают определять поддержку синтаксисов и символов.
Сборка пользовательских проектов исключена из целей по указанию пользователя.
APK создаётся только по явному запросу, только ARM64.

Documentation is bundled offline as Markdown/reStructuredText with a GTK viewer, GIF animation and a browser multimedia view for HTML5 video. Help/menu and main toolbar expose the guides and tutorials with matching command icons. Text search panels/results open on demand through Поиск/замена; standard Ctrl+F/Ctrl+H retain native Find/Replace. Native GUI tests cover these commands; embedded video decoding is not provided.


### Symbol type presentation and panel help (2026-10-14)

File Symbols and Project Symbols share language-aware Ctags kind colors/icons and optional type groups. Grouping is incremental and preserves selection/collapsed groups. Project controls can scroll horizontally when the right pane is narrowed. Each panel has a small question icon opening its own offline RU/EN controls reference and tutorial; native notebook panels use the notebook corner. Diff/Merge is on the main toolbar and Tools menu.


### Symbol type filters (2026-10-14)

The funnel menu in both symbol panels selects visible categories independently. Show all / Hide all, per-language kind mapping, name-query intersection, grouped and flat views, visible/loaded counters and saved masks are covered by native GUI tests. Type filtering affects the loaded outline rather than index contents; initial name-query portions are 2000 file / 500 project rows, with explicit incremental loading.


### Symbol outline context menus and keyboard (2026-10-14)

Both outlines provide right-click and keyboard menus, definition/Info/reference actions, copy name/location, selected-category filtering and group controls. Keyboard commands are routed before Geany’s global shortcuts when a symbol list has focus. Down/Enter from a filter reveal the first leaf in collapsed groups and ignore stale rows during loading. GUI tests cover both outlines, clipboard, preview independence and live RU image-menu labels.


### Incremental symbol outlines (2026-10-14)

Both outlines expose an icon-only Load more button and the matching context-menu action. A plus in the visible/loaded counter marks additional query matches. Each request adds 2000 file or 500 project rows without navigating the editor; new queries and scope changes reset the portion. Type filters apply to loaded rows, while name queries scan the available index (maximum 250000 symbols). Native GUI tests cover a function beyond both initial portions, grouping, cancellation, query resets and live RU tooltips.


### Persistent symbol sorting (2026-10-14)

File and project outlines offer name, source line, kind, scope and file path, with an icon-only descending toggle. Header sorting synchronizes the controls in flat and grouped views. Each outline independently saves its field and direction, with compatible defaults for older settings. Native GUI tests verify restore/write, header synchronization, query rebuilds, RU tooltips and unchanged editor position.


### Locate caret declaration in File Symbols (2026-10-14)

An icon-only target button and local outline menu action select and reveal the caret declaration without updating Context or navigating the editor. Supported brace/Python/Lua languages prefer the containing indexed function in buffers up to 2 MiB; other cases use the nearest preceding indexed declaration. Hidden/unloaded results report a status message without clearing filters or increasing the loaded portion. Native GUI covers both commands, group expansion, boundary/fallback cases, hidden results and RU labels.


### Persistent symbol column layouts (2026-10-14)

Both symbol outlines support resizing and reordering headers, fixed widths and ellipsized values with horizontal scrolling. Each panel saves its complete column layout independently. Validated restore ignores incomplete or duplicate layouts; Reset symbol columns affects only that panel's widths and order, preserving query, type filters and sorting. Native GUI covers restore/write, invalid settings, grouping/query rebuilds, reset and RU labels.


### Symbol query history (2026-10-14)

Both outlines provide an icon-only recent-query menu, explicit Remember and recording on Enter. Each independently persists 16 distinct valid queries up to 256 UTF-8 bytes. Choosing a query changes only the filter without activating a result; clearing history preserves the current filter. Query literals are excluded from UI translation. Native GUI tests verify restore, MRU bounds, invalid queries, both recording paths, non-navigating selection, RU commands and independent clear/write.


### Twenty outline and file-list improvements (2026-10-14)

Exact quoted path/scope queries, qualified-name/path/line clipboard commands, selected-symbol filters, query clearing, individual group controls, category inversion and persistent column visibility extend both symbol outlines. Qualified names use the active native parser when the source is open.

Project Files now has a non-navigating right-click menu, scoped keyboard commands, two-way synchronized sorting, independent column layout/visibility, saved preferences, file-aware query history and matching-file counters. Document events refresh after lifecycle changes; formatted counters update on UI language changes.

Backend lookup tests and a native C++ GUI scenario cover the new commands and settings. Seven existing GUI suites cover menus, columns, history, paging, pane sizing, overview/navigation and localization. Updated offline RU/EN tutorials explain all controls; project enumeration and list limits remain explicit.


### Ten Project Folders improvements (2026-10-15)

The manifest tree now supports shared path filters including fuzzy subsequences, ancestor retention, counters, non-navigating context menus, keyboard commands, full and project-relative clipboard paths, explicit file opening, Locate current file, persistent Follow current file, and selected/all branch controls. Tree refresh preserves selected source and expanded directory identities; invalid regex preserves the last valid tree. The tree remains limited to the first 10000 manifest files rather than arbitrary disk contents. Offline RU/EN controls and a tutorial document the behavior.

### Inheritance, scope hierarchy and non-brace functions (2026-10-15)

Relations supports indexed transitive Bases/Derived/inherited Members with namespace resolution, eligibility filtering, ambiguity omission, cycles/diamonds and depth limits. Both symbol outlines independently support nested parser scope branches and restore nested expansion/selection. Python/Lua function ranges and suite expansion now drive Select Function, lexical Calls, function tracking and Overview. Native GUI and standalone core tests cover these paths; semantic aliases/overloads/owners and expandable relation edges remain open.


## Прямая сверка по руководству 4.0 (2026-10-15)

Основной источник: https://www.sourceinsight.com/documentation/ → руководство
https://www.sourceinsight.com/doc/v4/userguide/index.html . Старые записи выше
отражают этапы реализации, а не сертификат полного соответствия. Статус
«полностью перенесено» пока не установлен для всего продукта.

| Раздел руководства | Проверяемое требование | Статус/остающаяся работа |
| --- | --- | --- |
| [Bookmark Storage](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Bookmark_Storage.htm) | Позиция относительно объявления функции/класса; устойчивость к внешним правкам | Реализовано: имя/scope/вид + смещение, актуальный снимок тегов, перенос/import/rename; native и GUI. Отдельные проектные хранилища реализованы и проверены GUI; Выбор нескольких строк, экспорт выбранных и пакетное удаление реализованы с GUI-проверкой; XML-совместимость остаётся открытой |
| [Style Properties](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Style_Properties.htm) | Иерархия стилей, наследование, собственные стили и сохранение | Geany themes + профильный CSS покрывают часть настроек. Родительские SI-стили и импорт SI-конфигурации не перенесены; точные пользовательские RGB/шрифты нельзя установить из описания опций |
| [Controlling Syntax Formatting](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Controlling_Syntax_Formatting.htm) | Временный Mono Font View; отдельный color-only режим | Реализованы Mono Font View, color-only, grayscale и отключение оформления; 4 GUI-сценария, RU/EN, восстановление raw-стилей и настроек |
| [Predefined Path Variables](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Predefined_Path_Variables.htm) | Именованные пути в настройках и командах | Подмножество уже работает в Master File List. Все контексты и все predefined names ещё не покрыты |
| [Lookup References](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Lookup_References.htm) | Smart Reference Matching, inactive-code и comment-only фильтры | Доступны All text, Skip comments, Comments only и Code only; проверены точные позиции, история и сохранение настроек. Лексические фильтры C/C++, JS, Python, Lua, Shell. Семантическая идентичность всех ссылок и условия препроцессора ещё не перенесены |
| [Symbol Info](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Symbol_Info.htm) | Сведения определения, исходник, размер при наличии, Jump/References/Leave open | Основные действия, размеры функций и именованных определений проверены GUI; неизвестные типы и остальные синтаксисы остаются ограниченными |
| Макросы и команды | Совместимость с документированными SI макросами и полным каталогом команд | Lua — отдельное расширение, не совместимый интерпретатор ISL. Полная совместимость не заявляется |
| Окна и платформы | Все виды окон, клавиатура, drag/dock, проверка ОС | Частичная реализация и Linux GUI; Windows x64 и APK не запускать без отдельного указания пользователя |

Сборка пользовательских программ по-прежнему исключена по указанию пользователя.
Руководство описывает назначение настройки шрифта, но не содержит экспорт
конкретной пользовательской таблицы стилей. Отсутствующий импорт/дисплейный
режим отмечается здесь как пробел, а не подменяется похожим оформлением.

### Symbol lists: parser declarations and activation (2026-10-15)

File and Project Symbols now use an independent declaration database captured
from native Geany/ctags TMTag snapshots, including the parser's actual type and
scope. Navigation keeps its separate parameters/local bindings. Undefined parser
internals and parameter records are omitted from display; local definitions are
opt-in. Source-line order nests actual owning declarations and retains overloaded
functions. Project paths display relative to the source root.

[Symbol Window Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Symbol_Window_Options.htm)
documents optional members/local definitions. [Project Symbol List](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Project_Symbol_List_Window.htm)
documents preview tracking; [Project Settings](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Project_Settings.htm)
defines relative source paths. User-confirmed interaction: selection/arrows preview
in Context; double-click, Enter and Space activate the selected declaration in the
editor. Both destinations highlight the symbol yellow with a red one-pixel frame.
Native and GUI regressions cover database separation, overloads, nested union
fields, relative paths, focus retention and activation independent of Context's
caret. This does not claim a new parser implementation or full SI parser parity.

### Project bookmark stores and controls (2026-10-15)

Bookmarks now use a per-project portable sidecar and the legacy global store when
no project is open. Scope generations guard modal rename/import/export. Store
switches clear only registered markers, restore current-scope marks and preserve
global clips. Corrupt input is kept intact; failed transactional mutations retain
the old list and markers. Failed pending-edit saves retain the previous store in
memory. Explicit reload does not silently overwrite external changes.

A native GUI scenario covers two projects with the same bookmark name, project
switch/close/reopen, stale modal rename, global data preservation, corrupt-file
repair, failed-write rollback, relative paths after moving a project, automatic
letter names, Ctrl+N/Ctrl+X and containing-symbol display. Existing anchor and
project-template GUI scenarios pass. XML interchange remains a gap; file naming intentionally follows Geany Insight's own format.

### Bookmark selection and export (2026-10-16)

The Bookmark Window supports multiple selection, visible-row Select all,
transactional batch delete and a non-navigating icon menu. Selected export freezes
records before the file chooser opens; Export all explicitly includes filtered-out
bookmarks. Symbol anchors and project-relative paths use the existing serializer.
Native GUI covers selected/all exports, selection changes during the chooser,
batch-delete write failure, clips preservation and menu sensitivity. XML exchange
remains unsupported.

### Bookmark field commands and confirmation (2026-10-16)

The Bookmark Window field now resolves exact/unique partial names and one-based
line numbers, with numeric bookmark names taking precedence. Ambiguous names and
invalid numbers leave the editor and store unchanged. Delete all includes hidden
marks and always confirms; selected deletion has a persistent confirmation switch.
Both use transactional writes and store-generation guards. GUI covers cancel,
reload during confirmation, failed-write rollback, hidden marks, retained clips,
name/line priority and restoration of the preference. Bookmark Options persistence
and close-file policies are described below; XML exchange remains open.

### Bookmark panel Font & Color (2026-10-16)

The Bookmarks panel menu now exposes the shared Font & Color dialog, as described
in Source Insight's Bookmark Options. Fonts and foreground/background colors use
an independent persisted style group; theme reset retains the custom values and
Cancel leaves the style unchanged. The separate editor bookmark-list window is
unchanged. The existing selection GUI scenario now includes restore, apply,
cancel, theme reset, all-column fonts, folder-style isolation, caret/selection
stability, saved preferences and Russian menu labels. All three bookmark-option GUI invocations pass on Linux. Delete bookmarks when file closes is also available, defaulting off and saved
independently. Accepted document-close events transactionally remove only the
closing file's marks from the active store; write failures retain notes and
other files/Clips are preserved. Normal session exit preserves marks by default. Additional
GUI assertions cover off/on, hidden marks, failure/reopen recovery and preference
restore. The separate Preserve bookmarks between sessions option is described below;
Source Insight XML interchange remains open.

### CSS visual themes and bundled Cyrillic fonts (2026-10-16)

The supplied SI 4.00.0078 default.themes.xml now produces 15 named styles folders,
with editable editor.css and panels.css and a reproducible converter. ParentStyle
chains are expanded; all exported semantic styles and unsupported attributes are
retained in CSS. Runtime applies generic lexer categories and line-number styles,
not the full semantic declaration/reference taxonomy. Panel/window CSS uses
native GTK; explicit panel appearance wins. Theme selection is persistent and
failure-atomic; disabling it restores original language-profile attributes.
Mono/Syntax Formatting, Context and new buffers retain their existing behavior.

Bundled Noto Sans/Mono files include OFL licenses, source URLs and hashes and are
registered privately. Russian alphabet, Ё/ё and № are checked in the real cmaps.
Installed original Windows fonts and UI Verdana are preferred through CSS stacks;
proprietary font files are not distributed. Linux GUI is verified; Windows remains
deferred. The uploaded export lacks a base editor point size (12pt is the editable
conversion baseline), and the styles-only theme retains current backgrounds.


Bookmark Options now has a panel button, list-menu command and panel-local
Ctrl+Q dialog, including confirmation/close-file flags, Font & Color and help.
Apply saves all three flags atomically and preserves unrelated settings; failed
writes and Cancel leave them unchanged. Session preservation is described below.


### Bookmark session preservation (2026-10-16)

Preserve bookmarks between sessions is implemented in Bookmark Options, default
on for existing profiles. Disabled mode retains marks throughout the process
and cleans valid visited global/project stores at normal exit. It preserves
Clips and exports, checks content digests plus native etags, and retains stores
on failed cleanup. Unvisited stores and crash recovery are outside cleanup.
Core and Linux GUI checks cover toggling/rollback, project switches, cancelled
exit, actual subprocess restart, external changes and failed cleanup.


### Symbol Info named definition ranges (2026-10-16)

Named class/struct/union/enum/namespace/interface bodies now show lexical line
size in C-family buffers; Python classes use suite indentation. The source
header must match name and kind on the parser line. Forwards, aliases, missing
ends and later same-line declarations decline. Headers are bounded to 16 KiB,
128 groups and 64 template levels. JavaScript/TypeScript nominal sizes remain
deferred until their regex masking is complete. Core and native Linux GUI
cover C++ kinds, Java interfaces, Python classes, RU/EN, unchanged source/Undo
and stale/ambiguous declarations. Full expression types and AST metrics remain
outside this change.

Folder Browser: добавлены File Size/File Date с фоновым чтением, сортировкой заголовками и сохранением настроек; Linux GUI и реальный перезапуск проверены. Единый Options дискового списка добавлен (колонки/Follow/скрытые/Font & Color, кнопка/меню/Ctrl+Q); адаптивная и ручная компоновка добавлены следующей серией ниже.

Project Files: Enter с маской фильтрует без открытия, `*` снимает фильтр;
маски без пути сравниваются с именем. Старые File/Path раскладки мигрируют
в четырёхколоночный список с сохранением настроек.

## Серия 2026-10-16: обслуживание файлов в Browse disk

Перенесены команды Rename/Move/System Trash, F2, применение Rename/Move по Enter, подтверждение корзины с Cancel по умолчанию, фоновые операции без перезаписи и copy/delete fallback, file ID/etag из снимка, защита открытых документов/смены каталога/закрытия и сохранение сообщения об ошибке при обновлении языка. Добавлены RU/EN подписи и справка. PROGRAM_DIR перенесён в main вместе с проверкой отсутствующего /proc в Linux.

Проверенный код: `2eaf74c3c1f3a84af17cdcab27220cf85926a2bf`.

- [Windows и Linux core/изолированный production GUI](https://github.com/bhgv/geany-insight/actions/runs/38866812349): обе задачи успешны, проектные пути и файловые операции с `-Werror`; диалоги, Unicode rename/move, collision, stale snapshots/root, open-document guards, отмена корзины и shutdown. Сервисы перевода/оформления и массив документов подменены в изолированном GUI-тесте; production Folder Browser компилируется с настоящими заголовками Geany.
- [Полный редактор Linux](https://github.com/bhgv/geany-insight/actions/runs/38866812464): Meson build/install, guarded core, isolated dialogs GUI и существующий unindexed disk browser GUI; 3 OK, 0 failures, 0 skipped.
- Локально: core, изолированный GTK GUI и UBSan прошли; недоступная корзина сохранила исходный тестовый файл. Успешная отправка в реальную корзину Windows отдельно не проверялась.

Открыто после этой серии: адаптивная компоновка (добавлена следующей серией ниже), переписывание сохранённых ссылок проекта/закладок после rename/move и полный native Windows GUI. Сверка file ID/etag перед операцией не является файловой блокировкой от внешних гонок. Полный паритет Source Insight не заявляется.

## Следующая серия 2026-10-16: дерево и форма Folder Browser

Добавлены отдельная область предков/загруженных подпапок, кэш соседних веток с пределом 4096 строк, навигационное меню без правок по правому щелчку, независимый Font & Color, Follow скрытого каталога при фильтре файла, адаптивная и ручная ориентация, раздельные положения разделителя и совместимость старых/повреждённых настроек. Обход диска остаётся в прежней фоновой задаче; дерево строится из снимка. Эталон: [Folder Browser Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Project_Folder_Browser_Options.htm).

Проверенный код: `c7897f5d7abe2a30fb15a07b8febb60291e051fc`.

- [Windows/Linux core и изолированный GUI](https://github.com/bhgv/geany-insight/actions/runs/38869372760): обе задачи успешны. Проверены форма/ручные режимы, завершённые перераскладки GTK, полная видимость выбранного каталога, Apply/Cancel, раздельные разделители, некорректные настройки, соседние ветки, меню правой кнопки без навигации, копирование пути, hidden-file Follow и прежние файловые операции.
- [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38869372956): 4 OK, 0 failures, 0 skipped; включены прежний Browse disk GUI, реальный Font & Color дерева и восстановление в новом процессе.
- Локально также пройдены core/UBSan, production GTK GUI и визуальная проверка обеих ориентаций.

Изолированный GUI по-прежнему подменяет перевод/оформление и массив документов, native Linux проверяет реальные сервисы. Произвольное раскрытие ещё не посещённых веток, полный native Windows GUI и полный паритет панели не заявляются.


## Серия 2026-10-16: ленивые ветки Folder Browser

Реализовано произвольное раскрытие непосещённых каталогов без перехода в них: отдельные фоновые чтения подпапок не зависят от фильтра и предела файлового списка. Добавлены дозагрузка по 256 подпапок, обновление выбранной ветки с сохранением раскрытых потомков, Expand/Collapse, отмена чтения, очистка кэша и локальные F5/Escape/+/- при сохранении штатных Left/Right. Меню дерева и RU/EN справка обновлены. Предки текущего пути сохраняют стрелки раскрытия без повторяющихся служебных подсказок.

Эталон панели: [Folder Browser Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Project_Folder_Browser_Options.htm). Проверенный код: `17b8041280d14f98aee8dcd6d2cb61cd59118b02`.

- [Windows/Linux core и изолированный GTK GUI](https://github.com/bhgv/geany-insight/actions/runs/38876201963): непосещённые/глубокие/пустые ветки, Unicode, 300 подпапок и дозагрузка, каталог с 2100 файлами, сохранение раскрытия и размера прочитанной части при refresh, исключение ссылок при доступности их создания, удалённая папка/повтор, отмена, supersession скрытых файлов, очистка кэша и закрытие. Прежние проверки файловых операций и компоновки тоже включены.
- [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38876202127): 9 OK, 0 failures, 0 skipped. Новый native сценарий проверяет раскрытие и branch refresh без смены дискового корня, документа и состава проекта, затем явный переход по выбранной папке. Включены прежние Browse disk, Git и файловые GUI-регрессии.
- Локально: production GTK-проверки, UBSan и визуальная проверка дерева.

Пределы: до 4096 настоящих папок и по одной служебной строке на папку; восемь одновременных чтений, до 100000 просмотренных записей за чтение, до 64 автоматически восстановленных предков. Страницы сортируются после ограниченного чтения и не являются глобально отсортированными срезами всего диска. Кэш веток не переносится между сеансами. Проверка типа ссылки не блокирует внешнюю замену компонентов пути. Полный native Windows редактор, перенос сохранённых ссылок при rename/move и полный паритет Folder Browser остаются открытыми.

## Серия 2026-10-16: File Type Options — редактирование

Добавлены настройки редактирования по типу файла: пробелы/табуляция/смешанные отступы, отдельная ширина табуляции и шага отступа, четыре режима автоотступа, экранный перенос и перенос при наборе, номера строк, пробельные символы, окончания строк, направляющие и правая граница. Включаемая группа наследует общие настройки до явного включения. Применение затрагивает подходящие открытые и новые документы; отключение восстанавливает снимок прежних параметров документа в текущем сеансе. Текст и история Undo/Redo сохраняются.

Команда File Type Options добавлена в Options и контекстное меню редактора. [Руководство с JPEG-снимком production-вкладки и SVG-схемой](languages.md) объясняет два вида переноса. Печать, отдельные флаги completion/snippets и Include when adding остаются вне этой серии; полный паритет File Type Options не объявляется.

Проверенный код: `676f84f722ea3aa84bf9037529c124096f54d99e`. [Linux build/install и 14 проверок](https://github.com/bhgv/geany-insight/actions/runs/38881148256): формат/границы/старые профили/повреждённые значения, GTK-вкладка, реальные меню Apply/Cancel, отступ штатной командой и Undo/Redo, изоляция документов, возврат прежних параметров, повторное открытие в новом процессе. Включены прежние language/filter, Git, Folder Browser и disk-operation регрессии. Windows/Android в этой серии не запускались.

## Серия 2026-10-16: разрешения ввода по типу файла

Перенесены Allow auto-complete, Allow code snippets, разрешение штатного автозакрытия скобок/кавычек/XML-тегов и Enter key inserts new line. Отключённый Enter переводит курсор в начало следующей строки без правки текста. Автоматическое дополнение учитывает общий переключатель; явная команда дополнения сохраняется. Запрет snippets охватывает штатное раскрытие, назначенные клавиши и дополнение имён шаблонов проекта; явная вставка Clips доступна. Применение запрета закрывает видимую подсказку, а выбор устаревшего template-кандидата повторно проверяет разрешение.

Все флаги сохраняются в профиле, отсутствующие ключи разрешают прежнее поведение. Обновлены [RU/EN руководства, настоящий JPEG-снимок GTK-вкладки и SVG-схема](languages.md). Intelligent paste, печать, Include when adding и полный набор парных shortcuts Source Insight остаются открытыми.

Проверенный код: `6bfbb3896a3dd4d8ffef6ad48bb8cccd86409a38`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38882334641): 14 OK, 0 failures, 0 skipped. Расширенный сценарий проверяет Enter без вставки строки, запрет раскрытия snippets и автозакрытия, их возврат при отключении профиля, текст/Undo и перезапуск; изолированная GTK-проверка и core проверяют все новые флаги, совместимость старых профилей и повреждённые значения. Прежние Git/Folder Browser/language/filter/disk-operation проверки включены. Windows и Android не запускались.

## Серия 2026-10-16: отслеживаемые скобки и кавычки

Добавлены пропуск автоматически созданного закрывающего символа, удаление пустой созданной пары одним Backspace и оборачивание уже существующего совпадающего скобочного блока новой парой. Действуют при включённом профиле редактирования и разрешённых typing shortcuts. Поддерживаются `()`, `[]`, `{}`, созданные одинарные/двойные кавычки; оборачивание готовых строк в кавычках и XML-тегов не заявляется.

Отметки ограничены 128 парами на Scintilla-виджет, следуют байтовым смещениям UTF-8 и сбрасываются Undo/Redo, отключением переопределения/разрешения либо удалением границы пары. Пропуск меняет только курсор; удаление и оборачивание сгруппированы в одно Undo. Ручные пары, read-only, выделения/несколько курсоров и активные completion-popup защищены. [Руководство и SVG-схема](languages.md) обновлены на RU/EN. Полный паритет всех typing shortcuts остаётся открытым.

Проверенный код: `beb192b9a0b8483227be80643aa77a7acfcca247`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38883855082): 15 OK, 0 failures, 0 skipped. Новый native сценарий проверяет скобки/кавычки, пропуск без нового Undo-действия, удаление пустой пары и Undo/Redo, оборачивание существующего блока, вложенность, UTF-8-позиции, ручной текст, сброс после Undo и read-only. Прежние 14 проверок включены. Локально пройдены строгая синтаксическая проверка модуля/интеграции/теста, SVG-декодирование GTK и визуальная проверка схемы. Windows/Android в этой серии не запускались.

## Серия 2026-10-16: расширенное автодополнение символов

Добавлен GTK-popup возле курсора с цветными именами/типами/параметрами/scope, значками и категориями символов, сведениями о расположении объявления. Используется прежний контекстный поиск кандидатов и порог автопоявления. Ввод повторно запрашивает кандидатов, Backspace также расширяет список; выбор удерживается по скопированной идентичности при обновлении. Up/Down/PageUp/PageDown, Enter/Tab, Escape, hover и левый щелчок работают при сохранении фокуса редактора. Вставляется только имя, одним Undo-действием и с прежней политикой остатка слова.

Добавлен сохраняемый Options → Detailed completion window (по умолчанию включён), отключение возвращает Scintilla-список. При вставке проверяются документ, позиция, выделение, read-only и неизменность префикса; модель хранит копии строк, а не указатели на теги. Панель учитывает границы монитора, выбранная строка использует цвета темы. [RU/EN справка и JPEG настоящего production-виджета](languages.md) обновлены.

Списки snippets, слов документа и внешних плагинов сохраняют прежний интерфейс. Отдельная вставка параметров, объединение всех источников кандидатов и полная семантика Source Insight остаются открытыми; отсутствующие сведения парсера не выдумываются. Эталон: [Typing Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Typing_Options.htm).

Проверенный код: `612af3c9ac73259515326650e1e111c5f64bde02`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38888368913): 16 OK, 0 failures, 0 skipped. Новый тест ждёт штатного фонового разбора документа, затем проверяет автопоявление после 3 символов, цветовую разметку/значки/тип и сигнатуру, сужение при вводе и расширение Backspace, Up/Down/Enter, мышь, Escape, Undo, обычный список при отключении и сохранение переключателя. Прежние 15 регрессий также прошли. Исправлены закрытие при повторном применении языкового оформления и временные GTK focus-out при обновлении popup. Локально пройдены строгая синтаксическая проверка и capture/визуальная проверка production GtkTreeView в изолированном сценарии. Windows/Android не запускались.

## Серия 2026-10-16: Completion options и вставка вызова

Добавлен отдельный Options → Completion options с Cancel/Apply/OK, общим переключателем подробного списка, принятием по Tab, разрешением комментариев, необязательной вставкой пустых скобок функций и пробелом после имени, отдельным порогом 0–12 (0 наследует Geany) и высотой 3–20 строк. Изменения сохраняются в прежнем insight-completion.conf; старый ключ detailed поддерживается, неизвестные корректные группы/ключи сохраняются. Некорректная группа отклоняется с defaults.

Вставка `()` применяется только к функциям/методам/прототипам расширенного списка, не дублирует существующую открывающую скобку после пробелов/табуляций, оставляет курсор внутри пары и объединена с именем в один Undo. Остаток слова и неопределённо длинный пробельный хвост подавляют добавление скобок. Декларационные параметры не превращаются в выдуманные аргументы вызова. Общие/языковые запреты автодополнения сохраняются; строки не становятся комментариями. Ожидающее обновление списка выполняется до принятия с клавиатуры.

[RU/EN руководство, JPEG страницы production GTK и SVG вставки](languages.md) обновлены. Полная генерация параметров, объединение snippets/других источников, нечувствительный к регистру поиск и остальной паритет Typing Options остаются открытыми. Эталон: [Typing Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Typing_Options.htm).

Проверенный код: `2a2e79fd83d6ae3c5187ca0a7186f760898e9329`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38890036619): 18 OK, 0 failures, 0 skipped. Core проверяет legacy/defaults, roundtrip, пределы, некорректные значения, неизменность при отклонённой записи и guards скобок; production GTK-страница — roundtrip. Расширенный native сценарий проверяет Cancel/OK, порог, Tab fallback, скобки/пробел/позицию курсора/Undo, сохранение существующих аргументов, включение/выключение комментариев и настройки в свежем процессе. Прежние сценарии автодополнения, Git, Folder Browser и редактирования тоже прошли. Локально: строгая синтаксическая проверка, core, GTK capture, визуальная проверка JPEG и проверка SVG/ссылок. Windows/Android не запускались.

## Серия 2026-10-16: регистр символов при автодополнении

В Completion options добавлен сохраняемый Case-sensitive symbol matching, по умолчанию включённый для совместимости. При отключении обычное дополнение символов и фильтр членов типа принимают смешанный регистр; исходное написание имени сохраняется при вставке. В подробном списке предпочтительно выбирается кандидат, совпадающий с набранным регистром. Обычный Scintilla-список получает соответствующие флаги ignore-case/respect-case.

Сопоставление использует Unicode casefold без нормализации; подготовленный префикс ограничен 512 байтами UTF-8, имя — 4096. Нечувствительное расширение сохраняет исходные exact-case результаты и прежние правила видимости, но ограничивает дополнительные проходы суммарно 100000 записями тегов; при исчерпании бюджета подробное окно сообщает о неполном поиске. Это не неограниченный индексный поиск и не расширение источников snippets/плагинов. [RU/EN руководство, JPEG production-страницы и SVG-пример](languages.md) обновлены. Полная генерация параметров и остальной паритет Typing Options остаются открытыми. Эталон: [Typing Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Typing_Options.htm).

Проверенный код: `d621339b11d7a4a4de13e6e93c1dcb55959587f5`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38892135471): 19 OK, 0 failures, 0 skipped. Core проверяет ASCII/Unicode casefold, несовпадающую нормализацию, malformed UTF-8 и границы байтов. Production GTK-страница и настройки проверяют новый флаг, legacy/defaults и roundtrip. Расширенный native сценарий проверяет смешанный регистр, предпочтение точного регистра, исходное написание/Undo, отсутствие локальной переменной другой функции, члены структуры, обычный Scintilla-список и сохранение режима в новом процессе. Новый сценарий выявил и помог исправить чтение пустого префикса сразу после точки; также защищено чтение суффикса в конце документа. Прежние 18 регрессий включены. Локально: строгая синтаксическая проверка, core, GTK capture, визуальная проверка JPEG, XML SVG и ссылки на completion media. Windows/Android не запускались.


## Серия 2026-10-16: интеллектуальная вставка целых строк

Добавлены сохраняемый Options → Intelligent paste (по умолчанию выключен) и Allow intelligent paste в File Type Options → Editing (legacy/defaults разрешают). Обычная команда Paste и вставка Clips размещают текст с завершающим CR/LF над текущей строкой или под ней при курсоре в конце непустой строки. Полностью выделенные строки заменяются; последняя строка без EOL получает разделитель. Минимальный общий отступ непустых строк заменяется отступом назначения, относительная вложенность сохраняется, табуляция и EOL следуют документу. Вставка сгруппирована в одно Undo/Redo.

При частичном тексте/выделении, нескольких курсорах, прямоугольном выделении, виртуальных позициях, некорректном UTF-8 и отклонении по пределам остаётся штатная вставка. Асинхронный запрос буфера хранит ссылку на виджет, id документа, поколение текста, anchor/caret и повторно проверяет разрешения/назначение перед вставкой; закрытие/переключение документа или правка отменяют устаревший запрос. Read-only не обходится. Преобразование ограничено 1 MiB входа, 4 MiB выхода, 10000 строками и 4096 колонками отступа. Это текстовая обработка отступов; Drag/drop, Drag Line Up/Down, отдельный Paste Line и параметры выделения после вставки остаются открытыми.

[RU/EN руководство, JPEG настоящей production-страницы в изолированной GTK-проверке и SVG-схема](languages.md) обновлены. Эталон: [Typing Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Typing_Options.htm) и [File Type Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/File_Type_Options.htm).

Проверенный код: `3b416445d0411baefcf1a5881481c7d186182553`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38895370809): 21 OK, 0 failures, 0 skipped. Core проверяет относительные отступы, tabs/spaces, пустые строки, Unicode, LF/CRLF/CR, повреждённый UTF-8 и пределы входа/выхода/строк/колонок. Настройки и production GTK-страница проверяют новый флаг и старые профили. Native-сценарий выполняет обычную команду Paste: над/под строкой, финальная строка без EOL, CRLF/табуляция, Undo/Redo, частичный текст/выделение и выделенные целые строки, изменение назначения до чтения clipboard, read-only, выключенный общий режим и запрет профиля. Он также сохраняет и вставляет настоящий Clip, проверяет неизменность исходного файла на диске и запускает новый процесс для проверки сохранённой настройки. Прежние 19 регрессий включены. Локально пройдены строгая синтаксическая проверка, core и UBSan, GTK roundtrip/capture, визуальная проверка JPEG/SVG, GTK-декодирование SVG и новые ссылки RU/EN. Windows/Android не запускались.

## Серия 2026-10-16: Paste Line и Drag Line

Добавлено Edit → Line operations: Paste Line, Drag Line Up/Down и Drag Line Up/Down More. Paste Line переходит к началу текущей строки и выполняет обычную вставку с прежними guards clipboard. More выполняет до пяти шагов с остановкой у границы. При разрешённом интеллектуальном режиме перемещённые строки получают отступ последней пересечённой строки с сохранением относительной вложенности. Существующие Move line(s) up/down и назначенные им клавиши также используют этот режим через прежние Sci wrappers; выключенный режим/запрет профиля сохраняют native movement.

Само перемещение выполняет штатный Scintilla, затем меняются только отступы. Подготовка ограничена прежними 1 MiB/4 MiB/10000 строк/4096 колонок; отклонённое преобразование возвращает native movement. В интеллектуальном режиме блок выделен с прежним направлением, курсор следует за текстом строки. Пять шагов и отступы — один Undo/Redo. Новое меню защищает read-only, несколько/прямоугольные выделения и виртуальные позиции. Для новых More/Paste Line клавиши в этой серии не назначены. [RU/EN справка и SVG-схема](languages.md) обновлены.

Это команды перемещения строк; mouse drag/drop и параметры выделения после Paste остаются открытыми. Эталон: [Drag Line Up](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Drag_Line_Up.htm), [Paste Line](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Paste_Line.htm) и [Typing Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Typing_Options.htm).

Проверенный код: `4f67d5adf6704fb4a9ba68ccb8cc09a2d6b04f79`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38896544335): 21 OK, 0 failures, 0 skipped. Расширенный native-сценарий проверяет пункты меню и существующую команду Move line(s), сохранение отступов при выключенном режиме и запрете профиля, новую подстройку отступов и внутренней вложенности, обратное выделение блока, More вверх/вниз на пять строк одним Undo/Redo, курсор в тексте, кириллицу/CRLF, последнюю строку без EOL, верхнюю границу без нового Undo, read-only и отказ нового меню для прямоугольного выделения. Paste Line проверяется для частичного текста с выключенным режимом и целых строк с включённым, с Undo. Прежние clipboard/Clips/restart и остальные 20 проверок сохранены. Локально: строгая синтаксическая проверка, core преобразования/профилей, XML/GTK-декодирование и визуальная проверка SVG, ссылки RU/EN. Windows/Android не запускались.

## Серия 2026-10-16: буфер обмена строк и Cut Selection or Paste

В Edit → Line operations добавлены Copy Line, Cut Line, Copy Line Right, Cut Line Left/Right и Cut Selection or Paste. Copy Line расширяет частичное выделение до целых затронутых строк; при уже полном выделении следующий вызов добавляет ещё строку, с остановкой у EOF. Cut Line вырезает текущую строку активного курсора даже при многострочном выделении. Части строк определяются по активному курсору без включения EOL. Cut Selection or Paste вырезает выделение либо выполняет обычную вставку, включая существующий интеллектуальный режим. Прежние Copy/Cut Current Line(s) и их клавиши в исходных редакторах подключены к той же реализации.

Copy разрешён в read-only, не меняет текст и Undo; Cut защищён и сгруппирован в одно Undo/Redo. Пустой диапазон оставляет прежний clipboard. Диапазон ограничен 1 MiB и проверяется на полный UTF-8 без NUL до изменения clipboard/текста. Новое меню требует одного обычного выделения без виртуальных позиций; прежние команды Geany сохраняют native handling для прочих режимов и не-документных Scintilla. Отдельные клавиши новых частичных команд и Cut Selection or Paste пока не назначены. Последний EOL не синтезируется при копировании строки без перевода. [RU/EN руководство и SVG диапазонов](languages.md) обновлены.

Эталон: [Copy Line](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Copy_Line.htm), [Copy Line Right](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Copy_Line_Right.htm), [Cut Line](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Cut_Line.htm), [Cut Line Left](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Cut_Line_Left.htm), [Cut Line Right](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Cut_Line_Right.htm) и [Cut Selection or Paste](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Cut_Selection_or_Paste.htm).

Проверенный код: `1f6abadbad80a4312c905aca0195d07c457912bc`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38898921751): 21 OK, 0 failures, 0 skipped. Расширенный native-сценарий проверяет повторный Copy Line до EOF, частичное/обратное выделение, Copy/Cut через меню и существующие команды клавиш, кириллицу/табуляцию/CRLF, точное сохранение EOL документа после вырезания частей строки, последний ряд без EOL, пустую строку/пустые диапазоны, read-only Copy и отказ Cut, прямоугольные/множественные выделения, предел 1 MiB, NUL/некорректный UTF-8 и неизменность clipboard при отказе. Cut Selection or Paste проверен для фрагмента и целых строк с интеллектуальной вставкой, с отдельными Undo/Redo обоих шагов. Clipboard-сравнения учитывают нормализацию EOL GTK; сравнения текста документа остаются точными.

[JPEG меню в справке](media/line-operations.jpg) получен из полного запущенного Linux-редактора и побайтово совпал со снимком успешного запуска; это не изолированный GTK-макет. Сценарий захвата выполняется отдельно от других тестов на общем X-дисплее и отклоняет перекрытое чёрное изображение. Снимок также сохраняется как artifact CI. Прежние Paste/Clips/restart, перемещение строк, Git/Folder Browser/typing/completion проверки включены. Локально: строгая синтаксическая проверка, core, XML/GTK-декодирование и визуальная проверка SVG, JPEG и новые ссылки RU/EN. Windows/Android не запускались.

## Серия 2026-10-16: выделение, переходы и Extend Mode

Добавлено Edit → Selection and navigation: Toggle Extend Mode, Beginning/End of Selection, Blank Line Up/Down, Select Word/Line, Select To End Of Line/Top Of File/End Of File и Top/Bottom of File. Extend Mode использует штатное расширение Scintilla при обычных движениях; Geany goto-line/position и прокручивающий set-current-position сохраняют anchor через общий navigation helper. Флаг относится к документу, не сохраняется в профиль и синхронизируется при открытии меню. Escape завершает режим без удаления выделения; popup автодополнения обрабатывает Escape первым. Сворачивание выделения к началу/концу завершает режим; без выделения это no-op.

Поиск пустых строк пропускает текущую, признаёт пустыми строки с пробелами/табуляциями, учитывает завершающую пустую строку и ограничен 100000 строками/4 MiB смещения; отказ не двигает курсор. Новые команды разрешены в read-only, не меняют текст/Undo и требуют одного обычного выделения без виртуальных позиций. Слова определяет прежняя политика редактора. Дополнительные клавиши не назначены. [RU/EN справка и SVG anchor/расширения](languages.md) обновлены.

Прямые raw SCI_GOTOPOS/SCI_GOTOLINE плагинов и непрокручивающий set-current-position сохраняют прежнюю семантику; правки текста и щелчок внутри редактора завершают режим; выбранное состояние восстанавливается при активации документа. Полная унификация внешних переходов, Select Block/Match и мышиные shortcuts остаются открытыми. Эталон: [Toggle Extend Mode](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Toggle_Extend_Mode.htm), [Blank Line Up](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Blank_Line_Up.htm) и [Beginning of Selection](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Beginning_of_Selection.htm).

Проверенный код: `657846b994f3a487f28857bc7aa3f7ce4a1d9c56`. [Полная Linux build/install и native GUI](https://github.com/bhgv/geany-insight/actions/runs/38901289242): 22 OK, 0 failures, 0 skipped. Новый native-сценарий проверяет кириллицу/CRLF, выделение слова/строки/диапазона, сворачивание к краям, пустые строки вверх/вниз/EOF и отсутствие совпадения, предел 4 MiB, обычные SCI-движения с anchor, goto-line и прокручивающий переход к позиции, Escape без удаления выделения, состояние/флажок при смене документов, read-only, отказ для прямоугольного/множественного выделения, неизменность текста/Undo и файла на диске. Прежние 21 проверки также прошли, включая clipboard/Clips/restart и ожидание асинхронной вставки после большого буфера.

[JPEG нового меню](media/selection-navigation.jpg) получен из полного запущенного Linux-редактора в успешном native-сценарии; это не изолированный макет. Меню снимаются отдельно от других GUI-тестов на общем дисплее и проверяются на перекрытое чёрное изображение. Снимки доступны в artifact insight-editor-menus; JPEG и SVG добавлены в правила установки справки Meson и Autotools. Локально: строгая синтаксическая проверка нового модуля/теста, core существующих преобразований/профилей, SVG XML/GTK-декодирование, визуальная проверка SVG/JPEG и новые ссылки RU/EN. Windows/Android не запускались.

### File Compare navigation, options and search (2026-10-16)

Native Diff/Merge now includes Previous/Next with Ctrl+Up/Down, block picking,
source scrolling mapped through logical lines, optional wrap/list/line gutters,
per-pane literal Unicode search with Ctrl+F and F3/Shift+F3, and case matching.
Comparison options ignore ASCII horizontal space and/or CR/LF/CRLF style while
retaining original merge bytes and protecting CR/LF seams. Bounded source reads
and cancellable LCS run in a worker; cancel/failure preserves result and history.
Linux checks use real production widgets in an isolated GTK/Xvfb fixture, not the
full editor; screenshot and diagram are in the RU/EN internal-diff guides.
Three-way merge, intraline differences, semantic whitespace handling and padded
visual alignment remain outside this implementation.

### Directory Compare (2026-10-16)

Tools and File Compare scan two trees in a cancellable worker. Content, date and
size/date modes; bounded binary or UTF-8 normalized comparison; hidden/recursive
options; explicit folder/link/type/error/limit statuses; path/status sorting,
Unicode filtering, differences-only, navigation and clipboard reports. Selected
pairs open independent File Compare windows using completed snapshot roots and
flags. Snapshot publication is incremental. File copying/deletion, known-type
filters, backups, path variables and persistent options remain open. See the
illustrated RU/EN directory-compare guide for limits and verification scope.

### Directory window options and appearance (2026-10-16)

Directory Compare now snapshots native file-type filename patterns and language-profile extensions before worker content reads. Unknown files are omitted while directory rows, enumeration limits and cancellation remain intact. The shared Font & Color dialog supplies per-window custom fonts and foreground/background colors with Apply/Cancel. Window options, paths, filters, column visibility, sorting, size and appearance survive closing and editor restart through the workbench settings; restoring never scans automatically. Individual-file profile bindings are outside this name-based filter.

Linux checks exercise known/empty/invalid definitions, recursive filtering, real appearance Apply/Cancel, persistence, multiple windows and malformed defaults. Native editor coverage verifies the real file-type provider, reopening without a scan and settings written at exit. Updated isolated production-widget JPEG captures and the snapshot SVG accompany the RU/EN guides.

### Directory file operations (2026-10-16)

Directory Compare now copies regular-file contents left-to-right/right-to-left and sends either side to system trash. Confirmations use completed snapshot paths; editor buffers using either path are guarded before and after confirmation. Worker tasks are cancellable, hold the window modal, and invalidate the snapshot after every attempt. A successful refresh is mandatory before further writes. Exact binary copies are bounded to 64 MiB; existing targets use conditional etag replacement, missing targets use a sibling temporary file and no-overwrite move. Parent links, same-file copies, stale identities and nonregular files are rejected. No recursive writes, parent creation, metadata preservation, automatic backups, mutation undo or permanent-delete fallback are implemented. Portable GIO checks do not hold a filesystem lock.

Linux tests cover exact copy in both directions, create/replace conflicts, stale source/target, cancellation, size bounds, traversal and parent symlinks, trash success or refusal without deletion, real confirmations, edited input fields, reopened buffers, modal lifetime and closing an active window. The native editor fixture additionally guards both paths of an open document, cancels trash and copies a separate file through the actual Tools menu window. RU/EN guides include a new production-widget confirmation JPEG; both build systems install it.

### Directory refresh selection and confirmation policy (2026-10-16)

A successful refresh restores the exact selected native relative path only when it remains visible in the new sorted/filtered snapshot. Removed rows do not select an unrelated file. Confirm file operations is enabled by default and persisted per window; disabling it starts copy/Trash immediately while retaining editor guards, backend checks, modal worker cancellation and the fresh-snapshot requirement. Invalid saved policy values keep confirmation enabled. Isolated GTK tests cover both policies, selection preservation/removal and persistence; native editor coverage exercises immediate copy and its saved policy.

### File Compare view workflow (2026-10-16)

File Compare has a unified Apply/Cancel options dialog (Ctrl+Q), 50–250% scaling with Ctrl+wheel, mono-font and vertical-scrollbar controls, and source Overview strips. Overview caches logical CR/LF/CRLF line ranges in a forward pass; markers select hunks and background clicks seek source lines. Selected markers are painted last, and dense overlapping markers choose the closest center. View defaults and text-comparison options persist through the workbench without storing filenames, source data, unsaved results or history.

Source context menus expose Edit File (Ctrl+E) at the source caret line, exact selected-difference clipboard copy and display controls. Source navigation uses native absolute paths from the completed comparison, independently of edited path fields. The result keeps its normal GTK editing menu. Native editor routing reuses/open documents with navigation history and validates scale/Overview persistence. Isolated Linux GTK coverage adds scaling, Apply/Cancel, map navigation, mixed-EOL map caches, original block copying, source routing, malformed settings and closing an options parent; core diff/merge random reconstruction coverage remains enabled. Updated production-widget JPEG captures, a display-dialog capture and the SVG accompany RU/EN guides and both installation systems. Syntax formatting, nesting guides and direct editing of comparison sources remain outside this increment.

### 2026-10-16 — comparison search controls

Added whole-identifier matching, optional wrap-around and Ctrl+F selection seeding in all three comparison panes. Unicode combining marks and underscore remain identifier constituents. Production GTK fixture verifies both directions, rejected partial identifiers, wrap disabled/enabled, selection seeding and unchanged result text. Linux GTK/UBSan validation; other platforms not executed. Reference: [Source Insight Search](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Search.htm).

### 2026-10-16 — comparison line navigation

Added Ctrl+G logical-line navigation in all three panes, EOF clamping, optional line-content selection, first/last line buttons, source/result context-menu access, Escape focus return and F6/Shift+F6 pane cycling. Comparison work blocks the jump. Production GTK/UBSan checks cover empty buffers, CR/LF/CRLF, Unicode, trailing empty lines, wrap mode, focus and unchanged merge history. Native Linux editor fixture uses Ctrl+G before Ctrl+E and verifies the opened editor line. [Source Insight Go To Line](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Go_To_Line.htm).

### 2026-10-16 — merge-result replacement

Added literal replace-all with preview count, result selection scope, one occurrence per logical line, shared case/identifier options, Ctrl+H focus, deletion, bounded preflight and a single undoable batch preserving merge marks. Original sources stay read-only. No-op replacements do not create history. GTK/UBSan fixture covers Unicode, CRLF, selection, deletion, no-op, busy and size rejection; native Linux fixture replaces a port and restores it with Ctrl+Z. [Source Insight Replace](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Replace.htm).

### 2026-10-16 — preserve case and selection case commands

Added per-match Unicode preserve-case replacement for uniform upper/lower and initial-capital forms, documented literal fallback for mixed case, and Result-only Uppercase/Lowercase/Toggle Case context commands. Preview, no-op detection and size preflight use converted strings; owned replacement ranges retain their individual values through reverse application and are freed on every path. GTK/UBSan verifies Unicode expansions, Cyrillic, mixed case, menu dispatch, selection retention, history, busy state and 1 MiB rejection. Native editor fixture exercises Preserve old case and Ctrl+Z. Reorganized replacement controls into two rows to keep the editor width usable. References: [Replace](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Replace.htm), [case commands](https://www.sourceinsight.com/doc/v3/ac924207.htm).

### 2026-10-16 — result line-editing commands

Added Result menu, indent/outdent, tab-stop expansion and trailing ASCII whitespace removal for selected lines or caret line. Selection boundaries exclude a following line at offset zero. Width 1–16 (default 4) uses logical Unicode character columns. Minimal reverse edits preserve unaffected text and EOL bytes; bounded preflight rejects whole batches. Each effective batch is one undo action. Production GTK/UBSan verifies mixed endings, Unicode, selection boundaries, busy/no-op, menu construction and size rejection. Native Linux editor fixture invokes Indent through Result menu then Ctrl+Z. References: [Indent Right](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Indent_Right.htm), [Tabs to Spaces command listing](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Commands.htm), [TrimSpaces macro](https://www.sourceinsight.com/download/macro-files/).

### 2026-10-16 — result history browser and bulk undo/redo

Added Undo All/Redo All over retained states and Result History with read-only full-text preview, byte sizes, excerpts and current/saved-text markers. Restore preserves redo and tracked merge ranges; serial validation rejects stale dialogs, and parent destruction is safe. Added a 512-state cap and accounted snapshot structure/NUL overhead in the approximate 32 MiB budget. GTK/UBSan covers Cancel/Apply, stale state, busy guards, parent destruction, eviction and bulk restore of real merge results; native Linux fixture restores a snapshot through the new toolbar dialog then uses Ctrl+Z. References: [Undo](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Undo.htm), [Redo](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Redo.htm).

### 2026-10-16 — result copy and selection export

Added Save a Copy and Save Selection to Result menu. Both preserve saved-text marker and history; selection export opens the file through the native editor bridge. Unified normal result save with capture-before-dialog, busy and history-serial guards, and parent-destruction-safe lifetime handling. Exact UTF-8/EOL bytes are written without BOM/newline additions; GTK confirms overwrite. No append-to-buffer mode. Production GTK/UBSan covers copy, selection bytes, opener routing, normal save marker, Cancel, stale text, empty selection, busy and parent close. Native Linux fixture exports through Result menu and verifies file content and opened unmodified document. References: [Save A Copy](https://www.sourceinsight.com/doc/v3/ac924207.htm), [Save Selection command](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Commands.htm).

### 2026-10-16 — visible whitespace in comparison panes

Added visible ASCII spaces/tabs and line-ending overlays to all three native comparison panes. CRLF produces one paragraph marker. Toolbar, Apply/Cancel options and context toggles share state; two whitelisted booleans persist without changing preference version or old option indices. Draw traversal is bounded at 20000 characters with clipping to the text window; zoom/wrap use GTK iter geometry. Production GTK/UBSan checks marker classification, real pixel changes, unchanged exact bytes/history and preference round trip. Native Linux fixture applies both options and verifies saved config. Reference: [Visible Tabs and Spaces](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Visible_Tabs_and_Spaces.htm).

### 2026-10-16 — pane identifier search and match list

Added Load Search String, forward/backward identifier search (Ctrl+F3/Ctrl+Shift+F3) and bounded Find All (toolbar, context and Ctrl+Shift+F). Unicode identifier traversal is bounded at 4096; identifier search enables whole-word matching and retains case/wrap options. Match lists cap at 5000, show one-based logical line/column and excerpts, support Go/double-click, and reject stale offsets via history serial. Modal lifetime survives parent destruction. GTK/UBSan covers Unicode, direction, range navigation, no-result/cap, stale text, unchanged history and parent close. Native Linux fixture invokes both shortcuts in the right source pane. Reference: [Searching the Current File](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Searching_the_Current_File.htm).

### 2026-10-16 — search output format, filter, clipboard and source editing

Added Unicode case-insensitive excerpt filtering, numeric/text column sorting, persistent output-field flags, visible-row TSV copy with escaped fields and a 4 MiB all-or-nothing limit. Four whitelisted booleans persist; query/filter/sort/results/paths do not. Edit source file routes the selected filtered/sorted match through the native opener using completed native source paths and one-based match lines. Both navigation modes retain stale-serial/busy guards. GTK/UBSan verifies filter/sort/copy order, hidden/no fields, empty-filter results, Unicode, TSV escaping, size rejection, exact navigation and preference round trip. Native Linux fixture filters/copies a right-side match, opens its source and verifies persisted field settings. Reference: [Search Results Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Search_Results_Options.htm).

### Comparison search inspection (2026-10-17)

Local pane results now provide snapshot context with exact highlighting (4096 Unicode characters maximum), single-row TSV copy, filter/list focus and cyclic navigation through sorted visible matches. Ctrl+Enter retains stale-snapshot guards. This extends comparison-pane inspection; editable project search buffers and bidirectional, edit-tracking Source Links from the Source Insight guide remain open. See [controls and diagram](internal-diff.md#match-context-and-keyboard-inspection).

### Perl-compatible comparison search and replacement (2026-10-17)

Local comparison panes now support Perl-compatible regex search in both directions, dot-all, bounded match lists, zero-width positions and capture replacement in Result. Selection-only, one-per-line, preserved case, preflight limits and one Undo/Redo batch are shared with literal replacement. Patterns are bounded by match/depth/input limits; invalid patterns, capture expansions or result limits reject the entire edit. Literal identifier commands stay literal. The separate Source Insight regex dialect, per-occurrence confirmation and source-file replacement are not claimed. [Controls, production-widget JPEG and SVG](internal-diff.md#regular-expressions-in-comparison-panes).

### 2026-10-17 — per-occurrence replacement confirmation

Added persisted Confirm replacements to comparison Result. Bounded old/new previews show line/column, Replace/Skip/Replace remaining stage decisions, and Cancel discards the complete batch. Full preflight precedes review; accepted ranges apply once in reverse order with one Undo/Redo. Revision, busy and parent-lifetime guards reject stale decisions. Linux production GTK fixture with strict warnings and UBSan verifies mixed decisions, replace remaining, cancellation, external edits and parent destruction. Windows/Android were not run for this change. Reference: [Source Insight Replace](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Replace.htm). See [flow](media/confirm-replace.svg).

### 2026-10-19 — explicit comparison replacement scopes

Added Whole Result / Selection / Current position to end scope selector, shared by literal and regex preview/apply/confirmation. Current position uses the lower selection bound or caret, never wraps, and includes terminal zero-width matches. Ctrl+H automatically chooses selection or whole-result scope. Scope is transient. Production GTK tests cover Unicode/CRLF offsets, reverse selection, partial-match exclusion, EOF/no-op, regex captures/zero-width, first occurrence per line, confirmed scoped batches and Undo/Redo; native editor fixture checks the actual selector and Ctrl+H routing. Reference: [Source Insight Replace, Search Group](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Replace.htm). See [scope diagram](media/replace-scope.svg).

### 2026-10-19 — unified file search and file symbols

Merged Ctrl+F/native repeat commands with the File Search Bar; compact Source Insight arrangement and options include wrap, selection, incremental search and button word loading. Merged native ctags structure and rich Insight list into one File Symbols page with one query and explicit modes. Native structure is default; custom square type badges, pale background, bold structural declarations and bottom sorting follow the official visual reference. Platform rendering and parser differences remain; pixel identity and unimplemented SI search dialect/context features are not claimed. [Guide](unified-file-tools.md).

### 2026-10-20 — direct panel window controls

Added Float/Dock and Maximize/Restore header buttons to the ten individual
workbench panels, with RU/EN tooltips and native window-state synchronization.
Docking selects the returned tab; system close retains docking behavior. Buttons
survive reparenting. Maximized state is not persisted. Added production-panel GUI
regression coverage for all ten hosts and repeated docking. This environment lacks
GTK development headers, Meson and Xvfb, so the GUI scenario and native compilation
have not been executed here; no application screenshot is claimed.
[Controls diagram](media/panel-controls.svg).
Reference: [Panel Window Controls](https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Panel_Window_Controls.htm).

### Individual panel geometry in layouts (2026-10-20)

Layouts now store normal bounds and maximized state for each of the ten panel
hosts. Session settings remember maximized state too. Native configure events
update normal bounds only outside maximized/fullscreen/iconified states, so
restoring a window does not replace its normal size with its maximized size.
Layout schema 4 migrates schemas 1–3; missing or malformed new geometry rejects
the complete layout. Panel settings schema 2 reads schema 1 as non-maximized.
Core regression cases cover all ten hosts, old versions and invalid/missing fields.

### Tab Tray for floating panels (2026-10-20)

The ten panel hosts now expose Minimize to Tab Tray and bottom restore tabs.
Native iconification is routed into the same tray; activation restores a minimized
host. The strip can be hidden independently, and Restore minimized panels remains
available from View → Panels. Session schema 3 and layout schema 5 retain minimized
state and strip visibility; layout schemas 1–4 remain readable. Minimized masks
must reference detached, nonvisible hosts. Window List and the grouped Project
window are outside this increment. Reference: [Tab Tray](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Tab_Tray.htm).

Linux native build and production GUI scenarios now pass for all ten hosts,
maximized/normal geometry, layouts, Tab Tray and a complete process restart.
The panel-state fixture also supplies the cropped production-window JPEG.
Windows and Android were not executed for this series.

### Additional layout files (2026-10-20)

View → Layouts now saves/loads additional `.geany-layout` files independently of
slots A–D, using the same full layout capture/apply path. The native chooser
confirms overwrites. Loads read at most 64 KiB plus one sentinel byte, reject NUL,
malformed/versioned fields and invalid geometry before touching the current UI.
Files contain layout state, not source text or project paths, and use Geany
Insight's INI format rather than Source Insight XML. Portable tests cover file
round trips, invalid-save preservation, missing/truncated/oversized/NUL input.
Native Linux GUI covers the real chooser, a minimized-panel export/import and an
invalid import leaving the current window intact.
Reference: [Save Layout](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Save_Layout.htm).

### Offline window-layout guide and regression checks (2026-10-20)

Added a dedicated RU/EN guide with header control table, tray workflow, file format
limits and actual production-window capture, linked from View → Layouts and the
main guide and installed by Meson/Autotools. Expanded the native scenario with
system iconification, chooser Cancel, live RU/EN tooltips and offline guide opening.
Updated the existing Window List schema assertion to the shared current version.

### Main application geometry in layouts (2026-10-20)

Layout schema 6 captures the main editor's normal rectangle and maximized state.
Native configure/state events retain observed normal bounds outside maximized,
fullscreen and iconified states. Loading limits the rectangle to a monitor work
area; versions 1–5 leave main-window geometry unchanged. Invalid/missing main
fields reject the complete record. Core tests cover migration and bounds; the
native GUI exercises normal-size restore and maximizing/restoring a saved layout.
Reference: [Load Layout](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Load_Layout.htm).

Maximized main-window loading waits for the native normal-window resize before
reapplying maximization. The GUI regression specifically changes the intervening
normal size and verifies that Restore returns to the saved size. Linux native
build/GUI and strict UBSan layout tests passed.

### Floating panel opacity (2026-10-20)

View → Panels exposes Enable transparent panels and a per-floating-panel scale
button (40–100 percent, with a live RU/EN value tooltip). Docking hides the control;
disabling transparency applies full opacity without losing individual settings.
Layout schema 7 and panel-session schema 4 retain the option/values; prior schemas
migrate to disabled/opaque defaults. Invalid session opacity disables restoration
and resets that host's display state. Native GUI covers global disable/enable,
layout and restart persistence; an Xvfb/Openbox/xcompmgr run verifies an actual
screen-pixel change. Compositing is required. Source Insight background input
pass-through and its exact Preferences:Display location are not implemented.
Reference: [Semi-transparent panels](https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Using_Semi-Transparent_Panel_Windows.htm).

### Tabbed floating project panel groups (2026-10-20)

Eight project panel hosts can share a floating notebook. Header menus group a
panel with another, including an automatically floated target, or extract one
panel into a separate window. Dock extracts only that page; group close docks all
remaining pages. Shared window callbacks synchronize geometry/maximize, opacity
and minimized state; each minimized group has one Tab Tray entry. Reorderable tabs,
active page and membership persist in layout schema 8 and session schema 5.
Layout validation rejects nonproject/dangling/noncanonical groups, duplicate/gapped
ranks, invalid active pages and inconsistent visibility/minimized/opacity state.
Versions 1–7 remain readable without groups. Native Linux production GUI covers
three-panel join, extraction, partial dock, close, opacity/tray, order/active page,
layout loading and full restart. File Symbols/Context/Window List are outside the
initial group scope; cross-window dragging is not part of this increment.
Reference: [Grouping Panel Windows](https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Grouping_Panel_Windows.htm).

### Physical header drag between floating panel groups (2026-10-20)

Floating project headers expose a grid drag handle. Drops on project headers use
an application-only MIME target, validate source widget identity and payload, then
apply the move after the GTK drag finishes. The target header highlights; docked
targets float automatically and source-group siblings stay in place. Tabs are
natively reorderable inside the notebook. A separate optional xdotool regression
sends real press/motion/release events across two windows, into an existing group
and onto a docked destination, then runs the group layout/restart assertions.
Arbitrary edge docking and grouping nonproject panels remain outside this scope.

The physical-pointer scenario also checks extraction from an existing group onto a
docked header and successful automatic floating of that destination. A third
process injects a broken session group link and verifies independent minimized
windows, separate tray entries and Restore all. Linux native build, group GUI and
strict UBSan layout validation pass; Windows/Android were not run for this series.

### Exact Duplicate and guarded function text operations (2026-10-20)

Ctrl+D duplicates exact partial multiline selections rather than expanding boundary lines. Copy Symbol, Cut Symbol and Duplicate Symbol are available under Edit → Structural selection and in File Symbols. Fresh parser names/lines and complete standalone brace/Python/Lua bodies are validated; read-only copy is allowed, cuts/duplicates use one Undo, and duplication preserves the clipboard. Limits, native screenshot and diagram: [structural editing](structural-selection.md). Native Linux GUI covers exact/reverse selections, Unicode, spaces, CRLF, line fallback, nested functions, EOF separators, stale outline targets, invalid ranges, read-only and Undo/Redo. The new symbol commands cover functions/methods, not arbitrary symbol kinds or project rows. References: [Duplicate](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Duplicate.htm), [Duplicate Symbol](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Duplicate_Symbol.htm).

### Native Selection History and bulk editor Undo/Redo (2026-10-20)

Selection History lists the shared Back/Forward queue with file, line, visual column, source excerpt, current marker and full-path tooltips. Go To preserves the forward branch; text edits anchor retained positions, closing files removes entries, and the queue is bounded to 512. Stale dialogs refresh before navigating. Untitled buffers, symbol labels and restart persistence are not implemented. Edit → Undo All / Redo All traverse retained Scintilla groups with cancellable batches, preserving normal undo/redo and savepoint behavior. Changed history/document, read-only and open-group guards stop unsafe continuation. Native Linux GUI verifies these paths along with existing selection/navigation, symbol editing and Ctrl+drag. [RU/EN guide, native screenshots and SVG](editor-history.md).

### GUID, character insertion and clipboard viewer (2026-10-20)

Edit → Insert and clipboard adds Insert GUID (UUID v4), Insert ASCII / Unicode with Auto/decimal/hex/octal/binary codes and preview, and Show Clipboard. Insertions replace a single normal selection with one Undo and revalidate document id, generation, selection and permissions. Unicode scalars except NUL/surrogates extend the ASCII command; controls are literal. The noneditable/nonselectable clipboard viewer reads asynchronous UTF-8 snapshots up to 256 KiB with Refresh, singleton reuse and generation/lifetime guards. No image/HTML/RTF rendering or automatic clipboard history is claimed. Native Linux GUI covers UUID shape/uniqueness, radices/overflow, Unicode/controls, stale and closed targets, read-only, Undo/Redo, a real external-process clipboard provider, refresh/size/no-text handling and source-text preservation across RU/EN switching. [Guide, native screenshots and SVG](insert-tools.md).
