# Project working preferences

- Commit authorized changes directly to `main`; do not use `master` or create a PR unless the user requests otherwise.
- Extend documentation together with user-visible features. Include useful explanatory screenshots, compact diagrams and graphical symbols, rather than text alone.
- Use real application/production-widget captures for screenshots; identify isolated GUI fixtures honestly. Prefer optimized JPEG for portable screenshots. Choose a smaller format only when the built-in viewer and target packaging support it. Keep text legible and strip metadata.
- Use SVG for diagrams and symbols (the requested vector format). Keep SVGs self-contained, without scripts, external fonts or network resources. Put shared images in `insight/docs/media`, use relative links from root/RU/EN guides, and install the assets with both supported build systems.
- Routine validation on a running application needs one desktop platform only. Use Linux by default, since its native GTK build and virtual display checks are available. Run Windows or Android application checks only when explicitly requested; do not repeat the same GUI checks across operating systems by default.
- Preserve Windows/Android support in implementation. An untested platform is not evidence of incompatibility or completed validation. Report exactly which platform was checked.

## Mandatory code readability and comments

- Keep hand-maintained source files at 500 lines or fewer. Split oversized files by responsibility when reorganizing them, and put related modules in directories named for their purpose. Vendored and generated files follow their upstream or generator layout.

Apply these rules to new or changed source files, scripts, and examples.

- Format code for human reading. Follow the surrounding project's indentation and brace conventions. Use descriptive names, separate logical blocks with blank lines, and break up long expressions and declarations. Keep one statement per line. Avoid minified code and dense one-line functions.
- At the top of each source file, after any required shebang or license notice, add a short comment in English and Russian explaining why the file exists and what it is responsible for.
- Immediately before every function definition you add or change, include a comment in English and Russian. Explain what the function does and what each parameter means. Document the return value, side effects, and error behavior when applicable. Use the actual parameter names and keep both languages in sync with the implementation.
- Before submitting a change, review the diff for readable formatting and check that the file and function comments meet these requirements.
