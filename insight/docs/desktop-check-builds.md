# Проверочные сборки Geany Insight

## Текущий native-комплект Windows x64

Распакуйте весь архив в каталог и запустите `Start-Insight.cmd`. Launcher использует `bin/geany.exe`, GTK и зависимости из этого же комплекта и создаёт отдельный `config`. Обычная установка MSYS2 для запуска не требуется. Проверяйте `REVISION.txt`: в нём записан коммит, из которого собран именно этот архив.

Современная native-сборка содержит сам изменённый редактор: Context, панели, Lua, Diff / Merge и справка встроены в него. Для меню Scripts включать отдельный Insight Lua plugin не требуется. Ранние комплекты на основе официального Geany 2.1 и `insight_lua.dll` относятся к другой, плагинной схеме; их инструкции и набор возможностей не следует переносить на новую сборку.

Справка устанавливается в `data/insight/docs` в Windows-комплекте. Кнопки документации/уроков и Help открывают локальные файлы без сети. GIF использует включённые GdkPixbuf loaders; launcher обновляет их cache для фактического пути комплекта. Для HTML5-видео мультимедийный просмотр вызывает системный браузер.

## Ручная проверка интерфейса

1. Откройте Help → Geany Insight — Documentation… и Tutorials…; повторите открытие кнопками книжек.
2. Убедитесь, что при запуске текстовые панели поиска скрыты. Ctrl+F и Ctrl+H открывают поиск и замену, кнопки панели вызывают те же функции.
3. Поиск/замена → Search project… показывает запрос и результаты; Search folder… предлагает папку. Крестик закрывает панель, Escape закрывает строку запроса.
4. Откройте исходник проекта. Один клик в Context меняет только предпросмотр; двойной открывает файл/строку в редакторе.
5. Двойной клик по номеру строки ставит или снимает синюю закладку. Книжка с закладкой показывает общий список.
6. Tools → Diff / Merge… запускает встроенное окно без установленного KDiff3. Проверьте Take Left/Right, ручную правку, Undo/Redo и Save result.
7. Scripts → Load extension… загружает собственное расширение из [урока](scripting.md). Проверьте вызов команды и Undo.
8. Откройте свой Markdown/RST с изображением и GIF. Video / Multimedia view должен открыть HTML-страницу с видеоплеером в браузере, если его кодеки поддерживают входной файл.

## Сборка из исходников

Нужны C/C++, Meson/Ninja, pkg-config, GTK3/GLib development packages и gettext. Для Windows используйте MSYS2 UCRT64 с GTK3.

```sh
meson setup build-editor --prefix="$PWD/install" --libdir=lib \
  -Dhtml-docs=disabled -Dpdf-docs=disabled -Dapi-docs=disabled \
  -Dgtkdoc=false -Dvte=false -Dmac-integration=disabled
meson compile -C build-editor
meson install -C build-editor
```

Markdown/RST Insight устанавливается независимо от `html-docs`: этот флаг относится к стандартному руководству Geany. Для переносного Linux-комплекта флаг `-DINSIGHT_RELOCATABLE` в c_args/cpp_args включает чтение абсолютного `GEANY_INSIGHT_PREFIX`, который задаёт launcher. GTK не обязательно включать в такой Linux-комплект; тогда он должен быть установлен в целевой системе.

Проверки ядра:

```sh
python3 insight/tests/run.py
python3 insight/tests/diff_test.py
```

GUI-тесты запускаются в настоящей графической сессии или подготовленном виртуальном дисплее. Они возвращают skip без `GEANY_RUN_GUI_TESTS=1` и DISPLAY/WAYLAND_DISPLAY. Сначала установите редактор и ресурсы указанным prefix.

```sh
GEANY_RUN_GUI_TESTS=1 meson test -C build-editor \
  'Insight Help and on-demand Search GUI' \
  'Insight Symbol Info and Window List GUI' \
  'Insight relation graph GUI'
```

На Windows исполнение и проверку GUI выполняйте в Windows-сессии. Успешная Linux GUI-проверка или факт компиляции Windows PE не подтверждают работу Windows GUI сами по себе. Результаты относятся к конкретному коммиту, а не к ранее скачанному архиву.

Windows workflow запускается вручную (`workflow_dispatch`) или явным изменением файла `insight/build-requests/windows-x64-request.txt` в main по запросу проверочной сборки. Обычные коммиты без этого файла не запускают сборку. Его сценарий включает native-сборку, упаковку GTK, проверку ядра diff/структурного выделения/геометрии графа, `geany --version` без SDK в PATH и `REVISION.txt`.

## Building without the charconv header

Some C++17 standard libraries report `scintilla/src/Selection.cxx: fatal error:
charconv: No such file or directory`. Selection restoration now uses a local,
range-checked decimal parser, so this code does not require `<charconv>`. There is
no replacement STL header to install or copy into the project. The editor still
requires C++17 for its other components.

After updating sources, rerun `make` in the existing build directory. If Autotools
inputs changed in a Git checkout, rerun `./autogen.sh` and your existing configure
command first. Do not replace system STL headers.
`SANITIZE=undefined python3 insight/tests/selection_parse_test.py` forbids including
`<charconv>` and verifies signed/unsigned limits, overflow, exact suffixes, 20,000
random conversions and selection restoration.

## Autotools link errors and GLib 2.56

Undefined references to `insight_file_patterns_*`, `insight_overview_*`,
`insight_remote_ui_*`, `insight_search_state_*` or `insight_keyword_collect`
were caused by missing runtime modules in `src/Makefile.am`. The source list now
includes them, the SSH dependencies and the installed `geany-insight-askpass`
helper. `python3 insight/tests/build_sources_test.py` checks Insight source parity
between Autotools and Meson so future omissions fail CI.

Wrappers preserve the declared GLib 2.56 baseline: wildcard matching uses the
older API when `g_pattern_spec_match_string` is unavailable; path normalization
uses a local GFile when `g_canonicalize_filename` is unavailable. This is lexical
normalization without reading files or resolving symbolic links. System libraries
need no replacement. CI generates an out-of-tree Autotools build, links and
installs the editor, checks unresolved symbols and runs `--version`. A separate
check builds the wrappers against actual GLib 2.56 headers and libraries.

After updating a Git checkout, regenerate Makefiles too. For an existing
in-source build:

```sh
git pull --ff-only
NOCONFIGURE=1 ./autogen.sh
./config.status --recheck
./config.status
make -j2
```

The recheck retains the previous configure arguments. With an out-of-tree build,
run autogen in the source directory and config.status/make in the existing build
directory.

### SSH queue cleanup and minimum-API enforcement

The `undefined reference to g_queue_clear_full` failure is fixed by clearing the
embedded queue with APIs available in GLib 2.56. Tests cover element destruction,
empty queues and reuse. The complete Autotools build now sets
`GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56` and treats newer API usage as a compile
error. Compatibility branches respect that limit even with modern headers.

## Minimum GTK dependency / Минимальная версия GTK

Both Meson and Autotools require **GTK >= 3.22.0**, with GLib/GIO >= 2.56. The editor uses the GTK 3.22 popup placement API (`gtk_menu_popup_at_pointer`, `gtk_menu_popup_at_widget`, `gtk_menu_popup_at_rect`), so earlier GTK versions are insufficient. GTK 3.24 is not required. CI compiles and links the complete editor against real GTK 3.22 / GLib 2.56 packages in Ubuntu 18.04, in addition to the current Linux build.

[GTK popup API version](https://docs.gtk.org/gtk3/method.Menu.popup_at_pointer.html)
