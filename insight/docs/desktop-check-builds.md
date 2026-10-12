# Проверочные сборки x64

Windows: распаковать ZIP в доступный для записи каталог; запустить
`Start-Insight.cmd`. Комплект включает официальный Geany 2.1 x64 и наш
собранный `lib/geany/insight_lua.dll`, GTK и зависимости из установщика Geany.
Это переносной проверочный комплект, не новый установщик и не подписанный
релиз Insight. Требуется Windows x64 и Windows PowerShell.

Linux: распаковать tar.gz; запустить `./start-insight.sh`. Редактор собран
из текущего дерева Geany 2.2 development; модуль находится в
`lib/geany/insight_lua.so`. Требуются glibc 2.38 или новее, системные
GTK3 >= 3.24 / GLib >= 2.80 и их зависимости (целевое окружение Ubuntu 24.04 x64).
GTK не включён в Linux-архив.

В обоих комплектах создаётся отдельный каталог `profile` рядом со скриптом.
Существующие пользовательские настройки Geany не изменяются. При переносе
уже использованного комплекта путь плагина в старом профиле может устареть:
снова включите Insight Lua через Tools > Plugin Manager.

## Ручная проверка

1. В Tools > Plugin Manager убедиться, что Insight Lua включён.
2. Открыть исходник с несколькими употреблениями `foo`, например
   `int foo; int foo_bar; void test() { foo = 1; }`.
3. Через Tools > Insight: Load Lua extension загрузить
   `examples/check_document.lua`.
4. Открыть палитру Primary+Shift+P, выбрать Insight: Check document.
   В статусе появится число совпадений `foo`; первое будет выделено.
5. Изменить исходник без сохранения и повторить команду: число обновится.
6. Проверить индексацию каталога и переходы через Project navigation.
7. Запустить `Run-Checks.cmd` на Windows или `./run-checks.sh` на Linux:
   восемь проверок ядра должны завершиться успешно.

## Повторная сборка

Linux-редактор:

```sh
meson setup build-editor --prefix=/opt/geany-insight --libdir=lib \
  -Dhtml-docs=disabled -Dpdf-docs=disabled -Dapi-docs=disabled \
  -Dgtkdoc=false -Dvte=false -Dc_args=-DINSIGHT_RELOCATABLE \
  -Dcpp_args=-DINSIGHT_RELOCATABLE
meson compile -C build-editor
DESTDIR="$PWD/stage" meson install -C build-editor
```

Флаг `INSIGHT_RELOCATABLE` включает чтение абсолютного
`GEANY_INSIGHT_PREFIX` для ресурсов; его устанавливает launcher.
Без флага стандартный механизм путей Geany сохраняется.

Сборка модуля и проверок с явным SDK:

```sh
python3 insight/tools/build_desktop_checks.py --platform linux \
  --geany-prefix stage/opt/geany-insight --gtk-prefix /usr \
  --gtk-libdir /usr/lib/x86_64-linux-gnu \
  --geany-library stage/opt/geany-insight/lib/libgeany.so --out build-checks
```

Для Windows: `--platform windows`, `--cc x86_64-w64-mingw32-gcc`,
Windows Geany SDK и GTK3 SDK, `--gtk-libdir <gtk>/lib`, а в
`--geany-library` передать import library для `libgeany-0.dll`. Если в
комплекте Geany нет `.dll.a`, её можно получить `dlltool` из списка экспортов
DLL. Использованные версии и результаты проверок находятся в BUILD-INFO.json
готового комплекта. Сборщик проверок не загружает зависимости и не запускает
Windows `.exe` на Linux.

Исходники Insight и редактора включены в Git bundle в каталоге source.
Windows-комплект также содержит исходники Geany 2.1 и сведения о зависимостях
официального комплекта.

## Проверено для этой сборки

Linux: восемь проверок ядра, восемь примеров штатных парсеров, запуск
`geany --version` и загрузка модуля динамическим загрузчиком проходят.
GUI-сценарий не проверен: подключение к виртуальному дисплею в среде сборки
недоступно. Windows: модуль и восемь проверок собраны для PE x64; все именованные
импорты модуля разрешаются в фактически включённых DLL. Выполнение Windows-кода
не подтверждено; Wine падает при инициализации до запуска теста. Проверки нужно
запустить на Windows через Run-Checks.cmd. Android APK не собирался.
