# Project working preferences

- Commit authorized changes directly to `main`; do not use `master` or create a PR unless the user requests otherwise.
- Extend documentation together with user-visible features. Include useful explanatory screenshots, compact diagrams and graphical symbols, rather than text alone.
- Use real application/production-widget captures for screenshots; identify isolated GUI fixtures honestly. Prefer optimized JPEG for portable screenshots. Choose a smaller format only when the built-in viewer and target packaging support it. Keep text legible and strip metadata.
- Use SVG for diagrams and symbols (the requested vector format). Keep SVGs self-contained, without scripts, external fonts or network resources. Put shared images in `insight/docs/media`, use relative links from root/RU/EN guides, and install the assets with both supported build systems.
- Routine validation on a running application needs one desktop platform only. Use Linux by default, since its native GTK build and virtual display checks are available. Run Windows or Android application checks only when explicitly requested; do not repeat the same GUI checks across operating systems by default.
- Preserve Windows/Android support in implementation. An untested platform is not evidence of incompatibility or completed validation. Report exactly which platform was checked.
