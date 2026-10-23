# Search Web and Search Engines

Use **Search → Search Web…**, the editor popup or a configurable Search shortcut. The query starts with the ordinary selection, or the word under the caret. Multiline, rectangular, multiple and virtual selections are not copied into the query. You can type a query even with no source document open.

![Web search workflow](../media/web-search.svg)

Choose a search engine and inspect the read-only, selectable URL preview. **Copy URL** changes only the clipboard. **Search** opens the configured/default external browser through Geany. Neither action closes the dialog. Editing the query or selecting an engine does not open a browser. Search terms are not saved in preferences and the feature does not upload files or scan the project.

**Search Engines…** opens a reusable editor with Name and URL fields, Add, Update selected, Remove selected and Move up/down. Up to 32 engines are supported; at least one remains. DuckDuckGo, Google and Bing are initial examples. The selected engine and list survive restart in `insight/web-search.conf`; text entered in the query is session-only. Closing and reopening the query dialog loads the current source selection again.

Templates must be HTTP(S) URLs up to 4096 UTF-8 bytes, with exactly one **%S** after the host. Credentials in the authority, raw spaces/control characters, invalid percent escapes and malformed ports are rejected. Existing URL escapes such as `%20` are retained. The query is encoded as one URL component: Unicode, spaces, `&`, `+`, `#`, quotes and slashes cannot introduce another query parameter or command. Queries are single-line UTF-8 text up to 4096 bytes. Engine names are limited to 128 bytes. Custom URLs may put `%S` in a path or query.

Source text, source selection, Undo and disk files are preserved. The external browser performs the actual request only after **Search**. Windows uses the default browser; other desktop platforms use Geany's browser setting or system default. This is external web lookup, without embedded browsing or downloading results into the editor.

References: [Source Insight Search Web](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Search_Web.htm), [Search Engines](https://www.sourceinsight.com/doc/v4/userguide/Manual/Command_Reference/Search_Engines.htm).
