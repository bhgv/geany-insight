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
