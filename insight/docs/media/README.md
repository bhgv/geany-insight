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

`console-ai.svg`: self-contained diagram of reviewed console context, explicit API requests and guarded code application.

`console-ai.jpg` and `ai-review.jpg`: actual assistant and Diff/Merge windows from the complete Linux editor under Xvfb (native integration run 39594612131). The source, diagnostic, key and reply are synthetic loopback mock API fixtures; these are not live provider sessions. JPEGs are optimized and stripped of metadata.

`editor-ai.svg`: self-contained diagram of cumulative requirements, editable Result, variants and explicit source refresh/application.

`editor-ai.jpg`: actual editor/project AI panel and Diff/Merge window in the complete Linux editor under Xvfb, native run 39602110109. Project source, prompts and replies are synthetic loopback mock API fixtures, not a live provider session. Optimized JPEG with metadata stripped.


`replace-files.svg`: self-contained diagram of scope capture, selected replacement plans and guarded buffer application.

`replace-files.jpg` and `replace-review.jpg`: actual Replace Files window and modal occurrence review in the complete Linux editor under Xvfb, native run 39616085928 (code 0f6d1549055ac635e85c969660baa0c12d2ce7a2). Source/project data are synthetic fixtures. The overview deselects an occurrence and skips comments; the review shows staged decisions. Optimized JPEGs, metadata stripped.


`replace-files-review.svg`: self-contained diagram of staged file/occurrence decisions, cancellation, source rechecks and remaining batches excluding applied files.


`renumber-ranges.svg`: self-contained diagram of source-order sequences across
column/multiple fragments, full preview, preflight and partial-operation Undo.

`renumber-ranges.jpg`: actual Renumber selected fragments window in the complete
Linux editor under Xvfb, successful native run 39625341427, source
f366a1dd576a56dcedbafccbdf7986c98b14f85b. Synthetic C source and column selections;
4/7/9 become 10/11/12 while unselected comment values stay unchanged. Optimized JPEG,
metadata stripped. It is a native test fixture running the complete application.

`buffer-transfer.svg`: self-contained source/destination splice and preview/Undo diagram. No external fonts, scripts or resources.

`buffer-transfer.jpg`: actual Copy Text to Buffer window in the complete Linux editor under Xvfb, native run 39638933057, source a848bc56efcac6b514e4dd82a6921e5bbed45090. Synthetic C source/target fixtures, whole-buffer replacement, numbered syntax-colored read-only panes. Optimized progressive JPEG, metadata stripped. This is a native fixture running the complete application.
