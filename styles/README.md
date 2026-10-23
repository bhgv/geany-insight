# Visual themes

Options → Visual theme… selects a theme or restores Current Geany theme.
The selection is saved in `insight/visual-theme.conf` in the editor's configuration
folder. Apply validates both files and saves the selection before changing the
screen. Cancel and write/parse failures retain the previous theme.

Each theme has its own named directory containing:

- `editor.css`: Scintilla font/color overlays and retained Source Insight styles.
- `panels.css`: ordinary GTK3 CSS for panel lists and the main window frame.

The distributed styles and fonts are installed under the Geany data directory.
A `styles/<name>` directory in the user's configuration folder overrides a bundled
name. New folders are discovered at startup; choosing an existing theme again
reloads its CSS. Each CSS file is limited to 512 KiB. Imports and URLs are rejected.
No network access or original XML file is required at runtime.

## Editor CSS

The editor uses a validated CSS subset mapped to Scintilla; it is not a web view.
Supported selectors are `editor`, `.keyword`, `.comment`, `.string`, `.number`,
`.operator`, `.type`, `.preprocessor` and `.line-number`. Properties are `color`,
`background-color`, `font-family` (ordered fallback list), `font-size` (4–96pt or
25–400% of the base editor size), `font-weight` (normal/bold), `font-style`
(normal/italic), and `text-decoration-line` (none/underline).

Themes overlay language-profile display styles without changing parser/keywords,
source text, selection or Undo history. Current Geany theme restores those styles.
Mono Font View and the existing Syntax Formatting switches apply on top.
Explicit panel Font & Color overrides have higher priority than the visual theme;
GTK theme selection colors and search/navigation decorations remain independent.

## Source Insight export

`source/default.themes.xml` is the user-supplied Source Insight 4.00.0078 export.
Run `python3 insight/tools/import_source_insight_themes.py` at the repository root
to regenerate all 15 directories. Do not run it over hand-edited CSS unless you
want to replace those edits. An alternate input/output can be passed as arguments.

ParentStyle chains are expanded into CSS. Every exported semantic style is also
retained as a `.si-<name>` rule, with original attributes in a comment. These rules
preserve distinctions for future semantic rendering; the current renderer applies
the generic lexer categories above. It does not yet classify every declaration or
reference like Source Insight. Dotted underlines become solid; tracking, shadow,
fixed-whitespace and keep-with-next attributes are retained but not applied.

The export has no base editor point size, so converted themes use an editable
12pt baseline. One entry, Styles for use with dark backgrounds, contains styles
only and therefore retains the current editor/panel background colors.

Panel fonts retain their exported sizes. Installed original font families are
preferred; UI stacks also allow installed Verdana before the bundled Noto Sans.
Code falls back to bundled Noto Sans Mono. See `../fonts/README.md` for licenses,
Cyrillic coverage and private font loading.

## Interface presets

Options / View → Interface style switches Classic and a macOS-inspired GTK shell independently of these source palettes. See [interface guide](../insight/docs/interface-style.md). `appearance-macos.css` is a standalone scoped UI stylesheet, not a source-theme directory. RGBA blending is within the window; no system blur is claimed.
