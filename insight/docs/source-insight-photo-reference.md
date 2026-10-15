# Разбор пользовательских фотографий Source Insight 4

Дата: 2026-10-15. Номера соответствуют фотографиям 01–10. Это требования
к дальнейшему переносу, а не утверждение о готовности всех перечисленных функций.

| Фото | Элемент | Назначение и проверяемое поведение |
| --- | --- | --- |
| 01, 02 | File Type Options | Профиль по маскам имени файла: язык разбора, экранный/печатный шрифт, отступы, переносы, автодополнение, пробелы, номера строк, правая граница, outline и overview. Открывается профиль текущего файла. |
| 02, 03, 10 | Symbol Window | Символы **текущего файла**, поиск имени, вложенные поля структур и классов, значки типов, переход к объявлению. В Source Insight также видны ветви `#ifdef/else/endif`. |
| 04, 05 | Project Files | Файлы **проекта**, поиск имени, отдельные столбцы имени, каталога, размера и даты изменения; выбор строки даёт предпросмотр в Context. |
| 06 | Project Symbols | Все символы проекта, включая одноимённые определения из разных файлов; фильтр по имени/фрагментам, видимое имя файла для различения совпадений. |
| 07 | Folders | Двухпанельный браузер: дерево каталогов слева и содержимое выбранного каталога справа. На фото виден диск целиком; это шире существующего дерева только файлов manifest в Geany Insight. |
| 08 | Symbol Categories | Дерево категорий с количеством: структуры, классы, функции, прототипы, поля, константы и т. д.; раскрытие категории показывает символы, структурного типа — его членов. |
| 09, 10 | Context Window | Автоматический предпросмотр определения/базового типа под курсором без ухода из основного редактора. В примере выбран `NotifierList`; видны `Notifier`, typedef и связанные объявления. Заголовок сообщает символ, тип, файл и строку. |
| 02, 07 | Overview Scroller | Миниатюра исходника справа; прямоугольник показывает видимую область. Перетаскивание прокручивает редактор. Опции включают выделение, закладки, изменения и границы функций. |
| 02, 04–08 | Панели и раскладки | Проектные страницы объединены вкладками; группы можно отделять и закреплять. Кнопки A–D переключают сохранённые раскладки. |

## Что непосредственно читается на фотографиях

В профиле C/C++: **Consolas 9**, tab width **4**, margin width **120**;
маски `*.c;*.h;*.cpp;*.hpp;*.cxx;*.hxx`. Включены номера строк, outline,
overview, autocomplete и snippets; перенос строк и преобразование tabs в spaces
выключены. Это настройки конкретного показанного профиля, не универсальные
значения Source Insight. Цвета по фотографии экрана нельзя считать точным RGB
экспортом; для точного переноса палитры нужен файл настроек либо значения цветов.

Жирный текст в Symbol Window не всегда означает текущий символ: руководство
предусматривает жирное начертание структурных типов и функций длиннее среднего
по файлу. Три первые нижние кнопки управляют сортировкой; шестерёнка открывает
настройки панели. Назначение остальных плохо читаемых пиктограмм нельзя надёжно
установить только по фото; не назначать им выдуманные команды.

Нижнее окно на 09/10 — Context, а не Relation Window. Relation — отдельное окно
вызовов, вызывающих функций, наследования и ссылок; на этих снимках его содержимое
не показано. Голубая плашка с китайским текстом поверх 04/05 не относится к
документированным панелям Source Insight; переносить её не требуется.

## Сопоставление с текущей реализацией

В workbench уже существуют File Symbols, Project Files, Project Symbols,
Project Folders, Symbol Categories, Context и Overview. Совпадение названия
не доказывает совпадение поведения. Приоритетные различия для проверки:

1. Полный профиль типа файла в одном диалоге и C/C++-профиль со шрифтом с фото.
2. Столбцы размера/даты и предпросмотр выбора в Project Files.
3. Настоящий двухпанельный дисковый браузер Folders вместо одной manifest-иерархии.
4. Ветви препроцессора и критерии выделения жирным в outline.
5. Подробный заголовок Context, история, фиксация и режимы отслеживания:
   символ под курсором, содержащая функция/класс, выключено; отдельно строки/комментарии.
6. Отделение/закрепление групп панелей и быстрые раскладки A–D.

Дополнительные настройки Context должны открываться по кнопкам с подсказками,
как просил пользователь; постоянные поля поиска и выбора не должны занимать
высоту предпросмотра. В Geany Insight вертикальный ряд кнопок справа является
согласованной адаптацией, хотя на фото Source Insight панель инструментов снизу.

## Проверенные источники

- [File Type Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/File_Type_Options.htm)
- [Symbol Windows](https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Symbol_Windows.htm)
- [Symbol Window Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Symbol_Window_Options.htm)
- [Project Window](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Project_Window_command.htm)
- [Project Symbol List](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Project_Symbol_List_Window.htm)
- [Project Folder Browser](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Project_Folder_Browser.htm)
- [Context](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Context_window1.htm)
- [Context Window Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Context_Window_Options.htm)
- [Overview Scroller](https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/The_Overview_Scroller.htm)
- [Overview Options](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Overview_Options.htm)
- [Panel Windows](https://www.sourceinsight.com/doc/v4/userguide/Manual/Window_Tour/Panel_Windows.htm)
- [Relation Window](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Relation_Window.htm)
