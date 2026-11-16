# Search results

Matches in the project, a folder or open files.

## Controls

Open search through Search/Replace commands, toolbar buttons or shortcuts. Scope, file masks, case, word and regular-expression options define the search. Results include file, line, column and text. Close hides the panel.

## Short tutorial

Open project search, enter a name and run it. Select a match to preview it and double-click to open it. Close the panel when done.

The ? button at the upper right opens this page. Help follows the interface language selected in Options.

More: [tutorials.md](tutorials.md).


Полная справка: [Режимы поиска/замены и языки запросов](search-replace.md).

[Source Links](source-links.md)

Background search refreshes (Follow, source edits and index updates) keep the selected message tab and leave closed Results closed. An explicit search or navigation to results still shows the tab.


[Search and copy the list](list-tools.md): context menu and F4/Shift+F4 within already collected results.

Project text search and Find File include every language in the selected scope, regardless of the active editor language. File masks still apply. Lexical symbol usages retain their language compatibility rules.

Текстовый поиск по проекту и поиск имени файла включают все языки выбранной области, независимо от активного редактора. Маски файлов учитываются. Поиск использований символа сохраняет правила совместимости языков.

Verilog/SystemVerilog: all-text, skip-comments, comments-only and code-only searches cover `.v`, `.sv`, `.vh`, `.svh` and `.inc`. Generated HDL profiles share these extensions for symbol queries. In an HDL project, `.inc` uses SystemVerilog for indexing unless a custom language profile overrides it. The slang index treats `.inc` as an include header, avoiding a second standalone compilation of included fragments. HDL semantic references require the slang backend and a valid include/project configuration.

Verilog/SystemVerilog: поиск всего текста, без комментариев, только комментариев и только кода поддерживает `.v`, `.sv`, `.vh`, `.svh`, `.inc`. Созданные HDL-профили совместимы при поиске символов. В HDL-проекте `.inc` индексируется как SystemVerilog, если пользователь не назначил другой профиль. Для slang это подключаемый заголовок, а не повторно компилируемая единица. Семантические ссылки требуют slang и корректной настройки подключений проекта.
