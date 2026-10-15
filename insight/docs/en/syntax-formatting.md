# Syntax Formatting

**View → Mono Font View** temporarily displays source text in one configurable
font and size, keeping syntax colors. It suppresses per-token bold, italic,
underline and case presentation. The preset is Monospace 12; choose its font in
**Options → Syntax Formatting…**. This temporary switch resets on restart.

The Options dialog saves these independent settings:

- **Use syntax formatting**: off displays the default text font and colors.
- **Use only color formatting**: retains syntax colors and uses the default
  source font, size and emphasis for all tokens.
- **Use no color formatting (monochrome)**: removes syntax color differences and
  uses grayscale default foreground/background; font variations still apply.

Mono Font View overrides all three while active. Turning it off restores the
saved combination. Apply saves changes; Cancel discards them. The mono font must
have a point size from 4 to 96.

These are display modes. They preserve text, caret, selection and Undo. Source
editors and colored previews participate; line-number/brace UI styles and search
or navigation indicators remain separate. New buffers and theme/profile/font
reloads use the active mode. Disabling a mode restores the underlying styles;
profile CSS and theme files are not rewritten. This does not implement the full
Source Insight parent-style hierarchy or import its proprietary style files.

## Source Insight visual themes

Options → Visual theme… selects one of 15 sets from the supplied theme export.
Each has editable `styles/<name>/editor.css` and `panels.css`. Apply remembers the
selection; Cancel retains the current view. Current Geany theme restores profile
styles. Themes affect existing/new buffers, Context and panel lists. Explicit
panel Font & Color overrides take precedence; Mono Font View and Syntax Formatting
still apply above the theme. User themes in the configuration directory's styles
folder are discovered at startup.

The fonts folder bundles OFL-licensed Noto Sans and Noto Sans Mono, verified for
Russian including Ё/ё and №. Installed Windows fonts and Verdana are supported
without redistributing their files. ParentStyle inheritance is expanded and all
semantic styles are retained in CSS; only generic lexer categories currently
render. This is not a complete semantic Source Insight style engine. See the
source-tree styles/README.md and fonts/README.md for details and limits.
