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
