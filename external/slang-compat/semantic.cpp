// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only, process-isolated native AST service. stdout is one protocol record.
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Compilation.h"
#include "slang/ast/Definition.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/syntax/AllSyntax.h"
#include <fstream>
#include <unordered_map>
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
std::string hex(string_view value) {
    const char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : value) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
struct Locate : ASTVisitor<Locate, false, false> {
    const SourceManager& sources;
    std::string path, hierarchy;
    size_t line;
    bool ambiguous = false;
    Locate(const SourceManager& sources, std::string path, size_t line) : sources(sources), path(ghc::filesystem::weakly_canonical(path).generic_string()), line(line) {}
    void handle(const InstanceSymbol& instance) {
        auto range = instance.getDefinition().syntax.sourceRange();
        if (range.start() && sources.getFullPath(range.start().buffer()).lexically_normal().generic_string() == path &&
            sources.getLineNumber(range.start()) <= line && sources.getLineNumber(range.end()) >= line) {
            auto name = identity(instance);
            if (!hierarchy.empty() && hierarchy != name) ambiguous = true;
            hierarchy = std::move(name);
        }
        visitDefault(instance);
    }
};
// Choose the narrowest bound AST reference containing the requested byte column.
// Different elaborated targets at the same source range are explicitly ambiguous.
struct Jump : ASTVisitor<Jump, true, true> {
    const SourceManager& sources;
    std::string path;
    size_t line, column, width = SIZE_MAX;
    SourceLocation target;
    bool ambiguous = false;
    Jump(const SourceManager& s, const std::string& p, size_t l, size_t c) :
        sources(s), path(ghc::filesystem::weakly_canonical(p).generic_string()), line(l), column(c) {}
    void consider(SourceRange range, SourceLocation destination) {
        auto begin = sources.getFullyOriginalLoc(range.start());
        auto end = sources.getFullyOriginalLoc(range.end());
        if (!begin || !end || !destination || begin.buffer() != end.buffer()) return;
        if (sources.getFullPath(begin.buffer()).lexically_normal().generic_string() != path) return;
        auto first = std::make_pair(sources.getLineNumber(begin), sources.getColumnNumber(begin));
        auto last = std::make_pair(sources.getLineNumber(end), sources.getColumnNumber(end));
        auto cursor = std::make_pair(line, column);
        if (cursor < first || cursor >= last || end.offset() < begin.offset()) return;
        size_t length = end.offset() - begin.offset();
        if (length < width) { width = length; target = destination; ambiguous = false; }
        else if (length == width && target != destination) {
            // Multiple instances can share the same physical declaration.
            auto a = sources.getFullyOriginalLoc(target), b = sources.getFullyOriginalLoc(destination);
            if (sources.getFullPath(a.buffer()) != sources.getFullPath(b.buffer()) ||
                sources.getLineNumber(a) != sources.getLineNumber(b) || sources.getColumnNumber(a) != sources.getColumnNumber(b)) ambiguous = true;
        }
    }
    void boundName(const Expression& node, const Symbol& symbol) {
        if (!node.syntax) return;
        auto token = node.syntax->getLastToken();
        if (token.valueText() == symbol.name) consider(token.range(), symbol.location);
    }
    void handle(const NamedValueExpression& node) { boundName(node, node.symbol); visitDefault(node); }
    void handle(const HierarchicalValueExpression& node) { boundName(node, node.symbol); visitDefault(node); }
    void handle(const MemberAccessExpression& node) { boundName(node, node.member); visitDefault(node); }
    void handle(const CallExpression& node) {
        if (!node.isSystemCall() && node.syntax) {
            const syntax::ExpressionSyntax* callee = node.syntax;
            if (callee->kind == syntax::SyntaxKind::InvocationExpression)
                callee = callee->as<syntax::InvocationExpressionSyntax>().left;
            auto token = callee->getLastToken();
            const auto& subroutine = *std::get<0>(node.subroutine);
            if (token.valueText() == subroutine.name) consider(token.range(), subroutine.location);
        }
        visitDefault(node);
    }
    void handle(const InstanceSymbol& node) {
        const auto* syntax = node.getSyntax();
        if (syntax && syntax->kind == syntax::SyntaxKind::HierarchicalInstance) {
            const auto& instance = syntax->as<syntax::HierarchicalInstanceSyntax>();
            if (instance.decl) consider(instance.decl->name.range(), node.getDefinition().location);
            if (syntax->parent && syntax->parent->kind == syntax::SyntaxKind::HierarchyInstantiation)
                consider(syntax->parent->as<syntax::HierarchyInstantiationSyntax>().type.range(), node.getDefinition().location);
            for (const auto* connection : instance.connections) {
                if (connection->kind != syntax::SyntaxKind::NamedPortConnection) continue;
                const auto& port = connection->as<syntax::NamedPortConnectionSyntax>();
                if (const auto* symbol = node.body.findPort(port.name.valueText())) consider(port.name.range(), symbol->location);
            }
        }
        visitDefault(node);
    }
};
struct ProjectIndex : ASTVisitor<ProjectIndex, true, true> {
    const SourceManager& sources;
    std::vector<std::string> rows;
    size_t bytes = 0;
    explicit ProjectIndex(const SourceManager& sources) : sources(sources) {}
    bool located(SourceLocation location) const { return location && sources.getFullPath(location.buffer()).is_absolute(); }
    void add(const std::string& type, string_view name, SourceLocation location, string_view scope, string_view detail, string_view relation) {
        if (!located(location) || name.empty()) return;
        if (rows.size() >= 100000) throw std::runtime_error("HDL index exceeds 100000 records");
        std::string row = type + "\t" + hex(name) + "\t" + hex(sources.getFullPath(location.buffer()).string()) + "\t" +
            std::to_string(sources.getLineNumber(location)) + "\t" + std::to_string(sources.getColumnNumber(location)) + "\t" + hex(scope) + "\t" + hex(detail) + "\t" + hex(relation);
        if (bytes + row.size() + 1 > 64u * 1024u * 1024u - 128) throw std::runtime_error("HDL index reply exceeds 64 MiB");
        bytes += row.size() + 1; rows.push_back(std::move(row));
    }
    void reference(string_view name, SourceRange range, SourceLocation target, string_view kind) {
        if (!located(target)) return;
        auto location = sources.getFullyOriginalLoc(range.start());
        add("R", name, location, sources.getFullPath(target.buffer()).string(),
            std::to_string(sources.getLineNumber(target)) + ":" + std::to_string(sources.getColumnNumber(target)), kind);
    }
    void boundName(const Expression& node, const Symbol& symbol, string_view kind) {
        if (!node.syntax) return;
        auto token = node.syntax->getLastToken();
        if (token.valueText() == symbol.name) reference(symbol.name, token.range(), symbol.location, kind);
    }
    template<typename T> void handle(const T& node) {
        if constexpr (std::is_base_of_v<Symbol, T>) add("D", node.name, node.location, identity(node), toString(node.kind), "");
        visitDefault(node);
    }
    void handle(const NamedValueExpression& node) { boundName(node, node.symbol, "value"); visitDefault(node); }
    void handle(const HierarchicalValueExpression& node) { boundName(node, node.symbol, "value"); visitDefault(node); }
    void handle(const MemberAccessExpression& node) { boundName(node, node.member, "member"); visitDefault(node); }
    void handle(const CallExpression& node) {
        if (!node.isSystemCall() && node.syntax) {
            const syntax::ExpressionSyntax* callee = node.syntax;
            if (callee->kind == syntax::SyntaxKind::InvocationExpression) callee = callee->as<syntax::InvocationExpressionSyntax>().left;
            auto token = callee->getLastToken();const auto& function = *std::get<0>(node.subroutine);
            if (token.valueText() == function.name) reference(function.name, token.range(), function.location, "call");
        }visitDefault(node);
    }
    void handle(const InstanceSymbol& node) {
        add("D", node.name, node.location, identity(node), "Instance", "");
        add("D", node.getDefinition().name, node.getDefinition().location, node.getDefinition().name, "Module", "");
        const auto* syntax = node.getSyntax();
        if (syntax && syntax->kind == syntax::SyntaxKind::HierarchicalInstance) {
            const auto& instance = syntax->as<syntax::HierarchicalInstanceSyntax>();
            if (instance.decl) reference(node.name, instance.decl->name.range(), node.getDefinition().location, "instance");
            if (syntax->parent && syntax->parent->kind == syntax::SyntaxKind::HierarchyInstantiation) {
                auto token = syntax->parent->as<syntax::HierarchyInstantiationSyntax>().type;
                reference(token.valueText(), token.range(), node.getDefinition().location, "module");
            }
            for (const auto* connection : instance.connections) {
                if (connection->kind != syntax::SyntaxKind::NamedPortConnection) continue;
                const auto& port = connection->as<syntax::NamedPortConnectionSyntax>();
                if (const auto* symbol = node.body.findPort(port.name.valueText())) reference(symbol->name, port.name.range(), symbol->location, "port");
            }
        }visitDefault(node);
    }
};
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
        std::string mode = argv[1], scope, prefix, sourcePath;
        size_t sourceLine = 0;
        bool editor = mode == "--editor-complete";
        bool definition = mode == "--editor-definition";
        size_t sourceColumn = 0;
        std::unordered_map<std::string, std::string> overlays;
        int first = 2;
        if (definition) {
            if (argc < 7) throw std::runtime_error("missing editor definition arguments");
            sourcePath = argv[first++];
            std::string l = argv[first++], c = argv[first++];
            if (l.empty() || c.empty() || l.find_first_not_of("0123456789") != std::string::npos || c.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("invalid source position");
            sourceLine = std::stoul(l); sourceColumn = std::stoul(c);
            if (!sourceLine || !sourceColumn) throw std::runtime_error("invalid source position");
        }
        else if (editor) {
            if (argc < 8) throw std::runtime_error("missing editor completion arguments");
            sourcePath = argv[first++];
            std::string line = argv[first++];
            if (line.empty() || line.find_first_not_of("0123456789") != std::string::npos) throw std::runtime_error("invalid source line");
            sourceLine = std::stoul(line);
            if (!sourceLine) throw std::runtime_error("invalid source line");
            scope = argv[first++]; prefix = argv[first++];
        }
        else if (mode == "--lookup" || mode == "--complete") {
            if (first >= argc) throw std::runtime_error("missing scope");
            scope = argv[first++];
            if (mode == "--complete") {
                if (first >= argc) throw std::runtime_error("missing prefix");
                prefix = argv[first++];
            }
        }
        else if (mode != "--symbols" && mode != "--references" && mode != "--editor-index") throw std::runtime_error("unknown operation");
        while (first < argc && std::string(argv[first]) == "--overlay") {
            if (first + 2 >= argc) throw std::runtime_error("missing overlay arguments");
            std::string path = argv[first + 1];
            std::ifstream input(argv[first + 2], std::ios::binary);
            if (!input) throw std::runtime_error("cannot read overlay");
            std::ostringstream text; text << input.rdbuf();
            if (text.str().size() > 2u * 1024u * 1024u) throw std::runtime_error("overlay exceeds 2 MiB");
            overlays[path] = text.str(); first += 3;
        }
        if (first >= argc || std::string(argv[first++]) != "--" || first >= argc)
            throw std::runtime_error("expected -- followed by source files");
        SourceManager sources;
        // Includes must see unsaved headers before any compilation unit parses.
        std::unordered_map<std::string, std::string> normalized;
        for (const auto& overlay : overlays) {
            auto name = ghc::filesystem::weakly_canonical(overlay.first).string();
            auto existing = normalized.find(name);
            if (existing != normalized.end() && existing->second != overlay.second)
                throw std::runtime_error("conflicting overlays for the same source");
            normalized[name] = overlay.second;
        }
        for (const auto& overlay : normalized) sources.assignText(overlay.first, overlay.second);
        auto header = [](const char* name) {
            std::string extension = ghc::filesystem::path(name).extension().string();
            for (char& c : extension) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
            return extension == ".vh" || extension == ".svh";
        };
        bool hasUnit = false;
        for (int i = first; i < argc; ++i) if (!header(argv[i])) hasUnit = true;
        Compilation compilation;
        std::unordered_set<std::string> units;
        for (int i = first; i < argc; ++i) {
            if (hasUnit && header(argv[i])) continue;
            auto name = ghc::filesystem::weakly_canonical(argv[i]).string();
            if (units.insert(name).second) compilation.addSyntaxTree(syntax::SyntaxTree::fromFile(name, sources));
        }
        const auto& root = compilation.getRoot();
        const auto diagnostics = compilation.getAllDiagnostics().size();
        if (mode == "--editor-index") {
            ProjectIndex index(sources);root.visit(index);
            std::ostringstream output;output << "insight-slang-index-v1\n" << diagnostics << '\n';
            for (const auto& row : index.rows) output << row << '\n';
            std::cout << output.str();return 0;
        }
        if (definition) {
            Jump jump(sources, sourcePath, sourceLine, sourceColumn); root.visit(jump);
            std::cout << "insight-slang-definition-v1\n";
            if (jump.target && !jump.ambiguous) {
                auto location = sources.getFullyOriginalLoc(jump.target);
                std::cout << hex(sources.getFullPath(location.buffer()).string()) << '\t' << sources.getLineNumber(location) << '\t' << sources.getColumnNumber(location) << '\n';
            }
            return 0;
        }
        Collector collector(sources, mode == "--references");
        if (mode == "--symbols" || mode == "--references") root.visit(collector);
        else {
            const Symbol* symbol = nullptr;
            if (editor) {
                Locate locate(sources, sourcePath, sourceLine); root.visit(locate);
                if (!locate.ambiguous && !locate.hierarchy.empty())
                    symbol = root.lookupName(locate.hierarchy + (scope.empty() ? "" : "." + scope));
                // Explicit absolute hierarchies remain usable outside module bodies.
                if (!symbol && !scope.empty()) symbol = root.lookupName(scope);
            }
            else symbol = scope.empty() ? &root : root.lookupName(scope);
            if (symbol && mode == "--lookup") collector.add(record(sources, *symbol));
            else if (symbol) {
                const Scope* members = nullptr;
                if (symbol->kind == SymbolKind::Instance) members = &symbol->as<InstanceSymbol>().body;
                else if (symbol->isScope()) members = &symbol->as<Scope>();
                std::unordered_set<std::string> seen;
                if (members) for (const auto& member : members->members()) {
                    if (!member.name.empty() && member.name.substr(0, prefix.size()) == prefix &&
                        seen.insert(identity(member)).second)
                        collector.add(editor ? hex(member.name) + "\t" + hex(toString(member.kind)) : record(sources, member));
                }
            }
        }
        if (editor) {
            std::ostringstream output;
            output << "insight-slang-completion-v1\n";
            for (const auto& row : collector.rows) output << row << '\n';
            std::cout << output.str();
            return 0;
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
