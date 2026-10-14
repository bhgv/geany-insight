# Сопоставление с Source Insight 4

Эталон: https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Window_Tour.htm
и https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Relation_Window.htm

Это проверяемая карта возможностей, а не заявление о полном совпадении.

| Область | Текущее состояние | Незакрытый объём |
| --- | --- | --- |
| File Symbols слева | Поиск, сортировка, общие цвета/значки, группировка и фильтр типов, счётчики, Context; проверен GUI Linux | Семантическая иерархия scope |
| Project Files справа | Manifest, поиск, сортировка, Context; диалог добавления/удаления файлов и деревьев, Remove all, исключения, DnD, импорт/экспорт списка | Автоматическое слежение за Master File List и подробная GUI-проверка |
| Project Folders | Дерево, раскрытие/сворачивание, навигация | Управление виртуальными группами |
| Project Symbols | Теги Geany, поиск, сортировка, цвета/значки, группировка и фильтр типов, независимые настройки | Семантическое разрешение типов и перегрузок |
| Context снизу | Цветной Scintilla, номера строк, центрирование, подчёркнутые ссылки с курсором-пальцем и обычным кликом, история, поиск | Подробная проверка GUI на целевых ОС |
| Relation Window | Определения/объявления, прямые члены scope, прямые Base/Derived по тегам, лексические ссылки, Calls текущей функции и Call sites проекта, Follow symbol / enclosing function, граф одного уровня с масштабом и двумя ориентациями | Семантические вызовы/вызывающие функции, разрешение типов/aliases, раскрываемые деревья связей |
| Overview Scroller | Цветная миниатюра файла, видимая область/выделение, прокрутка мышью и колесом, масштаб/ширина, сохранение параметров, отметки закладок и изменённых/сохранённых строк, переходы по блокам правок, текущая/все функции с фоновыми лексическими границами; проверен GUI Linux | Вложенные блоки, языки с границами по отступам и семантические диапазоны |
| Панели | Десять отдельных float/dock и сохранение геометрии | Произвольное перетаскивание и snap-to-edge |
| Symbol Info | Отдельное окно: кандидаты, сведения тегов, цветной исходник, Jump с проверкой снимка, References, Leave file open и копирование | Метрики, вывод типов и GUI-проверка |
| Window List | Немодальный список открытых буферов, поиск, MRU/имя/путь, ближайший символ, Context, Activate/Save/Close, сохранение геометрии/фильтра/сортировки/колонок; проверен GUI Linux | Docking |
| Раскладки A–D | Пользовательские снимки и восстановление | Детальная сверка всех параметров окон |
| Bookmarks / Clips | Постоянное хранилище, навигация, вставка и редактирование Clips, переименование, импорт/экспорт выбранной панели | Переносимые пути; движущиеся маркеры, синяя метка и немодальный список реализованы |
| Шаблоны кода | Clips: создание, ввод пользовательских переменных, дата/файл/проект/выделение/курсор, языковые профили/фильтр списка, один Undo/Redo, импорт/экспорт, общие значки меню/панели | Отдельные базы проекта, автодополнение и редактирование placeholder после вставки |
| Структурное выделение | Меню Edit, расширение по вложенным скобкам, Select Match, Select Line, Select Function и Function Up/Down; строки и комментарии исключаются лексической маской | Выделение полного символа/функции, блоки отступов, семантические границы, GUI-проверка |
| Поиск и изменение | Geany Find/Replace, Unicode/regex, Boolean Keywords с контекстом строк, выбор области/масок, сохранение параметров и выбранных путей, отдельная Search Results с Shift+F8/F9, жёлтые совпадения с красной рамкой, подсветка похожего выделения | Морфологические вариации слов, семантическое переименование и комплексные рефакторинги |
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

An icon-only target button and local outline menu action select and reveal the caret declaration without updating Context or navigating the editor. Supported brace languages prefer the containing indexed function in buffers up to 2 MiB; other cases use the nearest preceding indexed declaration. Hidden/unloaded results report a status message without clearing filters or increasing the loaded portion. Native GUI covers both commands, group expansion, boundary/fallback cases, hidden results and RU labels.


### Persistent symbol column layouts (2026-10-14)

Both symbol outlines support resizing and reordering headers, fixed widths and ellipsized values with horizontal scrolling. Each panel saves its complete column layout independently. Validated restore ignores incomplete or duplicate layouts; Reset symbol columns affects only that panel's widths and order, preserving query, type filters and sorting. Native GUI covers restore/write, invalid settings, grouping/query rebuilds, reset and RU labels.


### Symbol query history (2026-10-14)

Both outlines provide an icon-only recent-query menu, explicit Remember and recording on Enter. Each independently persists 16 distinct valid queries up to 256 UTF-8 bytes. Choosing a query changes only the filter without activating a result; clearing history preserves the current filter. Query literals are excluded from UI translation. Native GUI tests verify restore, MRU bounds, invalid queries, both recording paths, non-navigating selection, RU commands and independent clear/write.


### Twenty outline and file-list improvements (2026-10-14)

Exact quoted path/scope queries, qualified-name/path/line clipboard commands, selected-symbol filters, query clearing, individual group controls, category inversion and persistent column visibility extend both symbol outlines. Qualified names use the active native parser when the source is open.

Project Files now has a non-navigating right-click menu, scoped keyboard commands, two-way synchronized sorting, independent column layout/visibility, saved preferences, file-aware query history and matching-file counters. Document events refresh after lifecycle changes; formatted counters update on UI language changes.

Backend lookup tests and a native C++ GUI scenario cover the new commands and settings. Seven existing GUI suites cover menus, columns, history, paging, pane sizing, overview/navigation and localization. Updated offline RU/EN tutorials explain all controls; project enumeration and list limits remain explicit.
