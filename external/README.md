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

The editor's `clang_calls.cpp` traverses Clang's **C++ AST directly**, using
`ASTUnit`, `RecursiveASTVisitor`, `CallExpr::getDirectCallee` and native USR
generation. These edges feed the existing caller/callee trees and call paths.
This is the first direct C++ adapter; definition indexing, semantic token
classification and completion still use libclang while they are migrated.
It is not a claim that the entire frontend has already abandoned the C API.

Source revisions and C++ SDK headers/libraries must match. To modify Clang,
edit this source checkout and rebuild the SDK. Retain modifications in a fork
and update the gitlink, or keep reviewed patches in this repository and apply
them to the pinned checkout. A root-repository commit alone does not record
uncommitted edits inside a submodule. LLVM/Clang redistribution notices are
copied into the SDK and portable runtime by the existing bundler.
