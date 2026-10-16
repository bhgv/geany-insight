# Папки проекта

Дерево каталогов индексированных файлов проекта. Это дерево manifest, а не обзор всех файлов диска; отображаются первые 10 000 файлов.

## Управление

Поле фильтра принимает фрагменты пути, glob (`*.c`), regex с начальным `?` и нечёткие условия (`~sc`). Несколько условий соединяются через пробел. Совпавшие файлы показываются вместе с папками-предками; при активном фильтре ветки раскрываются. Неверный regex отмечается ошибкой и сохраняет последний корректный результат. Счётчик показывает число показанных файлов и размер manifest.

Щелчок файла показывает Context. Двойной щелчок или Enter открывает файл; двойной щелчок папки раскрывает/сворачивает её. Правый клик выбирает строку и открывает меню без перехода Context или редактора. На пустом месте команды для файла недоступны.

Меню позволяет открыть файл, скопировать полный путь или путь относительно корня проекта, найти текущий файл, раскрыть/свернуть выбранную ветку или всё дерево, очистить фильтр и открыть справку. Для пути вне корня или проекта без корня копируется полный путь.

Кнопка с мишенью находит текущий файл, раскрывает предков и центрирует строку, не меняя редактор и Context. «Следовать за текущим файлом» делает это автоматически при смене документа; настройка сохраняется независимо от Follow в Context. Если файл скрыт фильтром или отсутствует в manifest, фильтр не сбрасывается. Ручная команда сообщает об этом в строке состояния.

Клавиши в дереве: Ctrl+F — фильтр; Ctrl+C — полный путь; Ctrl+Shift+C — относительный путь; Ctrl+Enter — открыть файл; Ctrl+L — найти текущий файл; Menu / Shift+F10 — меню; Escape — редактор. В поле фильтра Down выбирает первый файл, Enter открывает его, Escape сначала очищает запрос, затем возвращает редактору фокус. На macOS вместо Ctrl используется Primary.

## Короткий урок

1. Откройте проект и вкладку «Папки».
2. Введите `*.c` или `~sc`, проверьте счётчик и раскрытые ветки.
3. Правым кликом выберите файл, скопируйте относительный путь.
4. Очистите фильтр, откройте другой документ и включите слежение.
5. Сверните ветку и нажмите кнопку поиска текущего файла: строка снова станет видимой, каретка редактора останется на месте.

Кнопка «?» открывает эту страницу. Язык справки следует настройке интерфейса. Подробнее: [guide.md](guide.md).

### Disk file size and date

File Date is initially visible and File Size hidden, following the Source Insight
Folder Browser options. Dates show modification time in the local timezone as
`YYYY-MM-DD HH:MM:SS`; sizes use human-readable units. A zero-byte file shows zero;
folder/link sizes and unavailable metadata are blank. Metadata is captured in the
existing cancellable directory worker with no target traversal or recursive size
calculation. Refresh/F5 rereads it.

Click a column header to sort ascending, then descending. Size/date sorting uses
64-bit byte counts/timestamps, independent of formatted labels. Sorting applies
to the loaded bounded directory snapshot; filters and Follow retain their usual
behavior. Columns can be dragged/resized; names remain available. Unknown size/
time values sort before known values ascending and after them descending.

Adaptive layout remains separate work. Reference: [Source Insight Folder Browser Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Project_Folder_Browser_Options.htm).

### Folder Browser Options

In Browse disk, use **Folder Browser Options…**, its context-menu item or Ctrl+Q
while focus is inside the disk browser. The dialog collects Kind, File Size,
File Date, Follow current file and Show hidden disk entries. Apply publishes the
checkbox choices together; Cancel leaves them unchanged. Follow/hidden changes
request one refresh using the final settings, without changing project membership.
Existing direct controls remain synchronized and all choices use their existing
persistent settings.

Font & Color opens the existing independent appearance dialog. Its own Apply
commits appearance immediately; cancelling the outer options dialog does not
undo a separately applied appearance. Cancelling the appearance dialog changes
nothing. This dialog configures the disk list; project-tree Follow and appearance
remain independent.
