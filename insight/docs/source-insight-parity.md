# Сопоставление с Source Insight 4

Эталон: https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Window_Tour.htm
и https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Relation_Window.htm

Это проверяемая карта возможностей, а не заявление о полном совпадении.

| Область | Текущее состояние | Незакрытый объём |
| --- | --- | --- |
| File Symbols слева | Поиск, сортировка, общие цвета/значки, группировка и фильтр типов, счётчики, Context, вложенные tag scope по языку; проверен GUI Linux | Семантические владельцы scope |
| Project Files справа | Manifest, поиск, сортировка, Context; диалог добавления/удаления файлов и деревьев, Remove all, исключения, DnD, импорт/экспорт списка, двусторонний фоновый MFL со снимками/откатом и GUI-проверкой | Расширенные параметры синхронизации и Path Variables |
| Project Folders | Дерево manifest, фильтр/нечёткий поиск с папками-предками, счётчик, меню/клавиатура, копирование путей, Locate/Follow текущего файла, ветки; отдельный фоновый Browse disk с preview/open неиндексированных файлов, навигацией, пределами и сохранением состояния/колонок; проверен GUI Linux | Обслуживание файлов/каталогов, полные Folder Browser Options; аналог виртуальных групп требует подтверждения |
| Project Symbols | Теги Geany, поиск, сортировка, цвета/значки, группировка, вложенные tag scope и фильтр типов, независимые настройки | Семантическое разрешение типов и перегрузок |
| Symbol Categories | Языковые типы/цвета/значки, вложенные scope, прямые члены однозначных классов, фильтр, дозагрузка, счётчик, меню/клавиатура/Info/References, колонки | Семантическое наследование, aliases и неоднозначные владельцы |
| File Types | Профили открытых и закрытых файлов, фильтр путей, счётчик, меню/клавиатура, независимые колонки/сортировка | Детальная сверка всех File Type Options с Source Insight |
| Context снизу | Цветной Scintilla, номера строк, центрирование, подчёркнутые ссылки с курсором-пальцем и обычным кликом, история, поиск | Подробная проверка GUI на целевых ОС |
| Relation Window | Определения/объявления, прямые члены scope, прямые/транзитивные Base/Derived и наследуемые члены по однозначным тегам, лексические ссылки, Calls текущей функции и Call sites проекта, Follow symbol / enclosing function, граф одного уровня с масштабом и двумя ориентациями, ленивое tag-дерево и раскрываемый граф с историей веток и пределами, scoped nominal aliases корней/баз/членов | Семантические вызовы/вызывающие функции, полные типы/перегрузки и include semantics, многоуровневые вызовы/ссылки |
| Overview Scroller | Цветная миниатюра файла, видимая область/выделение, прокрутка мышью и колесом, масштаб/ширина, сохранение параметров, отметки закладок и изменённых/сохранённых строк, переходы по блокам правок, текущая/все функции с фоновыми лексическими границами; проверен GUI Linux | Семантические диапазоны и дополнительные языки за пределами brace/Python/Lua |
| Панели | Десять отдельных float/dock и сохранение геометрии | Произвольное перетаскивание и snap-to-edge |
| Symbol Info | Отдельное окно: кандидаты, сведения тегов, цветной исходник, Jump с проверкой снимка, References, Leave file open и копирование, размер завершённых функций, nominal-типы aliases из снимка, устойчивый Refresh и RU/EN GUI | Размеры не-функциональных символов и типы произвольных выражений; сложность не подтверждена как требование команды Symbol Info |
| Window List | Немодальный список открытых буферов, поиск, MRU/имя/путь, ближайший символ, Context, Activate/Save/Close, сохранение геометрии/фильтра/сортировки/колонок; закрепление в sidebar и видимость/docking/геометрия в A–D, миграция раскладок; проверен GUI Linux | Перетаскивание, snap-to-edge |
| Раскладки A–D | Пользовательские снимки и восстановление | Детальная сверка всех параметров окон |
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
| [Symbol Info](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Symbol_Info.htm) | Сведения определения, исходник, размер при наличии, Jump/References/Leave open | Основные действия и размер функции проверены GUI; неизвестные типы и другие виды размеров остаются ограниченными |
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
and close-file policies remain open, as does XML exchange.
