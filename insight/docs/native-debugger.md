# C/C++ debugging with GDB

Build the program with debug symbols. For example, `cc -g -Og main.c -o app`
or `c++ -g -Og main.cpp -o app`. On Windows, select the resulting `.exe`.

In Geany Insight, open **Tools → Debug C/C++ Executable…** and select the
compiled program. Geany launches GDB in a console with the directory of
the executable as the working directory. The program starts when you
enter a command such as `start` or `run`.

![Geany opens a GDB console for the selected executable](media/native-debugger.svg)

| Command | Action |
| --- | --- |
| `set args one two` | Set program arguments before running. |
| `start` | Run and stop at `main`. |
| `break file.cpp:42` | Set a source breakpoint. |
| `continue` | Resume until the next stop. |
| `next` or `step` | Step over or into a call. |
| `backtrace`, `info locals`, `print value` | Inspect the stack and values. |
| `quit` | Close the debugging session. |

Install GDB and make it available in `PATH` before starting Geany.
The Unix launcher uses `xterm`. On Windows, GDB opens its own console.
Geany passes the executable path as a single argument. GDB commands,
breakpoints and value inspection take place in that console.
