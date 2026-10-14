# Проверочные сборки Geany Insight

## Текущий native-комплект Windows x64

Распакуйте весь архив в каталог и запустите `Start-Insight.cmd`. Launcher использует `bin/geany.exe`, GTK и зависимости из этого же комплекта и создаёт отдельный `profile`. Обычная установка MSYS2 для запуска не требуется. Проверяйте `REVISION.txt`: в нём записан коммит, из которого собран именно этот архив.

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

Windows workflow остаётся ручным (`workflow_dispatch`), чтобы обычные коммиты в main не вызывали сборочные уведомления. Его сценарий включает native-сборку, упаковку GTK, проверку ядра diff/структурного выделения/геометрии графа, `geany --version` без SDK в PATH и `REVISION.txt`.
