# Paragraph and lines

**Edit → Paragraph and lines** adds Source Insight style text commands alongside Geany's existing block reflow/join commands.

![Production paragraph preview dialog in an isolated Linux GUI fixture](../media/paragraph-tools.jpg)

![Scope and transformation rules](../media/paragraph-tools.svg)

- **Reform Paragraph** expands the caret to its enclosing paragraph, or the selection to complete intersected paragraphs. Blank lines (ASCII spaces/tabs only) separate paragraphs and remain byte-for-byte intact. The first line's exact spaces/tabs are repeated as indentation on wrapped lines. Within paragraphs, runs of spaces/tabs and line boundaries become word separators. Long words are kept whole, even beyond the margin.
- The margin comes from the enabled **File Type Options** profile; otherwise from the editor's line-breaking or long-line column, even if its visual guide is hidden. Display columns account for tab stops, wide Unicode characters and combining marks; this is a practical column estimate, not full grapheme or bidirectional layout.
- **Join Lines** joins the current and next physical line when there is only a caret. With a selection it joins all intersected lines; a selection ending exactly at the next line's start excludes that line. It removes boundary indentation/trailing whitespace and inserts one separating space. Interior spaces/tabs, first-line indentation and the final line's trailing whitespace/EOL are preserved. Blank lines within the joined range are collapsed. A final newline without another physical line is retained.
- **Paragraph Preview…** offers Reform/Join, a temporary margin override and explicit Preview/Apply. The initial margin uses the same file type/editor settings as the direct command. Changing options clears the preview. Preview shows at most 8000 bytes of the transformed range; Apply recomputes the whole range and does not change file type settings.

Both operations use one Undo action and select the transformed range, retaining reverse selection direction. No-op commands do not create Undo entries. Existing line endings are retained between paragraphs and after the final line; new wraps use the paragraph's first EOL, or the editor EOL for a single unterminated line.

Single ordinary selections only: read-only, column, multiple and virtual-space selections are excluded. Input/output are limited to 8 MiB and input to 500000 physical lines (plus an empty EOF position). A text/selection change, tab switch or closed target while the preview is open cancels Apply. These are plain-text operations: code, strings, comments and comment prefixes receive no language-aware handling.

Reference: [Reform Paragraph](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Reform_Paragraph.htm), [Join Lines](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Join_Lines.htm).
