# SSH

Remote files and commands through OpenSSH.

## Controls

Set host, user, port and key. Commands open a remote file, folder or project; Save uploads changes. Stop cancels a transfer. Connect/Disconnect controls the shell; Enter sends input and Ctrl+C interrupts. Input and output share one VT100/xterm-compatible terminal screen; screen dimensions follow the panel (see TTY / COM for history settings).

## Short tutorial

Enter your server settings and connect. Open a known remote file. After editing, explicitly save it remotely or enable automatic upload.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [guide.md](guide.md).


[Console AI assistant: configuration, diagnostics and Diff/Merge review](console-ai.md).

## Connection feedback

Connect starts an OpenSSH client, then the client authenticates with the server. Starting a process is not reported as a completed connection. The selectable status is on its own wrapping row; startup errors, disconnects and exit codes also remain in the console transcript. Read the original client output above a nonzero exit status for the server/network reason.

Clients shipped in the application bin directory are resolved to absolute paths before spawning; PATH and, on Windows, system OpenSSH locations are fallbacks. If the password dialog helper is missing, a visible message explains that only key/agent authentication is available. Password/host-key questions use the separate SSH dialog. Spaces around the host and login are ignored.

![SSH connection flow](../media/ssh-connection.svg)

SSH now groups connection, file access and independent terminals in a scrollable right-side tab rail. The plus button creates another terminal (up to 16); each has its own process, input queue and transcript. An 80%-opaque Ai button can be dragged within the terminal and opens a separate assistant window. Shared model profiles and guarded Diff/Merge source proposals remain available. Explicit Run sends the reviewed suggestion only to that terminal.


### Transfers independent of terminals

On Transfer, enter absolute local/remote paths, File/Folder and packing mode. Send uploads; Receive downloads to a new local name and refuses existing destinations. Plain transfers use a separate SCP process. gz packs one file; tar.gz packs a file or folder. Packed downloads run gzip/tar remotely over a separate SSH process without a PTY. Archives remain archives and are never automatically extracted. Stop cancels packing/transfer while terminal connections remain active.

Local packing requires Python 3; remote packing requires gzip/tar on the server. SCP remote paths accept ASCII letters, digits and `/._-` for compatibility with legacy remote shells; use the existing SFTP opening commands for other names. Check upload destinations: a remote file may be replaced.

![SSH workspace](../media/ssh-workspace.svg)


## Unified terminal input/output

SSH now uses a single black VT100/xterm input/output screen with colors and native selection instead of a separate command entry. Right-hand icon tabs show a plug, file transfer and numbered terminals, with tooltip names. Local VTE also uses a black background and middle-click selection/clipboard fallback. See [TTY / COM](panel-tty.md) for the serial panel and current limits, including dynamic SSH/TTY dimensions and configurable 10,000-line history.

## One terminal group and a floating Ai window

The bottom **Terminals** tab contains **Local / SSH / TTY**. Context remains a separate tab. Every terminal screen has an **80%-opaque Ai** button: drag it to a free area; resizing keeps it inside the screen.

Ai opens a separate resizable system window with window controls, prompt, answers, model settings and attachments. In the regular style it is transient for the main window; independent-window mode removes that relationship. Pressing Ai again raises the existing window and refreshes context unless a request is active. Destroying the source terminal closes its assistant. This is a floating system window; no internal MDI container is introduced.

**Errors → source**, **Shell help**, **Explain output** and **Connection help** stage an editable prompt. Recent requests and the four most frequent requests are shared across terminal assistants: up to 128 unique requests held in memory until application exit, never written to disk. Selecting a request does not send it; press **Send** explicitly. Received-answer history remains separate, up to 32 answers per window.

SSH/TTY use selected text or the last 12,000 characters; Local uses selected text or its available transcript. The resulting context is capped at 60 KiB and can be inspected and edited. Attach source explicitly. Review code proposals in **Diff / Merge**; inserting a command does not execute it. For TTY, check the device protocol: shell commands may not apply to a COM device.
