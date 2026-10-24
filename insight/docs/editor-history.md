# История переходов и правок

## Selection History

Откройте **Edit → Selection and navigation → Selection History…**. Список показывает недавние переходы в именованных открытых файлах: имя файла, номер строки и визуальную колонку (от 1), фрагмент исходника. Полный путь виден в подсказке. Метка ● обозначает текущее место, если оно есть в списке. Двойной щелчок или **Go To** восстанавливает выбранную позицию. **Refresh** перечитывает список; **Close** закрывает окно без перехода.

История общая с существующими командами **Назад/Вперёд**. Выбор записи не стирает доступные переходы вперёд. Новый обычный переход из середины очереди создаёт новую ветку, как и раньше. Вставки сдвигают сохранённые позиции, удаление содержащего их текста сводит позицию к началу удалённого участка. Undo удаления не восстанавливает точную прежнюю позицию внутри удалённого текста.

Очередь ограничена 512 записями; текущее место вне очереди может появиться дополнительной строкой. Закрытые файлы удаляются. В случае изменения истории во время открытого окна первый Go To обновляет список; выберите запись заново. Это история навигационных команд и текущей позиции, а не запись каждого движения мыши, всех выделений или списка символов. Неименованные буферы и сохранение истории между запусками пока не поддержаны.

Реальное окно Linux с переходами между двумя файлами:

![История переходов](media/selection-history.jpg)

## Undo All / Redo All

В **Edit** добавлены **Undo All** и **Redo All**. Они проходят всю доступную историю текущего документа до самого раннего или самого позднего сохранённого шага. Это не перечитывание файла с диска и не остановка у последнего Save: границы задаёт доступная история Scintilla.

Выполнение идёт порциями, окно показывает прогресс. **Cancel** останавливает дальнейшие шаги; уже выполненные изменения сохраняются, а доступные Undo/Redo остаются пригодны для продолжения. Обычные границы групп Undo, состояние сохранённости документа и переводы строк сохраняются. Новая правка после Undo, как обычно, создаёт ветку и удаляет старую ветку Redo.

Read-only и незакрытая группа Undo запрещают запуск. Закрытие/смена документа, новая правка или переход в read-only во время выполнения останавливают команду. Файлы на диске не перезаписываются.

![Самое раннее и самое позднее доступное состояние](media/edit-history.svg)

Реальное окно массовой отмены в Linux:

![Прогресс Undo All](media/bulk-undo.jpg)

Эталон: [Selection History](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Selection_History.htm), [Undo All](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Undo_All.htm).

## Go Back Toggle / Переключить последний переход

Edit → Selection and navigation → Go Back Toggle alternates normal Back and
Forward over the latest pair. A normal Back/Forward command, a different caret or
active document, a text edit or a changed/pruned history starts a fresh Back pair.
Failed boundary moves do not arm a toggle. Untitled buffers have no queue identity
and do not start a pair. The existing open-file history, edit anchors and Extend
Mode behavior remain in use. No bytes, clipboard or Undo are changed. A new
unassigned Go to keybinding allows a user shortcut without replacing defaults.

Повторная команда переключает две последние позиции через штатные Back/Forward.
Ручной переход, смена курсора/документа или изменение истории начинает новую пару.
На границе истории команда ничего не меняет. Новые несохранённые буферы без имени
не создают пару. В настройках клавиш Go to можно назначить своё сочетание.

Reference: [Go Back Toggle](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Go_Back_Toggle.htm).


Read-only source browsing: [Browser Mode](browser-mode.md).
