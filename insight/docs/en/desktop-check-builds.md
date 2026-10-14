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
