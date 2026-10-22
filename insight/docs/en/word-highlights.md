# Word highlights

Use **Search → Word highlights → Highlight Word** with the caret inside a word, or with one word selected. Repeat on the same word to remove it. Marks stay visible when the selection moves and appear in every open source buffer, including read-only buffers and files opened later.

![Word highlight workflow](../media/word-highlights.svg)

**Highlighted Words** manages up to 16 words for the current session. Remove selected words, clear all, choose a color, or change case sensitivity. Color and case sensitivity survive restart; the word list does not. The editor popup also offers toggle, management and clear commands. Assign shortcuts in Preferences → Keybindings; these commands also appear in the command browser.

**Next Highlighted Word** and **Previous Highlighted Word** select matches in the current file, wrap at the ends and use navigation history. Matching is literal, Unicode-aware and restricted to whole words; it includes comments and strings, rather than resolving symbols. A selected word must contain letters, numbers, underscores or combining marks and fit within 256 UTF-8 bytes.

Edits, Undo and Redo refresh marks in the background. Moving the caret does not restart scanning. Each buffer is limited to 2 MiB and 20,000 matches, with bounded painting batches; results for changed or closed documents are discarded. Source text, undo history and files on disk are untouched by highlighting.

Reference: [Source Insight Highlight Word](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Highlight_Word.htm). See also [selection search](selection-search.md).
