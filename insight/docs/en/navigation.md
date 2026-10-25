# Navigation

Navigate combines source jumps with Relation Window operations. A command may move the caret, present a choice of matching definitions, change an existing graph's view, or toggle whether a preview follows the editor.

| Command | Result |
| --- | --- |
| Back / forward | Editor position history, shared with the main toolbar and editor popup |
| Context back / forward | Independent preview history; does not switch the editor source |
| Definition / prototype / implementation / base type | Jump to a known target or choose among matching results |
| Jump To Caller / References / Members / Derived types | Reveal Relations, prefill the symbol under the caret and run the selected query; activate a result to visit its source |
| Function Up / Down | Navigate freshly parsed function tags in the current buffer |
| Horizontal / vertical relation graph | Reveal Relations and change the presentation of its existing results |
| Lock Context / Relation Window | Keep the current result instead of following the caret; toggle again to resume following |

Calls and references are lexical; members and inheritance use tags. They are not full compiler semantic analysis. Open a project and wait for its index for project-wide results. Missing symbols, history and functions are reported in the status bar; native history arrows are disabled when no transition is available.

Commands reveal the containing panel, nested tabs and nearly collapsed shelf. Detached panels are presented as independent windows. Relations controls share Query, Filters, View, Navigation and Collapse tabs.

![Shared options container](../media/compact-options.svg)
