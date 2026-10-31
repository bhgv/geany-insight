# Editable compiler sources

`llvm-project/` is a Git submodule, pinned to LLVM **18.1.8** commit
`3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff`. It contains the actual upstream
Clang and LLVM source, not a downloaded prebuilt compiler. The shared LLVM
infrastructure is required by Clang; copying only `clang/` is insufficient.

```sh
./build.sh --prefix "$PWD/o" --install
# Or:
./build.sh --backend meson --prefix "$PWD/o" --install
```

The script prepares the compiler automatically and refreshes configure before use.
No separate submodule or SDK command is needed. `--clang-provider system` opts into
system libraries; `--clang-provider disabled` skips the compiler. `--plan` shows the
steps without executing them. `--install` is optional; no sudo is run. Existing
compiler edits are retained. The first compiler build takes substantially longer
than an editor-only build.

![Automatic build pipeline](../insight/docs/media/build-bootstrap.svg)

For manual compiler development only:

```sh
git submodule update --init --depth 1 external/llvm-project
python3 insight/tools/build_external_clang.py --jobs 2
```

CMake, Ninja and a C++17 compiler build the source SDK. GTK development packages,
Meson and gettext build the editor; Linux portable bundling also needs patchelf.
The first LLVM build is substantially heavier than an editor-only build.
Generated files stay in `_external-clang/` and `_external-clang-sdk/`.
`--configure-only` checks the upstream CMake integration; `--plan` prints commands.
No source reset or cleanup is performed: local Clang changes are retained.

The editor's `clang_calls.cpp` uses the native **Clang C++ APIs** throughout:
`ASTUnit` / `RecursiveASTVisitor` for symbols, semantic colors and relationships;
preprocessing records for macros and include dependencies; native USRs and body
spans for navigation; `JSONCompilationDatabase` for settings; and Sema
`CodeCompleteConsumer` for completion. One AST is built for a normal parse. The
libclang C frontend is neither linked nor used as a fallback. In-memory header
buffers remain alive through the AST lifetime; completion uses copied VFS buffers.

Source revisions and C++ SDK headers/libraries must match. To modify Clang,
edit this source checkout and rebuild the SDK. Retain modifications in a fork
and update the gitlink, or keep reviewed patches in this repository and apply
them to the pinned checkout. A root-repository commit alone does not record
uncommitted edits inside a submodule. LLVM/Clang redistribution notices are
copied into the SDK and portable runtime by the existing bundler.

The CI compiler cache is keyed by the pinned revision and build configuration.
`--reuse-build` stages validated existing compiler outputs from a trusted cache
without rebuilding because of fresh checkout timestamps. It rejects local source
edits. Use a normal build after modifying Clang; do not use this cache option.
When updating the gitlink to a different commit, update the explicit PIN in the
builder and review C++ API compatibility as part of that change.

The editor now links only `clang-cpp` and LLVM. The SDK helper builds `clang-cpp`, `llvm-config` and resource headers; `libclang` is not a build target or a runtime requirement. Existing compiler caches may contain unused old libclang files; the portable bundler does not copy them. CI asserts both that the installed editor does not load `libclang.so` and that the installation contains no such library.

## Provider selection

The internal SDK above is the default for Meson and Autotools. Configuration prepares it automatically when needed. Unknown SDK directories or mismatched revisions decline without being overwritten. No system fallback is attempted. Linux/Windows
Meson installs automatically bundle the internal runtime and resource headers.

System Clang is opt-in:

```sh
meson setup build-system -Dclang-provider=system
./configure --with-clang-provider=system
```

A custom internal SDK uses `-Dclang-sdk=/absolute/prefix` or
`--with-clang=/absolute/prefix`; do not combine it with the system provider.
Minimal builds use `-Dclang=disabled` or `--without-clang` and need no SDK.

`clang-bundle=auto` is the default: it bundles internal Linux/Windows SDKs. `-Dclang-bundle=true` also bundles the system provider; `-Dclang-bundle=false` keeps an SDK-linked development install without copying the runtime.

Existing Meson build directories can be reconfigured explicitly with `meson setup --reconfigure build-editor -Dclang-provider=internal -Dclang-bundle=auto`. The internal SDK resolver accepts ELF, Darwin dylib and Windows import-library names; runtime validation for this change uses Linux.

## Autotools and existing Git checkouts

The generated `configure` file is not tracked by Git. After pulling changes to
`configure.ac` or `m4/`, regenerate it before configuring the editor:

```sh
NOCONFIGURE=1 ./autogen.sh
./configure --prefix="$PWD/out"
make -j2
make install
```

The build.sh entry point prepares the SDK automatically; the manual commands above are only needed for separate compiler work.
An old generated script may print the new Git revision yet still search system
Clang; the revision probe does not regenerate that script. A fresh script reports
`Clang provider: internal`. Do not install system Clang to work around a stale
script; explicitly selecting the system provider remains a separate option.

Fresh Autotools/Meson configurations prepare missing internal SDKs automatically. To forbid preparation, explicitly pass `--disable-clang-bootstrap` or `-Dclang-bootstrap=false`.
