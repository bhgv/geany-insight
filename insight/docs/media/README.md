# Documentation media

- `git-commit-dialog.jpg`: real production GTK commit dialog captured by the isolated Git GUI fixture; English sample, synthetic file names. Source behavior: `f4b14a0`.
- `folder-tree.jpg`: real production lazy GTK folder tree captured by its isolated GUI fixture; temporary test path and Unicode sample. Source behavior: `17b8041`.
- Screenshots are RGB, optimized progressive JPEG at quality 82, without EXIF. The commit capture is reduced to the built-in viewer's 800-pixel width; the tree capture keeps its native size.
- SVGs are explanatory diagrams or graphical symbols, not screenshots. They contain no scripts, external fonts or linked assets. Captions in the RU/EN guides explain the shared symbols.

Keep screenshots current when visible behavior changes. Favor small, useful illustrations over entire desktop captures. Check legibility and rendering in the built-in local help viewer on the chosen validation platform.

- `filetype-editing.jpg`: real production file type editing page from its isolated GTK fixture. `filetype-editing.svg` explains mixed indentation and screen versus typing wraps.

`filetype-typing.svg` explains typing permissions and inheritance. The updated `filetype-editing.jpg` captures the real extended GTK page in an isolated fixture.

`typing-pairs.svg` shows generated-pair skip, empty-pair deletion and wrapping a matched block.

`completion-list.jpg`: real production completion GtkTreeView captured in an isolated fixture with sample symbol metadata; not a full application screenshot.

`completion-options.jpg` captures the production preferences page in an isolated GTK fixture. `completion-insertion.svg` shows optional parentheses and existing-argument preservation.

`completion-case.svg` illustrates optional casefold lookup and preferred exact-case selection. `completion-options.jpg` now includes the case-sensitive option.

`intelligent-paste.svg` shows whole-line placement, shared indentation and destination indentation. The updated `filetype-editing.jpg` includes Allow intelligent paste; it is a capture of the production GTK page in an isolated fixture.

`line-operations.svg` is a compact diagram of the line commands and optional destination indentation; it is not an application screenshot.

`line-clipboard.svg` explains caret-based fragments, repeated whole-line copying and Cut Selection or Paste. It is a vector diagram.

`line-operations.jpg`: real Line operations menu in the complete running Linux editor during its native GUI test, including Copy/Cut and Paste commands. JPEG capture is also retained as a CI artifact.

`selection-navigation.svg` explains anchor-based Extend Mode versus ordinary movement. Native menu captures are produced by isolated-in-time full editor GUI tests.

`selection-navigation.jpg`: real Selection and navigation menu in the complete running Linux editor during its native GUI test. Shared-display captures run exclusively and reject occluded black output.

`paste-position.svg` explains the three positions after intelligent whole-line insertion. It is a self-contained vector diagram, not an application screenshot.

`file-compare.jpg`: real production Diff/Merge window captured in an isolated Linux GTK/Xvfb fixture with synthetic test files; not the complete editor. `file-compare-flow.svg` explains block selection and exact-byte result editing.

`directory-compare.jpg`: real production Directory Compare window from the isolated Linux GTK fixture with synthetic files, not the complete editor. `directory-compare.svg` illustrates relative matching and pair routing.
