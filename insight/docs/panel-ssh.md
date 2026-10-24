# SSH

Remote files and commands through OpenSSH.

## Controls

Set host, user, port and key. Commands open a remote file, folder or project; Save uploads changes. Stop cancels a transfer. Connect/Disconnect controls the shell; Enter sends input and Ctrl+C interrupts. This is a line-oriented shell rather than a full terminal emulator.

## Short tutorial

Enter your server settings and connect. Open a known remote file. After editing, explicitly save it remotely or enable automatic upload.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [guide.md](guide.md).


[ИИ-помощник консоли: настройка, разбор ошибок и исправления через Diff/Merge](console-ai.md).

## Connection feedback

Connect starts an OpenSSH client, then the client authenticates with the server. Starting a process is not reported as a completed connection. The selectable status is on its own wrapping row; startup errors, disconnects and exit codes also remain in the console transcript. Read the original client output above a nonzero exit status for the server/network reason.

Clients shipped in the application bin directory are resolved to absolute paths before spawning; PATH and, on Windows, system OpenSSH locations are fallbacks. If the password dialog helper is missing, a visible message explains that only key/agent authentication is available. Password/host-key questions use the separate SSH dialog. Spaces around the host and login are ignored.

![SSH connection flow](media/ssh-connection.svg)

SSH-панель разделена на вкладки справа: подключение, доступ к файлам и независимые терминалы. Кнопка «+» добавляет терминал (до 16); у каждого свой процесс, очередь ввода и журнал. Кнопку Ai с непрозрачностью 80% можно перетащить по терминалу; она открывает встроенного помощника. Используются общие профили моделей и проверяемые предложения правок через дифф. Явная кнопка выполнения отправляет предложенную команду только в этот терминал.


### Передача файлов отдельно от терминалов

Во вкладке Transfer задайте абсолютные локальный и удалённый пути, File/Folder и упаковку. Send отправляет, Receive получает; для получения укажите новое имя, существующие локальные файлы не перезаписываются. Без упаковки используется отдельный SCP; gz упаковывает один файл, tar.gz — файл или папку. При получении с упаковкой gzip/tar запускается на сервере через отдельный SSH без PTY. Архивы сохраняются как архивы, автоматической распаковки нет. Stop отменяет упаковку/передачу, сохраняя подключения терминалов.

Для локальной упаковки нужен Python 3, для удалённой — gzip/tar на сервере. SCP-пути на сервере ограничены буквами ASCII, цифрами и `/._-`, чтобы оставаться безопасными и на старых клиентах; для остальных имён используйте существующее открытие через SFTP. Перед отправкой проверьте имя назначения: удалённый файл может быть заменён.

![SSH workspace](media/ssh-workspace.svg)


## Unified terminal input/output

SSH now uses a single black VT100/xterm input/output screen with colors and native selection instead of a separate command entry. Right-hand icon tabs show a plug, file transfer and numbered terminals, with tooltip names. Local VTE also uses a black background and middle-click selection/clipboard fallback. See [TTY / COM](panel-tty.md) for the serial panel and current limits, including the shared 80×24 SSH/TTY screen.
