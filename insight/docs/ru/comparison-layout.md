## Shared comparison window layout / Единое оформление сравнений

Comparison/Merge, Directory Compare, snapshot comparisons, Git History and editor
AI review now share the same toolbar component: compact 2/3-size controls, scrollable
tabs at the top, and 86 px control pages. The default window size is 1100×700;
explicitly saved Directory Compare and Diff/Merge sizes remain respected. Source
fonts are independent of toolbar scaling. Excess controls scroll inside their page
instead of forcing the whole window wider or taller.

Directory tabs: Files and comparison; Comparison options; View (filters, masks,
columns); File operations; Navigation. Snapshot comparison groups comparison options,
report actions and navigation. Git groups revision controls and difference navigation.
Editor AI adds request, files, connection and request-history tabs, with the same
compact controls; it no longer forces a separate 1200×950 window. Read-only modes,
confirmation dialogs, cancellation and operation guards are preserved. Ctrl+F in
Directory Compare reveals the filter tab before focusing its input.

«Сравнение/сведение», сравнение каталогов и снимков, история Git и ИИ-проверка
используют общий компонент: верхние вкладки, уменьшенные в 1,5 раза элементы,
область параметров 86 px и начальный размер окна 1100×700. Явно сохранённые размеры
пользователя сохраняются. Шрифт исходника от масштаба параметров не зависит.
Длинные панели прокручиваются внутри вкладки, сохраняя место для содержимого.
У каталогов отдельные группы файлов, сравнения, вида/фильтров, файловых операций
и навигации; у ИИ — запроса, файлов, подключения и истории запросов.

![Shared comparison chrome](../media/compare-windows.svg)
