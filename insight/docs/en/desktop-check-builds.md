# Geany Insight check builds

## Native Windows x64 package

Extract the complete archive and run `Start-Insight.cmd`. It uses the bundled `bin/geany.exe`, GTK and dependencies and creates a separate `config`. MSYS2 is unnecessary for running it. Check `REVISION.txt` for the exact build commit.

The native package includes Context, panels, Lua, Diff / Merge and help in the editor. Scripts does not require a separate Insight Lua plugin. Earlier official-Geany-2.1/insight_lua.dll packages use a different plugin design; do not assume their instructions/features apply.

Offline help is in `data/insight/docs/en` and `ru`. Help and toolbar book buttons select the interface language. GIF uses bundled GdkPixbuf loaders; the launcher updates their cache for the extracted location. HTML5 video uses the system browser.

## Manual checks

1. Open Help → Geany Insight — Documentation… and Tutorials…; repeat with toolbar book buttons.
2. Search panels start hidden. Ctrl+F/Ctrl+H and toolbar buttons open file search/replacement.
3. Search / Replace → Search project… opens the query/results. Search folder… chooses a folder; crosses/Escape close panels.
4. Single click on a Context link changes preview only; double click opens the source line.
5. Double-click a line number to toggle a blue bookmark; the bookmarked-book button lists them.
6. Tools → Diff / Merge… works without KDiff3. Check Take Left/Right, manual edits, Undo/Redo and saving.
7. Scripts → Load extension… loads the [example](scripting.md); check its command and Undo.
8. Preview Markdown/RST images and GIF; Multimedia view opens browser video controls when codecs support the input.
9. Options → Language profiles… edits a profile. Editor right click assigns it; close/reopen a forced file and inspect fonts/colors.
10. Options → Interface language switches Russian/English/Automatic. Check captions, help and persistence after restarting.

## Build from source

Use C/C++, Meson/Ninja, pkg-config, GTK3/GLib development packages and gettext. Windows builds use MSYS2 UCRT64/GTK3.

```sh
meson setup build-editor --prefix="$PWD/install" --libdir=lib \
  -Dhtml-docs=disabled -Dpdf-docs=disabled -Dapi-docs=disabled \
  -Dgtkdoc=false -Dvte=false -Dmac-integration=disabled
meson compile -C build-editor
meson install -C build-editor
```

Insight Markdown/RST installs independently of html-docs, which controls standard Geany documentation. A portable Linux build can use `-DINSIGHT_RELOCATABLE` in c_args/cpp_args and the launcher's absolute `GEANY_INSIGHT_PREFIX`. If GTK is not bundled, install it on the destination system.

```sh
python3 insight/tests/run.py
python3 insight/tests/diff_test.py
python3 insight/tools/build_i18n.py
```

GUI tests need a real session or prepared virtual display; without `GEANY_RUN_GUI_TESTS=1` and DISPLAY/WAYLAND_DISPLAY they skip. Install editor/resources into the configured prefix first.

```sh
GEANY_RUN_GUI_TESTS=1 meson test -C build-editor \
  'Insight Help and on-demand Search GUI' \
  'Insight language profiles GUI' \
  'Insight Symbol Info and Window List GUI' \
  'Insight relation graph GUI'
```

Windows GUI validation needs a Windows session. Linux GUI success or a compiled Windows PE alone does not validate Windows GUI behavior. Results belong to a specific commit, not a previously downloaded archive.

The Windows workflow runs manually (`workflow_dispatch`) or on an explicitly requested change to `insight/build-requests/windows-x64-request.txt` in main. Ordinary commits do not trigger it. It builds the native editor, bundles GTK, tests diff/structural selection/templates/search/filtering/graph layout, runs `geany --version` without SDK PATH and includes REVISION.txt.

## Building without the charconv header

Some C++17 standard libraries report `scintilla/src/Selection.cxx: fatal error:
charconv: No such file or directory`. Selection restoration now uses a local,
range-checked decimal parser, so this code does not require `<charconv>`. There is
no replacement STL header to install or copy into the project. The editor still
requires C++17 for its other components.

After updating sources, rerun `make` in the existing build directory. If Autotools
inputs changed in a Git checkout, rerun `./autogen.sh` and your existing configure
command first. Do not replace system STL headers.
`SANITIZE=undefined python3 insight/tests/selection_parse_test.py` forbids including
`<charconv>` and verifies signed/unsigned limits, overflow, exact suffixes, 20,000
random conversions and selection restoration.

## Autotools link errors and GLib 2.56

Undefined references to `insight_file_patterns_*`, `insight_overview_*`,
`insight_remote_ui_*`, `insight_search_state_*` or `insight_keyword_collect`
were caused by missing runtime modules in `src/Makefile.am`. The source list now
includes them, the SSH dependencies and the installed `geany-insight-askpass`
helper. `python3 insight/tests/build_sources_test.py` checks Insight source parity
between Autotools and Meson so future omissions fail CI.

Wrappers preserve the declared GLib 2.56 baseline: wildcard matching uses the
older API when `g_pattern_spec_match_string` is unavailable; path normalization
uses a local GFile when `g_canonicalize_filename` is unavailable. This is lexical
normalization without reading files or resolving symbolic links. System libraries
need no replacement. CI generates an out-of-tree Autotools build, links and
installs the editor, checks unresolved symbols and runs `--version`. A separate
check builds the wrappers against actual GLib 2.56 headers and libraries.

After updating a Git checkout, regenerate Makefiles too. For an existing
in-source build:

```sh
git pull --ff-only
NOCONFIGURE=1 ./autogen.sh
./config.status --recheck
./config.status
make -j2
```

The recheck retains the previous configure arguments. With an out-of-tree build,
run autogen in the source directory and config.status/make in the existing build
directory.

### SSH queue cleanup and minimum-API enforcement

The `undefined reference to g_queue_clear_full` failure is fixed by clearing the
embedded queue with APIs available in GLib 2.56. Tests cover element destruction,
empty queues and reuse. The complete Autotools build now sets
`GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_56` and treats newer API usage as a compile
error. Compatibility branches respect that limit even with modern headers.
