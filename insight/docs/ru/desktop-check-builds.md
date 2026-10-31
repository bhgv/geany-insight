# Проверочные сборки Geany Insight

## Текущий native-комплект Окноs x64

Распакуйте весь архив в каталог и запустите `Start-Insight.cmd`. Launcher использует `bin/geany.exe`, GTK и зависимости из этого же комплекта и создаёт отдельный `config`. Обычная установка MSYS2 для запуска не требуется. Проверяйте `REVISION.txt`: в нём записан коммит, из которого собран именно этот архив.

Современная native-сборка содержит сам изменённый редактор: Контекст, панели, Lua, Сравнение / Сведение и справка встроены в него. Для меню Скрипты включать отдельный Insight Lua plugin не требуется. Ранние комплекты на основе официального Geany 2.1 и `insight_lua.dll` относятся к другой, плагинной схеме; их инструкции и набор возможностей не следует переносить на новую сборку.

Справка устанавливается в `data/insight/docs` в Окноs-комплекте. Кнопки документации/уроков и Справка открывают локальные файлы без сети. GIF использует включённые GdkPixbuf loaders; launcher обновляет их cache для фактического пути комплекта. Для HTML5-видео мультимедийный просмотр вызывает системный браузер.

## Ручная проверка интерфейса

1. Откройте Справка → Geany Insight — Документация… и Учебные материалыs…; повторите открытие кнопками книжек.
2. Убедитесь, что при запуске текстовые панели поиска скрыты. Ctrl+F и Ctrl+H открывают поиск и замену, кнопки панели вызывают те же функции.
3. Поиск/замена → Поиск в проекте… показывает запрос и результаты; Поиск в папке… предлагает папку. Крестик закрывает панель, Escape закрывает строку запроса.
4. Откройте исходник проекта. Один клик в Контекст меняет только предпросмотр; двойной открывает файл/строку в редакторе.
5. Двойной клик по номеру строки ставит или снимает синюю закладку. Книжка с закладкой показывает общий список.
6. Инструменты → Сравнение / Сведение… запускает встроенное окно без установленного KСравнение3. Проверьте Принять слева/Right, ручную правку, Undo/Redo и Save result.
7. Скрипты → Загрузить расширение… загружает собственное расширение из [урока](scripting.md). Проверьте вызов команды и Undo.
8. Откройте свой Markdown/RST с изображением и GIF. Video / Multimedia view должен открыть HTML-страницу с видеоплеером в браузере, если его кодеки поддерживают входной файл.

## Сборка из исходников

Нужны C/C++, Meson/Ninja, pkg-config, GTK3/GLib development packages и gettext. Для Окноs используйте MSYS2 UCRT64 с GTK3.

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

На Окноs исполнение и проверку GUI выполняйте в Окноs-сессии. Успешная Linux GUI-проверка или факт компиляции Окноs PE не подтверждают работу Окноs GUI сами по себе. Результаты относятся к конкретному коммиту, а не к ранее скачанному архиву.

Windows workflow запускается вручную (`workflow_dispatch`) или явным изменением `insight/build-requests/windows-x64-request.txt` в main по запросу проверочной сборки. Обычные коммиты его не запускают. Сценарий собирает редактор, упаковывает GTK, проверяет diff/структурное выделение/шаблоны/поиск/фильтры/геометрию графа, запускает `geany --version` без SDK в PATH и включает `REVISION.txt`.

## Сборка без заголовка charconv

Ошибка `scintilla/src/Selection.cxx: fatal error: charconv: Нет такого файла`
возникает с некоторыми реализациями стандартной библиотеки C++17. Scintilla
теперь разбирает сохранённые позиции и индексы выделений собственным десятичным
парсером с проверкой переполнения. Устанавливать или копировать `<charconv>` в
проект не требуется. Требование C++17 для остальных частей редактора сохраняется.

После обновления исходников повторите `make` в существующем каталоге сборки.
При изменении файлов Autotools в Git-версии сначала выполните `./autogen.sh`
и повторите свою команду `configure`. Не подменяйте системные заголовки STL.
Проверка `SANITIZE=undefined python3 insight/tests/selection_parse_test.py`
запрещает включение `<charconv>` и проверяет диапазоны signed/unsigned,
переполнение, суффиксы, 20 000 случайных преобразований и восстановление выделений.

## Ошибки линковки Autotools и GLib 2.56

Сообщения `undefined reference to insight_file_patterns_*`, `insight_overview_*`,
`insight_remote_ui_*`, `insight_search_state_*` и `insight_keyword_collect`
означали, что соответствующие модули отсутствовали в `src/Makefile.am`. Теперь
список включает их, зависимости SSH и устанавливаемый `geany-insight-askpass`.
Проверка `python3 insight/tests/build_sources_test.py` сравнивает исходники
Insight в Autotools и Meson, чтобы новое расхождение останавливало CI.

Для заявленной базовой GLib 2.56 добавлены обёртки: маски используют старый API
при отсутствии `g_pattern_spec_match_string`, а нормализация путей — локальный
GFile при отсутствии `g_canonicalize_filename`. Это лексическая нормализация без
чтения файлов или разрешения символьных ссылок. Системные библиотеки подменять
не нужно. CI проверяет полный цикл Autotools: генерацию, отдельный каталог сборки,
линковку, установку, отсутствие неопределённых символов и запуск `--version`.
Отдельный тест компилирует обёртки с настоящими заголовками и библиотекой GLib 2.56.

После обновления Git-исходников обновите и сгенерированные Makefile. Для уже
настроенной сборки в каталоге исходников:

```sh
git pull --ff-only
NOCONFIGURE=1 ./autogen.sh
./config.status --recheck
./config.status
make -j2
```

`config.status --recheck` сохраняет прежние параметры configure. При сборке в
отдельном каталоге запускайте autogen из исходников, а config.status и make —
из существующего каталога сборки.

### Очистка очереди SSH и контроль минимального API

`undefined reference to g_queue_clear_full` исправлен очисткой встроенной
очереди через API, доступный в GLib 2.56. Тест проверяет освобождение каждого
элемента, пустую очередь и её повторное использование. Проверка Autotools теперь
собирает весь редактор с `GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56` и считает
использование более нового API ошибкой компиляции. Совместимые ветки выбираются
с учётом этого ограничения, даже при сборке с современными заголовками.

## Minimum GTK dependency / Минимальная версия GTK

Both Meson and Autotools require **GTK >= 3.22.0**, with GLib/GIO >= 2.56. The editor uses the GTK 3.22 popup placement API (`gtk_menu_popup_at_pointer`, `gtk_menu_popup_at_widget`, `gtk_menu_popup_at_rect`), so earlier GTK versions are insufficient. GTK 3.24 is not required. CI compiles and links the complete editor against real GTK 3.22 / GLib 2.56 packages in Ubuntu 18.04, in addition to the current Linux build.

[GTK popup API version](https://docs.gtk.org/gtk3/method.Menu.popup_at_pointer.html)


The internal source-built Clang SDK is required by default: prepare it with `python3 insight/tools/build_external_clang.py`. System Clang requires `-Dclang-provider=system` or `--with-clang-provider=system`. Internal Linux/Windows Meson installs bundle their runtime automatically (Linux needs patchelf). Minimal builds use `-Dclang=disabled` or `--without-clang`. See [clang.md](clang.md).

Внутренний SDK — вариант по умолчанию. Проверки системных библиотек в CI задают `clang-provider=system` явно; сборка из external проверяет обычные параметры Meson и Autotools без выбора SDK.
