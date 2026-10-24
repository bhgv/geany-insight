## Shared comparison window layout / Единое оформление сравнений

Comparison/Merge, Directory Compare, snapshot comparisons, Git History and editor
AI review now share the same toolbar component: compact 2/3-size controls, scrollable
tabs at the top, and control pages sized to the active group. The default window size is 1100×700;
explicitly saved Directory Compare and Diff/Merge sizes remain respected. Comparison code uses a shared 10 pt starting baseline, independent of toolbar
scaling and editor font preferences. Excess controls scroll inside their page
instead of forcing the whole window wider or taller.

Directory tabs: Files and comparison; Comparison options; View (filters, masks,
columns); File operations; Navigation. Snapshot comparison groups comparison options,
report actions and navigation. Git groups revision controls and difference navigation.
Editor AI adds request, files, connection and request-history tabs, with the same
compact controls; it no longer forces a separate 1200×950 window. Read-only modes,
confirmation dialogs, cancellation and operation guards are preserved. Ctrl+F in
Directory Compare reveals the filter tab before focusing its input.

«Сравнение/сведение», сравнение каталогов и снимков, история Git и ИИ-проверка
используют общий компонент: верхние вкладки, уменьшенные в 1,5 раза элементы,
высоту активной группы по её содержимому и начальный размер окна 1100×700. Явно сохранённые размеры
пользователя сохраняются. Начальная база кода в сравнении — 10 pt; она не зависит от масштаба параметров и шрифта основного редактора.
Длинные панели прокручиваются внутри вкладки, сохраняя место для содержимого.
У каталогов отдельные группы файлов, сравнения, вида/фильтров, файловых операций
и навигации; у ИИ — запроса, файлов, подключения и истории запросов.

![Shared comparison chrome](../media/compare-windows.svg)

## Binary comparison, selective merge and hex editor

The Files and comparison tab in Diff/Merge opens Binary comparison / Hex editor
with the selected files, retaining the text window. Tools → Hex editor opens it
independently. Regular files up to 16 MiB are loaded without encoding conversion,
including NUL. A blank path means an empty stream; Result initially contains Left.

Sources are side by side with synchronized scrolling; Result is below them.
Hex addresses and printable ASCII accompany byte, 16/32/64-bit word views with
little/big endian. Display order never changes saved bytes. Pages contain 4096 bytes
(256 rows), numbered from zero. Different rows are highlighted. Next difference
selects a contiguous unequal byte range at matching offsets; shifted blocks are
not automatically aligned.

Click a hex word or ASCII byte to set its range; dragging selects whole rows.
Fine-tune exact byte boundaries using the offset/count fields.

The Merge tab has four decimal fields: source offset/count and Result offset/count
to replace. Take Left/Right range inserts source bytes into the specified Result
range. A zero replacement count inserts; missing bytes past the shorter source's
EOF remove the corresponding Result range. Check ranges after insertions/deletions
because offsets change. Apply hex bytes accepts digit pairs with optional whitespace;
empty input deletes. Undo/Redo retains up to 64 transitions within a 32 MiB snapshot
budget. Save writes raw bytes, without formatting or addresses; closing a changed
result requires confirmation.

Text Diff/Merge also offers Insert Left/Right selection: select any source fragment,
then a Result selection or caret. The last source selection is retained while you
choose the Result range, and cleared when source text changes. One Undo reverts the operation. This also works
with AI proposals: take just the useful fragment, edit Result and apply through
the existing guarded review action.

Base64 exports **Result**, including manual edits. Choose the whole stream, encoded
parts of 10/100 KiB or 1/10 MiB, or a custom encoded byte count (zero means whole).
Each export is limited to 4096 parts. Encoding happens once before splitting. Parts are named `name.b64.part000001`,
`name.b64.part000002`, etc. Existing parts are protected; failed exports remove only
parts created by that operation. Reassemble before decoding:

```sh
cat name.b64.part* > complete.b64
base64 --decode complete.b64 > restored.bin
```

Clipboard export copies the whole stream or one zero-based part. Individual parts
may not be independently decodable; concatenate them first.

![Binary merge and export](../media/binary-merge.svg)

### Toolbar height and collapse

The toolbar fits the **active** page's controls, including font changes, added
rows and horizontal scrollbars. Short pages do not reserve the tallest page's
height. The old fixed 86 px viewport caused lower controls to be clipped. The
comparison panes below now use the remaining space, retaining their proportional
split.

The Collapse tab leaves only the tab strip. Select any normal tab to restore that
group at its required height. Files, selections, unfinished Result and Undo/Redo
remain intact. If a group cannot fit a small window, internal scrolling remains
available so its final control is still reachable while retaining comparison space.

### File identity, difference table and merge destination

Each pane keeps a selectable source path visible above its content, including in
the binary editor. Long paths ellipsize in the middle and remain available in
full through tooltips. Editing source chooser fields does not relabel already
loaded snapshots until a comparison succeeds.

The difference table has Left file, Right file and Merge status columns. Headers
identify source paths; cells show logical line ranges. Status is Not merged,
Merged — Left, Merged — Right or Edited manually. The initial copy of Left is not
an accepted resolution; explicitly taking an unchanged Left block records that
choice. Manual edits mark affected blocks. Undo/Redo restore both text and status;
saving alone does not mark unresolved blocks as merged.

The Result header offers Left, Right or a separate output file, with a path entry
and file chooser. Selecting a destination never writes data. Save opens its chooser
with that destination and overwrite confirmation. Binary output has the same
choice. Text comparison help buttons overlay headers/table at 80% opacity rather
than reserving separate empty rows.

## Streaming large files and edit history

Files larger than 16 MiB in Hex, or 1 MiB in text Diff/Merge, open the streaming
comparison window. There is no 100 MiB ceiling. Sources are read in 64 KiB blocks;
only a 4096-byte page enters each GTK text buffer. Result is a piece table of
source ID, offset and length, with separate manual bytes. Each metadata list and
manual payload spills to private temporary storage after 2 MiB. Undo/Redo retains
piece-table versions rather than file copies. Temporary storage is removed on close;
this is not a persistent recovery journal. Source changes invalidate reads and saving.
Saving materializes the current composition into a temporary file before replacing
the selected destination. Allow disk space for Result and the destination staging file.
Base64 export is streamed through temporary storage; split parts concatenate before
decoding. Clipboard parts are limited to 16 MiB.

Text comparison uses bounded line-start anchors to resynchronise insertions and
deletions. It is a heuristic, not a globally minimal edit script; anchors beyond
64 KiB may produce broader ranges. Byte offsets remain authoritative, and original
bytes/line endings are preserved. Page boundaries can display an incomplete UTF-8
character as a replacement glyph; this never changes saved bytes. Manual replacement
text is UTF-8. Large-file comparison does not apply the small-window ignore-space,
regex, or whole-buffer editing commands.

Curved-arrow Undo/Redo buttons and a History dropdown stay beside Result when the
upper tabs collapse. Choose an event directly, keeping later events available for
Redo. Hover an event for Before/After: grey before, red after. The same controls
serve text, AI proposals, binary and streaming Result. Git and snapshot windows
remain read only; their labels and difference table headers identify the sources,
and help overlays do not allocate a separate blank row.
