# Terminal

An embedded terminal, when available in this build.

## Controls

The terminal runs local shell commands. Availability depends on platform and VTE support. Use the separate SSH panel for remote commands.

## Short tutorial

Select Terminal and run a command showing the current directory. For SSH, open its dedicated panel.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [panel-ssh.md](panel-ssh.md).


[Console AI assistant: configuration, diagnostics and Diff/Merge review](console-ai.md).


## Unified terminal input/output

SSH now uses a single black VT100/xterm input/output screen with colors and native selection instead of a separate command entry. Right-hand icon tabs show a plug, file transfer and numbered terminals, with tooltip names. Local VTE also uses a black background and middle-click selection/clipboard fallback. See [TTY / COM](panel-tty.md) for the serial panel and current limits, including dynamic SSH/TTY dimensions and configurable 10,000-line history.

## One terminal group and a floating Ai window

The bottom **Terminals** tab contains **Local / SSH / TTY**. Context remains a separate tab. Every terminal screen has an **80%-opaque Ai** button: drag it to a free area; resizing keeps it inside the screen.

Ai opens a separate resizable system window with window controls, prompt, answers, model settings and attachments. In the regular style it is transient for the main window; independent-window mode removes that relationship. Pressing Ai again raises the existing window and refreshes context unless a request is active. Destroying the source terminal closes its assistant. This is a floating system window; no internal MDI container is introduced.

**Errors → source**, **Shell help**, **Explain output** and **Connection help** stage an editable prompt. History and the four frequent requests are separated by usage context: Local terminal, SSH, TTY, manual console, diagnostics, program help, source help, and Editor/Project. Each context retains up to **100 unique requests**; repeating a request updates its frequency and recency, and adding the 101st discards the oldest. Selecting does not send: edit the text, then press **Send**. **Delete selected** removes one entry; **Clear history** clears this context only and leaves the current prompt editable. Answer history is separate: up to 32 replies held in the window's memory.

Requests are saved immediately before dispatch in **`<project-file>.ai-history/`**, beside the project file, with a separate `.conf` per context. The directory can be tracked with the project in Git. Without a project, storage uses `insight/ai-history/` under the configuration directory. Restarting reloads the history. An open window stays bound to the project it was created for; reopening terminal Ai after switching projects creates a window with the new project's history. The history controls' tooltip shows the file path. Only request text, frequency and timestamp are stored, excluding API keys, replies, terminal output and attached source. A save failure displays an error and prevents dispatch.

SSH/TTY use selected text or the last 12,000 characters; Local uses selected text or its available transcript. The resulting context is capped at 60 KiB and can be inspected and edited. Attach source explicitly. Review code proposals in **Diff / Merge**; inserting a command does not execute it. For TTY, check the device protocol: shell commands may not apply to a COM device.

## Compact terminal height and status

Local, SSH and TTY use the full common page height. SSH/TTY panel help and SSH diagnostic navigation live in the right icon rail. Their status appears on the Local / SSH / TTY tab row, aligned right in the unused area. Tabs always keep priority: long text is clipped at the right edge, the full message is available in a tooltip, and no status row is reserved below the terminal.
