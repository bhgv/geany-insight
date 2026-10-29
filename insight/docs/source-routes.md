# Source routes and control flow / Маршруты и поток управления

**Relations → View → Source routes / flow** opens a separate graph presentation
inside the existing Relations panel. The old symbol/call graphs remain available
through **Relations**, Graph view and the Navigate menu. A source route is a
user-defined reading/navigation link, not proof that one statement executes another.

![Two kinds of source graph](media/source-routes.svg)

## Arbitrary source positions

1. Put the caret at a useful position in a saved source file.
2. Choose **Navigate → Add caret to source routes**, the same editor context-menu
   command, or **Source routes → Add caret location**.
3. Repeat in another file or another place in the same file.
4. Click the origin node, press **Link from selected**, then click the destination.
   Escape cancels a pending link. Reverse arrows can be added separately.

A click opens the source immediately; Up/Down select nodes and Enter opens one.
Repeated positions and identical arrows are ignored. **Remove node** removes that
node and its incident arrows. There are at most 256 manual nodes and 1024 arrows.
Zoom is 40–200%, also Ctrl+wheel. Full paths/line text appear in tooltips. The
shared compact options notebook has a Collapse tab.

Named-file locations, full-line anchors and arrows are stored in the native
project file, in `insight-source-routes`; project writes use the existing save
mechanism. Paths inside the project are relative. A new editor process restores
the graph; without a project, routes exist for the current session only. Closed
files reopen on click. A changed line is relocated only when its complete saved
line text has one exact match in a buffer up to 2 MiB. Missing/ambiguous anchors
are refused rather than silently jumping elsewhere. Positions are byte columns;
no source bytes, selections, Undo history or source disk files are changed by
adding/linking nodes. Links are retained until explicitly removed, including if
a source file becomes unavailable.

## Function and selection flow

**Control flow → Function flow** builds a snapshot of the brace-bodied function
containing the caret, using current parser tags and unsaved editor text.
**Selection flow** builds a graph from one ordinary selected range of complete
statements. Entry/Exit, operators, if/else (including else-if), while/for,
return, break and continue have directed arrows; conditions have Yes/No labels.
Loop-back edges and early exits are explicit. Function-call expressions remain
inside their statement nodes; use the existing lexical Calls/Call sites relation
graph for inter-function browsing. An arbitrary pair of locations can always be
connected manually in Source routes.

This is a bounded structural graph for C/C++/C#/Java and semicolon-terminated
JavaScript/TypeScript, not a compiler CFG or semantic call resolver. Comments and
strings are masked first. Conditions/loop headers are atomic; short-circuit
operators and for-loop header steps are not separate nodes. Constant conditions,
macro expansion, overloads, exception edges and runtime paths are not inferred.
Switch, do/while, goto, exception constructs, labels, ternaries, preprocessors
inside the body, nested definitions and brace initializers are refused with an
explanation. Unreachable statements may remain as disconnected syntactic nodes.
Other languages remain supported by the existing symbol/call relation tools;
manual routes are language-independent.

Bounds: a complete UTF-8 buffer without NUL up to 2 MiB, 512 nodes, 1024 edges,
64 statement/128 expression nesting levels and 4096 bytes per captured line.
Selection requires one stream range without virtual space. A generated graph
keeps its exact source digest; editing the buffer makes click navigation refuse
the old snapshot until it is rebuilt. Generated snapshots are not project data.
Changing graph presentation never launches a project scan or index rebuild.

## По-русски

В панели **Связи → Вид → Маршруты / поток управления** появились два режима.
В ручном режиме добавляйте позицию курсора из меню **Навигация**, контекстного
меню редактора или кнопкой **Добавить позицию курсора**. Выберите начальный узел,
нажмите **Связать от выбранного**, щёлкните конечный. Узлы могут находиться
в любых двух файлах или двух местах одного файла. Щелчок открывает исходник;
стрелки клавиатуры выбирают узлы, Enter открывает выбранный. Маршруты и стрелки
сохраняются в проекте, закрытые исходники переоткрываются. Повторные узлы и стрелки
не создаются. Изменившаяся строка ищется только по уникальному полному совпадению;
при неоднозначности нужно добавить позицию заново. Это ручные навигационные связи,
а не автоматически доказанные пути исполнения.

**Граф функции** строит ветвления текущей функции; **Граф выделения** — полных
выделенных операторов. Поддержаны if/else, while/for, return, break/continue.
Есть вход/выход, стрелки Да/Нет и обратные стрелки циклов. Это структурный
С-подобный анализ с перечисленными выше ограничениями, без вычисления макросов,
типов и условий. Неподдерживаемые конструкции явно отклоняются. После изменения
исходника граф нужно перестроить. Графы вызовов между функциями уже доступны
в прежних режимах **Вызовы / Места вызовов** панели «Связи».
