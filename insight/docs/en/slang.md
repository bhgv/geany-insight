# SystemVerilog slang: old-toolchain compatibility experiment

![Native C++ compatibility probe](../media/slang-compat.svg)

The pinned source in `external/slang` is upstream **slang 3.0** (`7efcca2e348ecf1e01d1859f7ea85d6202fc9f91`), the last major release before the move to C++20. This is an explicit compatibility variant, not current slang 12.x and not a completed editor integration. The current editor continues using its existing Verilog/SystemVerilog Ctags and lexical highlighting.

## Build without upgrading the system toolchain

On Ubuntu 18.04 the target tools are CMake **3.10.2**, GCC/G++ **7.5**, Python **3.6** and Make. Git and CA certificates are needed for the first online source preparation. The common `build.sh` driver builds slang automatically for Meson and Autotools, independently of the Clang provider. Use `--slang-provider disabled` to skip it explicitly.

```bash
python3 insight/tools/build_external_slang.py --jobs 2 --check
```

The helper initializes only the slang submodule, fetches three exact dependency commits, applies `external/slang-compat/cmake310-gcc7.patch`, builds a static library and the slang command-line tool, then runs a **direct C++ API probe**. Its two separate SystemVerilog files declare a module and instantiate it with `WIDTH=16`; the probe resolves the cross-file instance, its overridden parameter and both ports. This establishes elaboration/API access, not completion or references inside the editor.

Sources and dependencies are editable in `external/slang` and `external/slang-deps`. Existing initialized checkouts are never reset; a wrong revision or conflicting patch stops preparation. A second run recognizes the already applied patch. `--prepare-only` downloads/prepares without CMake. `--build PATH` and `--deps PATH` select separate output/dependency directories. `_external-slang/source-manifest.json` records source/dependency revisions and the CMake version.

## Adaptation

- Remove reliance on FetchContent, modern Python discovery and newer CMake install/source-path behavior in the compatibility route.
- Pin fmt 9.1.0, unordered_dense 2.0.0 and ghc::filesystem 1.5.14 by commit; CMake performs no network downloads.
- Use ghc::filesystem consistently in the adapted public headers and implementation, avoiding GCC 7's missing `std::filesystem`. Consumers must compile against the adapted headers and dependencies; an unpatched system slang ABI is not interchangeable.
- Replace the integer-only `std::from_chars` calls missing in GCC 7; boundary tests cover signed/unsigned limits, overflow and invalid input.
- Spell out container types where GCC 7 lacks C++17 deduction guides.
- Keep C++17, original parser and semantic/elaboration code. Python bindings, slang-tidy, upstream test suite and installation/export targets are excluded from this experiment. The fixture is a focused integration check, not full language conformance testing.

The CI workflow `insight-slang-compat.yml` uses the actual Ubuntu 18.04 toolchain in a container. The host checkout avoids requiring modern Node.js inside the old container. The Windows/UCRT64 CI also builds the engine, stages the SDK on drive D and compiles/runs a separate C++ consumer from an install path containing spaces. macOS has not been validated.

## What newer versions add

Upstream [4.0](https://github.com/MikePopoloski/slang/releases/tag/v4.0) adds checkers, net aliases and parallel parsing; [6.0](https://github.com/MikePopoloski/slang/releases/tag/v6.0) adds configurations, [7.0](https://github.com/MikePopoloski/slang/releases/tag/v7.0) completes support for IEEE 1800-2023 changes, and [9.0](https://github.com/MikePopoloski/slang/releases/tag/v9.0) adds a data-flow analysis framework and substantially improved instance caching. Later releases improve diagnostics, analysis APIs and correctness. [12.1](https://github.com/MikePopoloski/slang/releases/tag/v12.1) fixes many corner cases in constant evaluation, name resolution and instance caching. Compatibility 3.0 does not inherit those features/fixes.

Current upstream [build requirements](https://sv-lang.com/building.html) include C++20 and CMake 3.28+. Supporting old build tools with 3.0 is a first experiment, not a commitment to freezing the editor's HDL engine on an old release. Porting a newer C++20 engine to GCC 7 requires a separate feasibility check, not just lowering `cmake_minimum_required`.

## Automatic build and staged SDK

`./build.sh --slang-only --install --prefix "$PWD/hdl-tools"` builds and stages only slang, without GTK/Clang prerequisites. Normal editor `--install` also stages the engine. The console tool is under `libexec/geany-insight/slang`; the static SDK is under `lib/geany-insight/slang` with merged/generated headers, dependency headers, `libsvlang.a`, `libfmt.a`, pkg-config metadata and licenses. The compatibility CI compiles the C++ elaboration probe against that installed SDK. Ninja is preferred when available; existing build directories retain their generator.

## Native HDL semantic service

The build also produces `insight-slang-semantic` (`.exe` on Windows). Installation stages it beside the slang CLI in `libexec/geany-insight/slang`. It uses the bundled engine's C++ AST directly, without LSP or a system parser.

```bash
_external-slang/insight-slang-semantic --lookup top.u_leaf.data_i -- leaf.sv top.sv
_external-slang/insight-slang-semantic --complete top.u_leaf data_ -- leaf.sv top.sv
_external-slang/insight-slang-semantic --references -- leaf.sv top.sv
_external-slang/insight-slang-semantic --symbols -- leaf.sv top.sv
```

Each request reads the explicit file set and elaborates it together. stdout contains one JSON object: `protocol: 1`, `diagnostics_count`, and `records`. Locations carry an absolute path and one-based line/column. Lookup takes a hierarchical name; completion takes a scope and case-sensitive prefix. Completion deduplicates port/internal-variable pairs. References cover instance-to-module definitions, value accesses and user function calls, including functions in a separate package file. System calls do not acquire invented source declarations.

A nonzero `diagnostics_count` marks results requiring inspection; JSON alone does not establish project correctness. Missing symbols return an empty array. Read/argument errors return nonzero, explain the failure on stderr and leave stdout empty. A 100000-record limit fails without partial JSON. Sources are never modified. An explicit file list can represent a temporary workspace without saving a project.

This prepares the editor integration: **completion, definition navigation and a saved HDL reference index are connected; automatic semantic reindexing and HDL graphs remain pending**. Each process reparses its input; persistent AST caching, include/define/top options and complete reference coverage are not connected yet. A caller can terminate the process without GTK access. Editor highlighting remains lexical.

The `slang_semantic_protocol` test exercises the real executable: cross-file targets, package function calls, deduplicated completion, exact positions, erroneous input and paths with spaces.

## Completion inside the editor

With detailed and semantic completion enabled (completion preferences), typing `u_leaf.data_` inside a module proposes members of that instance. The existing popup marks their origin as `slang`. The normal explicit completion command also requests candidates. The current build's installed `insight-slang-semantic` is required; without it ordinary tag completion remains available. Developers can override its path with `GEANY_INSIGHT_SLANG_SERVICE`.

Requests combine current project members and open HDL files. Without a project, named open documents form a temporary source set. Unsaved buffers are copied to a temporary directory removed after the request; originals and project files are not written. Snapshot I/O, process execution and waiting happen in a worker. New input cancels the request and child process; buffer contents, position and project are checked before publishing a reply.

Plain dotted hierarchies and package `::` qualifiers are supported. File/line identifies the current module. Multiple elaborated instances do not acquire a guessed implicit scope; use an explicit full hierarchy instead. Indexed/dynamic receivers, precise procedural local scope, function arguments, include/define/top settings and persistent caching are not implemented yet. Requests are bounded to 256 HDL files, 2 MiB per buffer, 32 MiB of snapshots and a portable command-line length; excessive requests fall back to tag completion. Headers requiring their include context do not yet receive full semantics. Each request elaborates the set again, so large HDL projects still need the next caching stage.

## HDL declaration navigation

The normal Go to Definition command in navigation/editor actions now uses slang's AST for Verilog/SystemVerilog. Place the caret on a signal reference, function call, module type, instance name or named port connection. Navigation opens the declaration at its exact line and column; Back/Forward retain both source and destination positions. Semantic completion does not need to be enabled for this command.

The shared worker uses current project members and open files, including unsaved buffers. Replies are discarded if the caret, buffers, project or membership change. Destination contents are checked before moving the caret. Changes to explicitly supplied disk files during analysis also discard the reply. Ambiguous elaboration produces no jump and a status message. A failed slang request is not replaced by a same-name guess.

The internal `--editor-definition FILE LINE COLUMN -- FILE...` mode uses one-based lines and UTF-8 byte columns. Output starts with `insight-slang-definition-v1`, followed on success by one tab-separated record: hex-encoded UTF-8 path, line and column. `--overlay ORIGINAL SNAPSHOT` before `--` supplies the same snapshots as completion. Multiple instances sharing a physical declaration are accepted; different destinations for the same source range produce an empty result.

Coverage includes bound value/member expressions, user calls and module/named-port links. This is not every SystemVerilog syntax reference: types, macros and some preprocessor constructs still need separate coverage. Persistent AST caching, HDL graphs and automatic semantic reindexing remain future work.

## Repeated-request cache

The worker adapter retains up to eight recent completion/navigation replies, totaling at most 4 MiB. An identical file set, snapshot and position can reuse a reply without starting slang again. The SHA-256 key covers source order and full contents, unsaved buffers, query arguments, working directory and executable version. Content changes invalidate reuse even if file size stays the same. Inputs are checked again before storing a reply.

This is a thread-safe process-local cache. File reads and key calculation stay in workers, without blocking GTK. **Any source set containing a backtick (preprocessor directives/macros) bypasses caching**, so changing an include or its lookup path cannot leave stale suggestions. A conservative bypass for backticks inside comments/strings is acceptable. This caches replies, not persistent ASTs; changed text and include-based projects still run the parser again.

## Saved index and Relations panel

Choose **HDL references (slang)…** from Navigation or the editor popup, or **References (slang HDL, by name)** in Relations. A request elaborates project HDL members and named open HDL files together. Rows show reference locations and destinations (`kind → file:line:column`). Activating a row opens the reference; Go to Definition opens its bound declaration. Filtering is by name, so distinct same-named symbols may appear with different destinations; this is not compiler-identity filtering.

The AST supplies declarations and value/member/user-call/instance/module/named-port references. They are atomically saved as a separate project index component: a **`.hdl` file beside the main project database**. These are semantic facts, not serialized ASTs. Status reports declaration/reference counts, diagnostic count, saved/restored/memory-only state.

The index is built **on a Relations request**, rather than on every document opening or once per project file. For sets without preprocessing, subsequent requests/editor sessions restore it after checking complete source contents, membership/order and the engine executable. Changes trigger joint rebuilding on the next request. Sets containing directives/macros are saved too but currently rebuilt on request: transitive include dependency validation for reuse is unfinished. Diagnostic-bearing indexes can also be restored for unchanged non-preprocessor sets, preserving messages and their nonzero diagnostic count. Changed source snapshots/membership reject stale publication.

Without a project the index stays in memory. Ctags symbols continue using the existing persistent project DB mechanism. Requests are bounded to 256 files, 32 MiB of snapshots and 100000 records. Project-index replies are capped at 64 MiB (completion/navigation: 4 MiB); the panel shows the first 2000 references. Full interactive HDL graphs remain unconnected. Detailed messages and compiler name suggestions are now available through diagnostics; automatic application of fixes is not implemented.

Open-file snapshots are registered with slang before parsing any compilation unit, so includes can see unsaved selected headers. With main `.v/.sv` sources present, `.vh/.svh` headers are not parsed again as standalone units; they are reached through includes. Include-path/define configuration is still not connected.

## Diagnostics and typo assistance

Choose **Navigation → HDL diagnostics (slang)…**, the matching editor popup action, or **Diagnostics (slang HDL)** in Relations. The command checks the whole current HDL set and works on a blank line without a symbol under the caret. Detailed completion does not need to be enabled.

The table shows severity (error/warning/note), file, line, UTF-8 byte column, formatted message and stable diagnostic code. Activating a row opens its source line through Context and the shared position history. Project-wide messages without a location are marked separately and cannot trigger navigation. The existing results filter can search message text. The first 2000 rows are shown; compiler diagnostic counts can differ from displayed message counts because of notes and engine filtering.

For some unknown names slang emits `did you mean ...?` with a nearby visible declaration, such as suggesting `target_signal` for `target_signl`. This is a compiler suggestion, not proof of algorithmic correctness. The editor displays the advice without applying or saving changes. Not every syntax or logic error has a suggested fix.

Diagnostics use selected unsaved sources and headers. Messages are stored in the semantic index as `@hdldiag`, and reused for unchanged non-preprocessor sets together with their nonzero diagnostic count. Project exchange uses `insight-slang-index-v2`; older saved semantic indexes are rebuilt. Message bodies remain slang's original English while interface labels switch EN/RU. This check does not use system Clang.

## Semantic highlighting

Open a Verilog or SystemVerilog tab. When the installed slang helper finishes background analysis, the editor automatically colors recognized modules, instances, ports, subroutines, parameters and signals in that tab. The lexer still colors keywords, comments and strings. Highlighting uses the same project index, including a restored one, and applies colors only near the visible lines; scrolling reveals more colors without rerunning slang. The parser and token preparation run in a worker; the editor copies open buffers and applies colors in short idle slices. An HDL edit invalidates the old colors and schedules fresh analysis after a short pause. Background coloring does not open Relations or replace its results. Explicit diagnostics and reference searches remain available.

## Vivado HDL settings

Open the Vivado project and run these commands in its Tcl Console:

```tcl
source C:/my/geany-insight/insight/tools/export-vivado-settings.tcl
geany_export_hdl_settings C:/my/vivado-hdl.tsv
```

Start Geany Insight with `GEANY_INSIGHT_VIVADO_CONFIG` set to the absolute path of `vivado-hdl.tsv`. For example, in Windows Command Prompt:

```cmd
set "GEANY_INSIGHT_VIVADO_CONFIG=C:\my\vivado-hdl.tsv"
C:\gi\editor\bin\geany.exe -i
```

The export uses the current design fileset's include directories, Verilog defines and top module. Select the HDL sources in Geany Insight's project membership separately. Requests with this configuration rebuild analysis so changes to external includes cannot reuse a stale index. Restart the editor with a different environment variable to select another Vivado export.

## Vivado project export and VCD viewing

In the open Vivado project's Tcl Console, load the exporter and create the two files:

```tcl
source C:/my/geany-insight/insight/tools/export-vivado-settings.tcl
geany_export_project C:/my/chip.geany
```

Open `chip.geany` using Project → Open. The project contains existing HDL files from the current design fileset. For semantic analysis start the editor with `GEANY_INSIGHT_VIVADO_CONFIG` pointing to the adjacent `chip.hdl.tsv`, as shown above. Choose a new filename when exporting again.

Export digital signals to VCD in Vivado Simulator, then choose **Tools → VCD Waveforms…** in Geany Insight. Select a signal, click the diagram to position the cursor, or use the transition and zoom buttons. The source field starts with the VCD hierarchy. Edit it if the simulator adds an outer scope, then choose **Go to HDL declaration**. Slang resolves the name using project and open HDL files. Captures are limited to 32 MiB, 4096 signals and 250000 changes. The viewer displays one selected trace at a time.
