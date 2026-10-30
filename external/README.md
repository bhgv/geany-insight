# Editable compiler sources

`llvm-project/` is a Git submodule, pinned to LLVM **18.1.8** commit
`3b5b5c1ec4a3095ab096dd780e84d7ab81f3d7ff`. It contains the actual upstream
Clang and LLVM source, not a downloaded prebuilt compiler. The shared LLVM
infrastructure is required by Clang; copying only `clang/` is insufficient.

```sh
git submodule update --init --depth 1 external/llvm-project
python3 insight/tools/build_external_clang.py --jobs 2
meson setup build-editor --prefix="$PWD/install" --libdir=lib \
  -Dclang-sdk="$PWD/_external-clang-sdk" \
  -Dclang=enabled -Dclang-cpp=enabled -Dclang-bundle=true \
  -Dhtml-docs=disabled -Dpdf-docs=disabled -Dapi-docs=disabled -Dgtkdoc=false
meson compile -C build-editor
meson install -C build-editor
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
