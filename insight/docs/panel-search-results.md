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

Verilog/SystemVerilog: all-text, skip-comments, comments-only and code-only searches cover `.v`, `.sv`, `.vh`, `.svh` and `.inc`. Generated HDL profiles share these extensions for symbol queries. Literal include chains determine whether `.inc` uses C, C++ or SystemVerilog for indexing. A header shared by HDL and C/C++, an unresolved macro include or an orphan `.inc` retains its existing language; assign a language profile explicitly when needed. Custom profiles take priority. The slang index treats `.inc` as an include header, avoiding a second standalone compilation of included fragments. HDL semantic references require the slang backend and a valid include/project configuration.

Verilog/SystemVerilog: поиск всего текста, без комментариев, только комментариев и только кода поддерживает `.v`, `.sv`, `.vh`, `.svh`, `.inc`. Созданные HDL-профили совместимы при поиске символов. Язык `.inc` определяется по цепочке буквальных include: C, C++ либо SystemVerilog. Общий для HDL и C/C++ заголовок, макросное подключение или `.inc` без владельца сохраняет прежний язык; при необходимости назначьте профиль явно. Пользовательский профиль имеет приоритет. Для slang это подключаемый заголовок, а не повторно компилируемая единица. Семантические ссылки требуют slang и корректной настройки подключений проекта.

Include ownership is rebuilt in a worker before project indexing, using literal `#include` / backtick-include edges, relative paths, project-root paths and unique project basenames. Macro includes and ambiguous shared headers require an explicit language profile. The pass reads at most 64 MiB and accepts up to 65,536 include edges; it declines automatic classification when incomplete. Normal filename and all-text search still include all project `.inc` files.

Перед индексацией фоновая задача определяет владельцев `.inc` по буквальным `#include` / подключениям с обратной кавычкой: относительным путям, путям от корня и уникальным именам проекта. Для макросов и общих неоднозначных заголовков нужен явный профиль. При превышении 64 МиБ входных данных или 65 536 связей автоматическое определение отменяется целиком. Поиск имён и всего текста охватывает все `.inc` выбранного состава проекта.
