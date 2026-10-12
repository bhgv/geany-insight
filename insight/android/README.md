# Android frontend — 0.3

Первый минимальный редактор Android, а не перенос GTK-интерфейса Geany.
Общее C-ядро Lua и модели документа используется через JNI.

## Возможности

* Один редактируемый документ, моноширинный текст и выделение.
* Open / Save / Save as через Storage Access Framework; без разрешений на весь диск.
* UTF-8, кириллица и emoji. Некорректный UTF-8 и NUL отвергаются.
* Lua: выбрать файл скрипта кнопкой Lua. Доступны editor.text(),
  editor.selection(), editor.replace_selection().
* Скрипт выполняется на отдельной копии документа; ошибка не изменяет текст UI.
* Одношаговый Undo для обычной правки или целого успешного Lua-скрипта.
* Файловые операции и Lua вне UI-потока. Пока операция выполняется, редактирование
  блокируется. Состояние удерживается при пересоздании Activity.
* Черновик сохраняется атомарно в приватном каталоге приложения на lifecycle
  checkpoints и восстанавливается при запуске. Undo после перезапуска не сохраняется.
* Подтверждение отбрасывания несохранённых изменений при открытии и выходе.

Ограничения: 2 MiB на документ, 256 KiB на Lua-файл. Работает только UTF-8;
выбора кодировки, подсветки, вкладок и индексации Android-проектов пока нет.
project.symbols() на Android пока возвращает ошибку отсутствия индекса.
Заголовок пока не показывает имя выбранного файла. Скриптовые команды/события ещё
не подключены. Это прототип, не редактор с возможностями Source Insight.

## Сборка через Gradle

JDK 17, Gradle wrapper 8.9, AGP 8.7.3, SDK 35, Build Tools 35.0.0,
NDK 30.0.16248370 и Android CMake 3.22.1. Минимальная ОС Android 8 (API 26).
ABI: arm64-v8a, armeabi-v7a, x86_64.

Откройте этот каталог в Android Studio или укажите SDK в local.properties:

```properties
sdk.dir=/absolute/path/to/android-sdk
```

```sh
./gradlew :app:assembleDebug
```

Результат: app/build/outputs/apk/debug/app-debug.apk. Gradle-конфигурация ещё не
прошла полную сборку в окружении автора: JVM здесь не имеет прямого сетевого
доступа. Wrapper добавлен из официального Gradle 8.9 с checksum дистрибутива.

## Проверенная сборка без Gradle

Поскольку приложение использует только Android framework и C-ядро, его можно
собрать напрямую официальными инструментами SDK/NDK:

```sh
python3 ../tests/build_android.py \
 --ndk /path/to/android-sdk/ndk/30.0.16248370 \
 --cmake /path/to/android-sdk/cmake/3.22.1/bin/cmake \
 --ninja /path/to/android-sdk/cmake/3.22.1/bin/ninja \
 --build-tools /path/to/android-sdk/build-tools/35.0.0 \
 --android-jar /path/to/android-sdk/platforms/android-35/android.jar \
 --jdk /path/to/jdk17
```

Получается ../build-android-apk/geany-insight-0.3-debug.apk. Создаётся локальный
отладочный ключ; этот ключ и промежуточные файлы исключены из Git. Для новой
сборки на другом компьютере отладочная подпись будет другой.

Проверено: native-компиляция NDK для всех трёх ABI; Java с Android API 35 и
-Xlint:all -Werror; преобразование D8; упаковка AAPT2; подпись APK v2/v3;
zipalign для 16 KiB-выравнивания. На устройстве/эмуляторе ещё не проверено.

## Проверки JNI на обычной JVM

```sh
JAVA_HOME=/path/to/jdk17 python3 ../tests/native_bridge.py
```

Тест использует -Xcheck:jni, проверяет кириллицу/emoji, выделение, ошибки,
бюджет исполнения и откат. Это не проверка Android ART или lifecycle.

## Что проверить на устройстве

Открыть UTF-8 файл через системный диалог, изменить текст, сохранить и перечитать;
проверить Save as, отказ провайдера в записи, Undo, Lua на выделении, Lua с ошибкой,
поворот экрана, фон/возврат и восстановление черновика после завершения процесса.
Сохранение черновика выполняется на checkpoints, не после каждой клавиши;
аварийное завершение может потерять правки после последнего checkpoint.
Завершение операции ввода-вывода после ухода Activity пока требует отдельной
проверки восстановления. Перезапись файла зависит от провайдера SAF и не является
атомарной: ошибка записи может оставить неполный файл. Внешние изменения файла
автоматически не отслеживаются.

## Источники по платформе

* https://developer.android.com/training/data-storage/shared/documents-files
* https://developer.android.com/ndk/guides/jni-tips
* https://developer.android.com/build/releases/agp-8-7-0-release-notes
* https://developer.android.com/ndk/downloads
