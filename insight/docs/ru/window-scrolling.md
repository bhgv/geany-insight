# Window scrolling / Прокрутка окна

Edit → Selection and navigation → Window scrolling provides six viewport commands.
Line Up/Down moves one display row; Half Page moves half the visible display rows
(at least one). Left/Right moves one tab width measured using the default font.
The caret, every selection, text, savepoint and Undo remain unchanged, including
read-only buffers and rectangular/multiple selections. Native scroll limits apply.
No default shortcuts override Geany navigation or document switching.

![Viewport and selection](../media/window-scrolling.svg)

Правка → Выделение и навигация → Прокрутка окна: строка вверх/вниз,
полстраницы вверх/вниз и влево/вправо на ширину табуляции. Курсор и все
выделения сохраняются, даже вне видимой области. Текст и история Undo
не меняются; команды доступны в режиме только чтения.

Reference: [Source Insight scrolling commands](https://www.sourceinsight.com/doc/v4/userguide/Manual/Concepts/Scrolling_Commands.htm).
