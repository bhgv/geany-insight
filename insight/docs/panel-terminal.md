# Terminal

An embedded terminal, when available in this build.

## Controls

The terminal runs local shell commands. Availability depends on platform and VTE support. Use the separate SSH panel for remote commands.

## Short tutorial

Select Terminal and run a command showing the current directory. For SSH, open its dedicated panel.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [panel-ssh.md](panel-ssh.md).


[ИИ-помощник консоли: настройка, разбор ошибок и исправления через Diff/Merge](console-ai.md).


## Unified terminal input/output

SSH now uses a single black VT100/xterm input/output screen with colors and native selection instead of a separate command entry. Right-hand icon tabs show a plug, file transfer and numbered terminals, with tooltip names. Local VTE also uses a black background and middle-click selection/clipboard fallback. See [TTY / COM](panel-tty.md) for the serial panel and current limits, including dynamic SSH/TTY dimensions and configurable 10,000-line history.

## Общая группа терминалов и плавающее окно Ai

Нижняя вкладка **Терминалы** объединяет **Локальный / SSH / TTY**. Контекст остаётся отдельной вкладкой. На экране каждого терминала кнопка **Ai** имеет непрозрачность 80%: перетащите её в свободное место; после изменения размеров она остаётся внутри экрана.

Ai открывает отдельное изменяемое по размеру окно с обычными кнопками управления окнами, промптом, ответами, настройками моделей и вложениями. В обычном режиме оно связано с главным окном; в режиме независимых окон не имеет родительского окна. Повторное нажатие поднимает то же окно и обновляет контекст, если запрос сейчас не выполняется. Закрытие исходного терминала закрывает его помощника. Это плавающее системное окно; отдельный внутренний MDI-контейнер не добавлен.

Кнопки **Ошибки → исходник**, **Помощь в шелле**, **Объяснить вывод**, **Помощь с подключением** заполняют промпт для редактирования. Список последних запросов и четыре наиболее частых запроса общие для терминальных помощников: до 128 уникальных запросов в памяти до выхода из приложения, без сохранения на диск. Выбор истории или кнопки не отправляет запрос: нажмите **Отправить** явно. История полученных ответов остаётся отдельной, до 32 ответов на окно.

Для SSH/TTY берётся выделенный текст либо последние 12 000 символов; локальный терминал предоставляет выделение либо доступный журнал. Итоговый контекст ограничен 60 КиБ и доступен для проверки и редактирования. Исходники прикладываются явно. Предложения кода проверяются в **Diff / Merge**, вставка команды сама её не выполняет. Для TTY учитывайте протокол устройства: команда оболочки может быть неприменима к COM-порту.
