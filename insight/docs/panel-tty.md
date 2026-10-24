# TTY / COM serial terminal

TTY Terminal sits next to SSH Terminal in the bottom shelf. Its right-hand icon tabs contain Connection and numbered terminals. The plug icon opens connection parameters. There is no transfer tab. **+** adds an independent terminal, up to 16.

![Terminal transports](media/terminals.svg)

The connection page refreshes the port list whenever opened, and Refresh ports repeats enumeration. Windows lists COM1–COM256 reported by the system; Unix lists conventional ttyS/ttyUSB/ttyACM/rfcomm and macOS tty./cu. device names in /dev. The editable list accepts an explicit device name (including a PTY for testing). Enumeration reads names without opening hardware. A refresh retains a manually entered device.

Choose baud rate (300–230400 from the supported list), 5/6/7/8 data bits, 1/2 stop bits, None/Even/Odd parity, and optional Local echo, then Connect. Defaults are 115200, 8N1, echo off. Settings apply to the selected terminal when connecting. The driver can reject unsupported combinations. Disconnect closes only that terminal. Hardware/software flow control is disabled; there is no modem-control, BREAK, file-transfer protocol or saved connection profile yet. Unix device permissions remain under the OS; the editor does not change them.

Input and output share one black terminal screen, with native text selection, Ctrl+Shift+C/V, Shift+Insert, a Copy/Paste context menu and middle-click paste. Middle click sends the terminal selection first, otherwise PRIMARY selection, falling back to the clipboard when PRIMARY is empty. Pasting can send newlines; remote programs decide whether these execute commands. Bracketed paste is used when requested by the remote application.

SSH and TTY share bundled MIT libvterm 0.3.3: UTF-8, VT100/xterm, 16/256/RGB colors, alternate screen and terminal keys. The screen follows the available panel area instead of fixed 80×24 dimensions. Unix SSH uses a real local PTY and forwards window-size changes to the server. On Windows the field resizes too, but the current OpenSSH pipe transport does not forward subsequent remote PTY size changes. Serial devices have no standard window-size negotiation protocol. Scrollback defaults to 10,000 lines beyond the visible screen, without the former 4 MiB cutoff; the configurable range is 0–100,000 lines. Memory use depends on line width and colors. Historical lines keep their original wrapping when width changes. The mouse selects text; OSC clipboard requests are not connected to the system clipboard.

COM uses Windows overlapped I/O; Unix uses nonblocking termios and restores original settings on close. Input queues are bounded to 1 MiB. A read/write failure closes the connection and reports the problem. UTF-8 is the current terminal encoding; binary file transfer and legacy DOS code pages are outside this stage. Linux validation uses a real pseudo-terminal pair and loopback OpenSSH; physical RS-232 hardware and Windows runtime were not tested.

See [SSH](panel-ssh.md) and [local terminal](panel-terminal.md).


## Terminal menu and settings

Right-click exposes Copy, Cut, Paste, Select All, Search, Clear, Save to file and Settings. Search is literal, case-insensitive, forward/backward and wraps through retained text. Save to file writes an immutable snapshot of the complete current buffer as plain UTF-8 without ANSI codes; the chooser confirms overwrites. Clear resets the local screen/history without sending shell commands. Settings changes history depth for all open SSH/TTY terminals and future terminals, saved in insight/terminal.conf. Reducing it discards the oldest lines. Cut is enabled for a selected ASCII suffix ending at the terminal cursor: it copies the text and sends the corresponding Backspace keys. Older output is immutable. Mouse wheel/scrollbar and Shift+PageUp/PageDown browse history. New output preserves a scrolled-back viewport.

## Общая группа терминалов и плавающее окно Ai

Нижняя вкладка **Терминалы** объединяет **Локальный / SSH / TTY**. Контекст остаётся отдельной вкладкой. На экране каждого терминала кнопка **Ai** имеет непрозрачность 80%: перетащите её в свободное место; после изменения размеров она остаётся внутри экрана.

Ai открывает отдельное изменяемое по размеру окно с обычными кнопками управления окнами, промптом, ответами, настройками моделей и вложениями. В обычном режиме оно связано с главным окном; в режиме независимых окон не имеет родительского окна. Повторное нажатие поднимает то же окно и обновляет контекст, если запрос сейчас не выполняется. Закрытие исходного терминала закрывает его помощника. Это плавающее системное окно; отдельный внутренний MDI-контейнер не добавлен.

Кнопки **Ошибки → исходник**, **Помощь в шелле**, **Объяснить вывод**, **Помощь с подключением** заполняют промпт для редактирования. Список последних запросов и четыре наиболее частых запроса общие для терминальных помощников: до 128 уникальных запросов в памяти до выхода из приложения, без сохранения на диск. Выбор истории или кнопки не отправляет запрос: нажмите **Отправить** явно. История полученных ответов остаётся отдельной, до 32 ответов на окно.

Для SSH/TTY берётся выделенный текст либо последние 12 000 символов; локальный терминал предоставляет выделение либо доступный журнал. Итоговый контекст ограничен 60 КиБ и доступен для проверки и редактирования. Исходники прикладываются явно. Предложения кода проверяются в **Diff / Merge**, вставка команды сама её не выполняет. Для TTY учитывайте протокол устройства: команда оболочки может быть неприменима к COM-порту.
