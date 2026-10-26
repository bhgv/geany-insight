# Custom Commands

Open **Tools → Custom Commands…**. This modeless tool window adds a saved external-tool list, literal context substitutions, a launch preview, a working directory, numbered arguments and a selectable output log. It uses the shared compact top tabs and Collapse control. An unassigned **Custom Commands** Editor binding can be configured in Keybindings.

![Production window in an isolated Linux GUI fixture](../media/custom-commands.jpg)

![Command workflow](../media/custom-commands.svg)

1. Choose New command, enter a name, executable/arguments and working directory.
2. Save command stores it across editor restarts. Move up/down changes the saved order; Delete command removes the selected entry. Save edits before switching profiles or closing the window.
3. Fill `%1`–`%9` on Arguments when needed. These values are session-only and are not saved with a profile.
4. Preview command shows the expanded argument vector, directory and available context. Run samples the current editor/project context again; preview does not start a process.
5. Output merges stdout and stderr. Stop ends the directly launched process; Copy output and Save command output export the captured text. Clear output only clears the local display.

| Variable | Value |
| --- | --- |
| `%f`, `%n`, `%b`, `%e` | Full current filename, filename, stem, extension without dot. Require a named source file. |
| `%r`, `%d` | Filename relative to the project source root (absolute outside it), current file directory. Without a named source, `%d` is the home directory. |
| `%j`, `%J` | Project source root; project-data directory. Without a project, `%j` uses the named file's directory or home. `%J` requires a project. |
| `%p`, `%o` | Project path without extension; project name without path/extension. |
| `%l`, `%w` | One-based caret line; first whitespace-delimited word in a normal selection, or native word at the caret. Word capture is limited to 4096 bytes. |
| `%a`, `%t` | Current local date (`YYYY-MM-DD`) and time (`HH:MM:SS`). |
| `%1`–`%9` | Fields on the Arguments tab. Empty fields produce empty arguments. |
| `%%` | Literal percent sign. |
| `%PROJECT_SOURCE_DIR%`, `%PROJECT_DATA_DIR%`, `%PROJECT_NAME%` | Long project variables, case insensitive. |
| `%PROGRAM_DIR%`, `%TEMP_DIR%`, `%APPDATA_DIR%`, `%LOCAL_APPDATA_DIR%`, `%DESKTOP_DIR%`, `%MYDOCUMENTS_DIR%` | Predefined system folders, using the existing project-path resolver. Windows also supports `%PROGRAMFILES_DIR%`, `%PROGRAMFILESX86_DIR%`, `%WINDOWS_DIR%`. Unavailable folders decline the launch. |

## Arguments and examples

The template is parsed into an argument vector **before** substitutions. Values are substituted exactly once inside each argument. A filename containing spaces, quotes, dollar signs or semicolons remains one argument and cannot add another executable. Quote literal arguments that contain spaces. Ordinary shell operators are not interpreted; explicitly launch a shell if desired. For an explicit shell, pass substitutions as separate arguments and reference quoted positions such as `"$1"`, rather than inserting them into the script text. When writing shell scripts in the template, escape literal percent signs as `%%`, including printf formats. The preview quotes arguments for display; this display string is not executed and is truncated after 8192 bytes.

Examples:

```text
cc -fsyntax-only %f
python3 -m py_compile %f
git -C %j diff -- %r
python3 %1 %f
```

Command execution uses on-disk files. It does not save buffers, replace source text, clear Undo or silently apply output as code. External tools have their normal filesystem permissions and may modify files themselves. The working directory must exist and be absolute after expansion; blank uses `%j`. File timestamps, source parsing and project membership are unaffected by merely opening or previewing this window.

## Bounds and platform scope

Up to 64 named commands are stored in the editor profile's `insight/custom-commands.conf` by atomic replacement. Templates are at most 16384 bytes, names 128 bytes, settings 1 MiB; launch supports up to 128 arguments, each expanded argument 65536 bytes and all arguments together 256 KiB. Unknown variables, invalid UTF-8, line breaks and unavailable required context reject a launch.

Output is decoded as UTF-8 across pipe chunks; invalid bytes use replacement characters and NUL is displayed as `␀`. The process is stopped at 4 MiB, 50000 lines or 64 KiB on one line. This is an output log, not a terminal: no VT escape interpretation or interactive stdin. Stop/Close terminates the directly launched process and cancels the local output read; it does not promise to terminate independently forked descendants. No auto-run occurs when a project opens or settings are loaded.

Source Insight modifiers for all open/modified files, `%s` selection temporary files, ShellExecute and automatic per-command hotkeys are not implemented. Numbered arguments use explicit fields instead of modal argument prompts. Native GUI testing uses Linux; Windows shell syntax and runtime behavior require separate validation.

Reference: [Source Insight Custom Commands](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Custom_Commands1.htm).
