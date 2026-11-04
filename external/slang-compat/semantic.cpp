// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only, process-isolated native AST service. stdout is one protocol record.
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Definition.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/text/SourceManager.h"
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <unordered_set>
using namespace slang;
using namespace slang::ast;
namespace {
std::string quote(string_view value) {
    std::string out = "\"";
    const char hex[] = "0123456789abcdef";
    for (unsigned char c : value) {
        if (c == '"' || c == '\\') { out += '\\'; out += char(c); }
        else if (c < 32) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += char(c);
    }
    return out + '"';
}
std::string position(const SourceManager& sources, SourceLocation loc) {
    if (!loc) return "null";
    return "{\"path\":" + quote(sources.getFullPath(loc.buffer()).string()) +
           ",\"line\":" + std::to_string(sources.getLineNumber(loc)) +
           ",\"column\":" + std::to_string(sources.getColumnNumber(loc)) + "}";
}
std::string identity(const Symbol& symbol) {
    std::string name;
    symbol.getHierarchicalPath(name);
    return name;
}
std::string record(const SourceManager& sources, const Symbol& symbol) {
    return "{\"name\":" + quote(symbol.name) + ",\"hierarchy\":" + quote(identity(symbol)) +
           ",\"kind\":" + quote(toString(symbol.kind)) +
           ",\"declaration\":" + position(sources, symbol.location) + "}";
}
struct Collector : ASTVisitor<Collector, true, true> {
    const SourceManager& sources;
    std::vector<std::string> rows;
    bool references;
    explicit Collector(const SourceManager& sources, bool references) : sources(sources), references(references) {}
    void add(std::string row) {
        if (rows.size() >= 100000) throw std::runtime_error("semantic result exceeds 100000 records");
        rows.push_back(std::move(row));
    }
    void reference(const Symbol& symbol, SourceLocation location) {
        if (references && location && symbol.location)
            add("{\"source\":" + position(sources, location) + ",\"target\":" + record(sources, symbol) + "}");
    }
    template<typename T> void handle(const T& node) {
        if constexpr (std::is_base_of_v<Symbol, T>) {
            if (!references && !node.name.empty() && node.location) add(record(sources, node));
        }
        visitDefault(node);
    }
    void handle(const InstanceSymbol& node) {
        if (references) {
            const auto& definition = node.getDefinition();
            add("{\"source\":" + position(sources, node.location) +
                ",\"instance\":" + quote(identity(node)) + ",\"definition\":" + quote(definition.name) +
                ",\"target\":" + position(sources, definition.location) + "}");
        }
        else if (node.location) add(record(sources, node));
        visitDefault(node);
    }
    void handle(const CallExpression& node) {
        if (!node.isSystemCall()) reference(*std::get<0>(node.subroutine), node.sourceRange.start());
        visitDefault(node);
    }
    void handle(const NamedValueExpression& node) { reference(node.symbol, node.sourceRange.start()); visitDefault(node); }
    void handle(const HierarchicalValueExpression& node) { reference(node.symbol, node.sourceRange.start()); visitDefault(node); }
};
}
int main(int argc, char** argv) {
    try {
        if (argc < 4) throw std::runtime_error("usage: insight-slang-semantic --symbols|--references|--lookup NAME|--complete SCOPE PREFIX -- FILE...");
        std::string mode = argv[1], scope, prefix;
        int first = 2;
        if (mode == "--lookup" || mode == "--complete") {
            if (first >= argc) throw std::runtime_error("missing scope");
            scope = argv[first++];
            if (mode == "--complete") {
                if (first >= argc) throw std::runtime_error("missing prefix");
                prefix = argv[first++];
            }
        }
        else if (mode != "--symbols" && mode != "--references") throw std::runtime_error("unknown operation");
        if (first >= argc || std::string(argv[first++]) != "--" || first >= argc)
            throw std::runtime_error("expected -- followed by source files");
        SourceManager sources;
        Compilation compilation;
        for (int i = first; i < argc; ++i)
            compilation.addSyntaxTree(syntax::SyntaxTree::fromFile(argv[i], sources));
        const auto& root = compilation.getRoot();
        const auto diagnostics = compilation.getAllDiagnostics().size();
        Collector collector(sources, mode == "--references");
        if (mode == "--symbols" || mode == "--references") root.visit(collector);
        else {
            const Symbol* symbol = scope.empty() ? &root : root.lookupName(scope);
            if (symbol && mode == "--lookup") collector.add(record(sources, *symbol));
            else if (symbol) {
                const Scope* members = nullptr;
                if (symbol->kind == SymbolKind::Instance) members = &symbol->as<InstanceSymbol>().body;
                else if (symbol->isScope()) members = &symbol->as<Scope>();
                std::unordered_set<std::string> seen;
                if (members) for (const auto& member : members->members()) {
                    if (!member.name.empty() && member.name.substr(0, prefix.size()) == prefix &&
                        seen.insert(identity(member)).second)
                        collector.add(record(sources, member));
                }
            }
        }
        // Buffer the complete reply: exceptions cannot leave a partial JSON document.
        std::ostringstream out;
        out << "{\"protocol\":1,\"diagnostics_count\":" << diagnostics << ",\"records\":[";
        for (size_t i = 0; i < collector.rows.size(); ++i) {
            if (i) out << ',';
            out << collector.rows[i];
        }
        out << "]}\n";
        std::cout << out.str();
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "slang semantic analysis: " << error.what() << '\n';
        return 1;
    }
}
