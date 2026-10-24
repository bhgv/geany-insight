# TTY / COM serial terminal

TTY Terminal sits next to SSH Terminal in the bottom shelf. Its right-hand icon tabs contain Connection and numbered terminals. The plug icon opens connection parameters. There is no transfer tab. **+** adds an independent terminal, up to 16.

![Terminal transports](../media/terminals.svg)

The connection page refreshes the port list whenever opened, and Refresh ports repeats enumeration. Windows lists COM1–COM256 reported by the system; Unix lists conventional ttyS/ttyUSB/ttyACM/rfcomm and macOS tty./cu. device names in /dev. The editable list accepts an explicit device name (including a PTY for testing). Enumeration reads names without opening hardware. A refresh retains a manually entered device.

Choose baud rate (300–230400 from the supported list), 5/6/7/8 data bits, 1/2 stop bits, None/Even/Odd parity, and optional Local echo, then Connect. Defaults are 115200, 8N1, echo off. Settings apply to the selected terminal when connecting. The driver can reject unsupported combinations. Disconnect closes only that terminal. Hardware/software flow control is disabled; there is no modem-control, BREAK, file-transfer protocol or saved connection profile yet. Unix device permissions remain under the OS; the editor does not change them.

Input and output share one black terminal screen, with native text selection, Ctrl+Shift+C/V, Shift+Insert, a Copy/Paste context menu and middle-click paste. Middle click sends the terminal selection first, otherwise PRIMARY selection, falling back to the clipboard when PRIMARY is empty. Pasting can send newlines; remote programs decide whether these execute commands. Bracketed paste is used when requested by the remote application.

SSH and TTY share bundled MIT-licensed libvterm 0.3.3: streaming UTF-8, VT100/xterm cursor/erase/scroll controls, DEC line drawing, SGR 16/256/RGB colors, alternate screen, application cursor keys, control keys and terminal replies. The screen currently uses **80 columns × 24 rows**, matching the OpenSSH pipe client's initial PTY size; resizing the panel scrolls this screen rather than negotiating a new remote window size. Native local VTE keeps its own resize support. Scrollback is bounded to 2000 rows / 4 MiB of cell data; at most 4096 distinct color tags are retained. Terminal mouse-reporting protocols are not enabled: the mouse selects text. OSC clipboard requests are not connected to the system clipboard.

COM uses Windows overlapped I/O; Unix uses nonblocking termios and restores original settings on close. Input queues are bounded to 1 MiB. A read/write failure closes the connection and reports the problem. UTF-8 is the current terminal encoding; binary file transfer and legacy DOS code pages are outside this stage. Linux validation uses a real pseudo-terminal pair and loopback OpenSSH; physical RS-232 hardware and Windows runtime were not tested.

See [SSH](panel-ssh.md) and [local terminal](panel-terminal.md).
