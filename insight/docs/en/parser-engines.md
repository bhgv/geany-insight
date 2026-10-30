# Parser engines / Движки разбора

## Clang: source and direct C++ access

The editable LLVM/Clang source is a pinned Git submodule in
`external/llvm-project`. See [external source build instructions](https://github.com/bhgv/geany-insight/blob/main/external/README.md).
`insight/tools/build_external_clang.py` builds an SDK from that checkout.
Meson accepts `-Dclang-sdk=ABSOLUTE_PREFIX -Dclang-cpp=enabled`; the portable
bundler carries both libclang and clang-cpp plus LLVM dependencies and licenses.

The direct C++ adapter derives resolved call edges and expression references
from Clang AST (`ASTUnit`, `RecursiveASTVisitor`, `CallExpr::getDirectCallee`,
`DeclRefExpr`, `MemberExpr`, USRs). These feed caller/callee trees, call paths
and reference searches. Resolved expression/call destinations also come from
the direct AST, preferring definitions to declarations in the translation unit.
Supported type/alias/template/namespace references, explicit function definitions
and compound-body spans also come from the direct AST. Symbol-list declarations,
tokens and completion still use the existing C frontend. Unsupported constructs
retain their frontend metadata. Unevaluated operands retain references without
becoming executable call edges. It is a staged migration, not a
claim that the entire compiler frontend has already moved away from libclang.
The transitional adapter builds its own AST in a worker; it adds parsing cost.
If that parse fails, existing call edges and references are retained. Arbitrary runtime function
pointers and dynamic dispatch targets are not inferred. Lambda-body edges are
not attributed to the enclosing function; capture initializers are evaluated
in the enclosing function and retain its call edges. Expression references
inside deferred bodies remain searchable. See [Clang reference details](clang.md#expression-references-through-the-c-ast).

## Other languages: candidates, not implemented backends

Tree-sitter provides a C runtime and incremental syntax trees, including useful
trees for incomplete code. Its grammars do not by themselves resolve project-wide
names and types. Existing Ctags/lexical services remain active until a new backend
is implemented and validated.

| Language | Syntax tree candidate | Semantic analysis candidate | Integration considerations |
|---|---|---|---|
| Python | [tree-sitter-python](https://github.com/tree-sitter/tree-sitter-python), [CPython ast](https://docs.python.org/3/library/ast.html) | [Pyright](https://github.com/microsoft/pyright), CPython [symtable](https://docs.python.org/3/library/symtable.html) for lexical scopes | CPython AST/symbol tables do not infer arbitrary runtime types or all imports. Pyright brings its own runtime/toolchain. |
| JavaScript / TypeScript | [tree-sitter-javascript](https://github.com/tree-sitter/tree-sitter-javascript), [tree-sitter-typescript](https://github.com/tree-sitter/tree-sitter-typescript) | [TypeScript compiler/language service](https://github.com/microsoft/TypeScript/wiki/Using-the-Compiler-API) | Pin a compiler revision; API generations differ. JS dynamic behavior remains approximate. |
| Java | [tree-sitter-java](https://github.com/tree-sitter/tree-sitter-java) | [Eclipse JDT ASTParser](https://help.eclipse.org/latest/topic/org.eclipse.jdt.doc.isv/reference/api/org/eclipse/jdt/core/dom/ASTParser.html), [javac JavacTask](https://docs.oracle.com/en/java/javase/21/docs/api/jdk.compiler/com/sun/source/util/JavacTask.html) | JDT bindings/javac analysis require project classpaths and a Java runtime. |
| Lua | [tree-sitter-lua](https://github.com/tree-sitter-grammars/tree-sitter-lua) | [LuaLS](https://github.com/LuaLS/lua-language-server) | Native Lua parser emits VM-oriented data; LuaLS provides richer editor semantics. Luau is a distinct dialect and cannot silently replace ordinary Lua. |

Для редактора целесообразно разделить быстрый синтаксический слой и семантику.
Tree-sitter можно встроить на C без Python/Node/Java для самого синтаксического
разбора. Точные импорты, типы, перегрузки и межфайловые ссылки требуют отдельного
анализатора соответствующего языка. Таблица — подбор кандидатов; эти четыре
новых backend-а ещё не объявляются готовыми или подключёнными.

Sources: [Clang interface choices](https://clang.llvm.org/docs/Tooling.html),
[LibTooling](https://clang.llvm.org/docs/LibTooling.html),
[Tree-sitter design](https://tree-sitter.github.io/tree-sitter/).
