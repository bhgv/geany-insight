Markdown and reStructuredText viewer
===================================

Choose Help → Markdown / reStructuredText viewer… and select a local UTF-8 .md, .markdown, .rst or .rest file. The viewer is separate from the editor.

To preview the current buffer, use Help → Preview current Markdown / RST… or the page button on the main toolbar. Saving is unnecessary: Reload document reads the same buffer's unsaved text. A document opened through the file picker reloads from disk. Closing the source buffer retains the last preview but makes reload report that the source is unavailable.

Relative images resolve against a named buffer's folder, or the working directory for an unnamed document. Preview does not change editor text, selection or caret position.

Compact rendering
-----------------

Supported features include Markdown and underlined RST headings, **bold**, *italic*, `code`, Markdown/simple RST links, Markdown tables and lists, fenced code, and code-block/image/figure/video directives. This is a portable initial subset, not complete CommonMark or Docutils. Unsupported constructs remain readable text.

UTF-8 documents are limited to 8 MiB and the first 10000 source lines. Tables allow 32 columns and 256 rows, with remaining content shown as text. Local PNG/JPEG and other installed GdkPixbuf formats load relative to the document; GTK plays animated GIF. Images are limited to 32 MiB and 16 million pixels. Static images wider than 800 pixels shrink; animation retains its original size.

Markdown example:

.. code-block:: markdown

   # Demonstration
   **Emphasis** and [another page](guide.md).
   ![Diagram](scheme.png)
   ![Animation](demo.gif)
   ![Video](demo.webm)

RST example:

.. code-block:: rst

   Demonstration
   =============
   .. image:: scheme.png
   .. image:: demo.gif
   .. video:: demo.webm

Multimedia view
---------------

The globe or video button opens generated HTML in the system browser, including images and HTML5 video controls. Playback depends on browser codecs. WebM/MP4/OGV/MOV are recognized as video links; not every browser plays every container.

In the GTK window, video is a button opening the multimedia view. There is no embedded video decoder. The browser page remains available after closing help until its temporary file is removed. Generate a fresh Multimedia view after editing.

The compact viewer does not fetch network images. HTTP/HTTPS links open when clicked; a multimedia browser page may load explicitly specified network images/video. Raw HTML is displayed as text: scripts from Markdown/RST are not executed.

Navigation and search
---------------------

.md/.rst/.rest links open in the same window with Back/Forward history. Other links use a system application. The top contents selector lists the first 512 headings. Same-page links such as #heading jump to that heading. Anchors use lowercase letters/digits, underscores/hyphens and spaces replaced by hyphens; duplicate headings append -1, -2 and so on.

Ctrl+F opens literal, case-insensitive page search including Cyrillic. Regular-expression characters are ordinary text. Enter/F3 selects the next match; Shift+F3 selects the previous match, wrapping at either end. The counter shows the selected and total matches, up to 2048. Escape or the cross closes search. Help search does not alter the editor's search query.
