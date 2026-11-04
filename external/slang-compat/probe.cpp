// SPDX-License-Identifier: GPL-2.0-or-later
#include "slang/ast/Compilation.h"
#include "slang/ast/types/Type.h"
#include "slang/diagnostics/ExpressionsDiags.h"
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
    slang::ast::Compilation wide;
    wide.addSyntaxTree(slang::syntax::SyntaxTree::fromText(
        "module wide; parameter p = 'd999999999999; endmodule"));
    const auto* value = wide.getRoot().lookupName("wide.p");
    if (!value || value->kind != slang::ast::SymbolKind::Parameter ||
        value->as<slang::ast::ParameterSymbol>().getType().getBitWidth() != 40) {
        std::cerr << "Backport regression: unsized based parameter lost its width\n";
        return 7;
    }

    slang::ast::Compilation assertions;
    assertions.addSyntaxTree(slang::syntax::SyntaxTree::fromText(
        "module check; initial $static_assert(((12 < 8))); endmodule"));
    if (assertions.getAllDiagnostics().empty()) {
        std::cerr << "Backport regression: false static assertion produced no diagnostic\n";
        return 8;
    }
    slang::ast::Compilation streaming;
    streaming.addSyntaxTree(slang::syntax::SyntaxTree::fromText(
        "module unused #(parameter int N); logic [7:0] a; "
        "external_cell u(.a({<<{a}})); endmodule "
        "module top; endmodule"));
    for (const auto& diagnostic : streaming.getAllDiagnostics()) {
        if (diagnostic.code == slang::diag::BadStreamContext) {
            std::cerr << "Backport regression: streaming expression rejected in an unresolved port\n";
            return 9;
        }
    }
    return 0;
}
