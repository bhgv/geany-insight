// SPDX-License-Identifier: GPL-2.0-or-later
#include "slang/ast/Compilation.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/ParameterSymbols.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 3) return 2;
    slang::SourceManager sources;
    slang::ast::Compilation compilation;
    for (int i = 1; i < argc; i++)
        compilation.addSyntaxTree(slang::syntax::SyntaxTree::fromFile(argv[i], sources));
    const auto& root = compilation.getRoot();
    if (!compilation.getAllDiagnostics().empty()) {
        std::cerr << "Unexpected diagnostics in valid cross-file fixture\n";
        return 3;
    }
    const auto* instance = root.lookupName("top.u_leaf");
    const auto* width = root.lookupName("top.u_leaf.WIDTH");
    const auto* input = root.lookupName("top.u_leaf.data_i");
    const auto* output = root.lookupName("top.u_leaf.data_o");
    if (!instance || !width || !input || !output) return 4;
    if (instance->kind != slang::ast::SymbolKind::Instance ||
        width->kind != slang::ast::SymbolKind::Parameter) return 5;
    const auto& parameter = width->as<slang::ast::ParameterSymbol>();
    if (parameter.getValue().integer().as<int>() != 16) return 6;
    std::cout << "Cross-file elaboration: top.u_leaf -> leaf, WIDTH=16, both ports resolved\n";
    return 0;
}
