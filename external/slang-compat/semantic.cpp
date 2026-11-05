// SPDX-License-Identifier: GPL-2.0-or-later
// Read-only, process-isolated native AST service. stdout is one protocol record.
#include "slang/ast/ASTVisitor.h"
#include "slang/ast/Statements.h"
#include "slang/ast/TimingControl.h"
#include "slang/ast/Compilation.h"
#include "slang/diagnostics/DiagnosticClient.h"
#include "slang/ast/Definition.h"
#include "slang/ast/expressions/AssignmentExpressions.h"
#include "slang/ast/expressions/MiscExpressions.h"
#include "slang/ast/expressions/OperatorExpressions.h"
#include "slang/ast/expressions/CallExpression.h"
#include "slang/ast/expressions/SelectExpressions.h"
#include "slang/ast/expressions/LiteralExpressions.h"
#include "slang/ast/symbols/SubroutineSymbols.h"
#include "slang/ast/symbols/CompilationUnitSymbols.h"
#include "slang/ast/symbols/InstanceSymbols.h"
#include "slang/ast/symbols/MemberSymbols.h"
#include "slang/ast/symbols/BlockSymbols.h"
#include "slang/ast/symbols/PortSymbols.h"
#include "slang/syntax/SyntaxTree.h"
#include "slang/parsing/Preprocessor.h"
#include "slang/util/Bag.h"
#include <cstdlib>
#include <cstdio>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
#include <memory>
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
void loadVivadoSettings(SourceManager& sources, Bag& options, std::string& top) {
#ifdef _WIN32
    const wchar_t* path = _wgetenv(L"GEANY_INSIGHT_VIVADO_CONFIG");
    if (!path || !*path)
        return;
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(_wfopen(path, L"rb"), std::fclose);
#else
    const char* path = std::getenv("GEANY_INSIGHT_VIVADO_CONFIG");
    if (!path || !*path)
        return;
    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::fopen(path, "rb"), std::fclose);
#endif
    std::string contents(65537, '\0');
    if (!file)
        throw std::runtime_error("cannot open Vivado settings");
    auto size = std::fread(&contents[0], 1, contents.size(), file.get());
    if (std::ferror(file.get()) || size > 65536)
        throw std::runtime_error("cannot read Vivado settings (limit: 64 KiB)");
    contents.resize(size);
    std::istringstream input(contents);
    auto readLine = [&input](std::string& line) {
        if (!std::getline(input, line))
            return false;
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        return true;
    };
    std::string line;
    if (!readLine(line) || line != "geany-insight-vivado-v1")
        throw std::runtime_error("invalid Vivado settings header");
    parsing::PreprocessorOptions preprocessing;
    while (readLine(line)) {
        auto separator = line.find('\t');
        if (separator == std::string::npos || separator + 1 == line.size() ||
            line.find('\0') != std::string::npos)
            throw std::runtime_error("invalid Vivado settings entry");
        auto kind = line.substr(0, separator);
        auto value = line.substr(separator + 1);
        if (kind == "include") {
            if (!ghc::filesystem::path(value).is_absolute())
                throw std::runtime_error("Vivado include directory must be absolute");
            sources.addUserDirectory(value);
        }
        else if (kind == "define")
            preprocessing.predefines.push_back(value);
        else if (kind == "top" && top.empty())
            top = value;
        else
            throw std::runtime_error("unknown or repeated Vivado settings entry");
    }
    if (input.bad())
        throw std::runtime_error("cannot read Vivado settings");
    options.set(preprocessing);
    if (!top.empty()) {
        CompilationOptions compilation;
        compilation.topModules.insert(top);
        options.set(compilation);
    }
}

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
struct PortOperands : ASTVisitor<PortOperands, false, true> {
    struct Operand {
        const Symbol* symbol;
        SourceRange range;
        std::string selection;
    };
    std::vector<Operand> signals;
    std::unordered_map<const Symbol*, std::unordered_set<std::string>> seen;

    // Read an already resolved integer without evaluating a run-time expression.
    static std::optional<int32_t> knownInteger(const Expression& expression) {
        if (expression.kind == ExpressionKind::IntegerLiteral)
            return expression.as<IntegerLiteral>().getValue().as<int32_t>();
        if (expression.constant && expression.constant->isInteger())
            return expression.constant->integer().as<int32_t>();
        return {};
    }

    static std::string knownBound(const Expression& expression) {
        const auto value = knownInteger(expression);
        return value ? std::to_string(*value) : "?";
    }

    // Describe a bit or slice while retaining the signal's original hierarchy.
    // Описывает бит или срез, сохраняя исходное иерархическое имя сигнала.
    static std::string selectionOf(const Expression& expression) {
        if (expression.kind == ExpressionKind::Conversion) {
            const auto& conversion = expression.as<ConversionExpression>();
            if (conversion.isImplicit())
                return selectionOf(conversion.operand());
        }
        if (expression.kind == ExpressionKind::ElementSelect) {
            const auto& select = expression.as<ElementSelectExpression>();
            if (!selectionOf(select.value()).empty())
                return "[?]";
            return "[" + knownBound(select.selector()) + "]";
        }
        if (expression.kind == ExpressionKind::RangeSelect) {
            const auto& select = expression.as<RangeSelectExpression>();
            if (!selectionOf(select.value()).empty())
                return "[?]";
            const auto kind = select.getSelectionKind();
            const char* separator = kind == RangeSelectionKind::Simple ? ":" :
                kind == RangeSelectionKind::IndexedUp ? "+:" : "-:";
            return "[" + knownBound(select.left()) + separator + knownBound(select.right()) + "]";
        }
        return {};
    }

    // Keep each signal and selection once per connection.
    // Сохраняет каждый сигнал и срез один раз на подключение.
    void add(const Symbol* symbol, SourceRange range, std::string selection = {}) {
        if (symbol && symbol->kind == SymbolKind::ModportPort) {
            // A named modport member denotes the signal of the connected interface instance.
            if (const auto* internal = symbol->as<ModportPortSymbol>().internalSymbol)
                symbol = internal;
        }
        if (symbol && (symbol->kind == SymbolKind::Net || symbol->kind == SymbolKind::Variable ||
                       symbol->kind == SymbolKind::ModportPort) && range.start() && seen[symbol].insert(selection).second)
            signals.push_back({symbol, range, std::move(selection)});
    }

    // Record a selected value and visit index expressions as independent reads.
    // Записывает выбранный сигнал и обходит индексы как отдельные чтения.
    void handle(const ElementSelectExpression& node) {
        if (const auto* symbol = node.getSymbolReference()) {
            add(symbol, node.sourceRange, selectionOf(node));
            node.selector().visit(*this);
        }
        else {
            visitDefault(node);
        }
    }

    // Record a selected range and preserve reads of its bounds.
    // Записывает выбранный диапазон и сохраняет чтения его границ.
    void handle(const RangeSelectExpression& node) {
        if (const auto* symbol = node.getSymbolReference()) {
            add(symbol, node.sourceRange, selectionOf(node));
            node.left().visit(*this);
            node.right().visit(*this);
        }
        else {
            visitDefault(node);
        }
    }

    // Collect a local signal read in an input expression.
    // Собирает чтение локального сигнала во входном выражении.
    void handle(const NamedValueExpression& node) { add(&node.symbol, node.sourceRange); }

    // Collect a hierarchical signal read in an input expression.
    // Собирает чтение сигнала по иерархическому имени во входном выражении.
    void handle(const HierarchicalValueExpression& node) { add(&node.symbol, node.sourceRange); }

    // Ignore output arguments when collecting reads in a port expression.
    // Исключает выходные аргументы из чтений во входном выражении.
    void handle(const AssignmentExpression& node) {
        if (!node.isLValueArg())
            node.right().visit(*this);
    }

    // Follow output destinations without treating index expressions as targets.
    // Обходит получателей выхода и исключает выражения индексов из целей записи.
    void destinations(const Expression& node) {
        if (node.kind == ExpressionKind::Assignment) {
            const auto& assignment = node.as<AssignmentExpression>();
            if (assignment.isLValueArg())
                destinations(assignment.left());
        }
        else if (node.kind == ExpressionKind::Conversion &&
                 node.as<ConversionExpression>().isImplicit()) {
            destinations(node.as<ConversionExpression>().operand());
        }
        else if (node.kind == ExpressionKind::Concatenation) {
            for (const auto* operand : node.as<ConcatenationExpression>().operands())
                destinations(*operand);
        }
        else {
            add(node.getSymbolReference(), node.sourceRange, selectionOf(node));
        }
    }
};

struct ProjectIndex : ASTVisitor<ProjectIndex, true, true> {
    const SourceManager& sources;
    std::vector<std::string> rows;
    size_t bytes = 0;
    string_view valueRole = "value";
    bool inProceduralBlock = false;
    struct ControlOperands {
        PortOperands inputs;
        string_view sourceRole, destinationRole;
    };
    std::vector<ControlOperands> controls;
    explicit ProjectIndex(const SourceManager& sources) : sources(sources) {}
    bool located(SourceLocation location) const { return location && sources.getFullPath(location.buffer()).is_absolute(); }
    void push(std::string row) {
        if (rows.size() >= 100000 || bytes + row.size() + 1 > 64u * 1024u * 1024u - 128) throw std::runtime_error("HDL index reply exceeds its limits");
        bytes += row.size() + 1;rows.push_back(std::move(row));
    }
    void add(const std::string& type, string_view name, SourceLocation location, string_view scope, string_view detail, string_view relation) {
        if (!located(location) || name.empty()) return;
        std::string row = type + "\t" + hex(name) + "\t" + hex(sources.getFullPath(location.buffer()).string()) + "\t" +
            std::to_string(sources.getLineNumber(location)) + "\t" + std::to_string(sources.getColumnNumber(location)) + "\t" + hex(scope) + "\t" + hex(detail) + "\t" + hex(relation);
        push(std::move(row));
    }
    void reference(string_view name, SourceRange range, SourceLocation target, string_view kind) {
        if (!target) return;
        target = sources.getFullyOriginalLoc(target);
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
    void handle(const NamedValueExpression& node) { boundName(node, node.symbol, valueRole); visitDefault(node); }
    void handle(const HierarchicalValueExpression& node) { boundName(node, node.symbol, valueRole); visitDefault(node); }
    void handle(const MemberAccessExpression& node) { boundName(node, node.member, "member"); visitDefault(node); }

    // Classify direct assignment targets and reads in the right-hand expression.
    // Различает прямые цели присваивания и чтение в правой части.
    void handle(const AssignmentExpression& node) {
        if (inProceduralBlock)
            indexAssignment(node, "procedural-source", "procedural-destination");
        const auto previousRole = valueRole;
        const auto& target = node.left();
        const bool directTarget = target.kind == ExpressionKind::NamedValue ||
                                  target.kind == ExpressionKind::HierarchicalValue;
        valueRole = directTarget ? (node.isCompound() ? "read/write" : "write") : "value";
        target.visit(*this);
        valueRole = "read";
        node.right().visit(*this);
        valueRole = previousRole;
    }
    void handle(const CallExpression& node) {
        if (!node.isSystemCall() && node.syntax) {
            const syntax::ExpressionSyntax* callee = node.syntax;
            if (callee->kind == syntax::SyntaxKind::InvocationExpression) callee = callee->as<syntax::InvocationExpressionSyntax>().left;
            auto token = callee->getLastToken();const auto& function = *std::get<0>(node.subroutine);
            if (token.valueText() == function.name) reference(function.name, token.range(), function.location, "call");
        }visitDefault(node);
    }
    // Encode the endpoint selections relative to this row's owner and peer.
    // Записывает срезы относительно владельца строки и соседнего узла.
    static std::string endpointSelections(const std::string& owner, const std::string& peer) {
        if (owner.empty() && peer.empty())
            return {};
        return (owner.empty() ? "all" : owner) + " / " + (peer.empty() ? "all" : peer);
    }

    // Connect one group of dependencies to assignment targets.
    // Связывает одну группу зависимостей с целями присваивания.
    void indexDependencies(const PortOperands& inputs, const PortOperands& outputs,
                           string_view sourceRole, string_view destinationRole) {
        constexpr size_t maxPairs = 4096;
        if (outputs.signals.empty() || inputs.signals.size() > maxPairs / outputs.signals.size())
            return;

        for (const auto& input : inputs.signals) {
            const auto source = identity(*input.symbol);
            if (source.empty())
                continue;
            const auto read = sources.getFullyOriginalLoc(input.range.start());
            for (const auto& output : outputs.signals) {
                const auto destination = identity(*output.symbol);
                if (destination.empty() ||
                    (source == destination && (input.selection.empty() || output.selection.empty() ||
                                               input.selection == output.selection)))
                    continue;
                const auto write = sources.getFullyOriginalLoc(output.range.start());
                add("P", source, read, destination, endpointSelections(output.selection, input.selection), sourceRole);
                add("P", destination, write, source, endpointSelections(input.selection, output.selection), destinationRole);
            }
        }
    }

    struct MappedBit {
        const Symbol* symbol;
        SourceRange range;
        int64_t index;
    };

    // Flatten a wiring expression in MSB-to-LSB order. A null symbol is a constant bit.
    // Leave resizing, dynamic selects and computed expressions to the general index.
    static bool mapBits(const Expression& node, std::vector<MappedBit>& bits) {
        if (!node.type->isSimpleBitVector() || node.type->getBitWidth() > 256)
            return false;
        if (node.kind == ExpressionKind::Conversion) {
            const auto& conversion = node.as<ConversionExpression>();
            return conversion.isImplicit() &&
                node.type->getBitWidth() == conversion.operand().type->getBitWidth() &&
                mapBits(conversion.operand(), bits);
        }
        if (node.kind == ExpressionKind::Concatenation) {
            for (const auto* operand : node.as<ConcatenationExpression>().operands()) {
                if (!mapBits(*operand, bits))
                    return false;
            }
            return true;
        }
        if (node.kind == ExpressionKind::Replication) {
            const auto& replication = node.as<ReplicationExpression>();
            const auto count = PortOperands::knownInteger(replication.count());
            if (!count || *count < 0 || *count > 256)
                return false;
            for (int32_t i = 0; i < *count; ++i) {
                if (!mapBits(replication.concat(), bits))
                    return false;
            }
            return true;
        }
        if (node.kind == ExpressionKind::IntegerLiteral ||
            node.kind == ExpressionKind::UnbasedUnsizedIntegerLiteral) {
            bits.insert(bits.end(), node.type->getBitWidth(), {nullptr, {}, 0});
            return true;
        }

        const Expression* base = &node;
        auto range = node.type->getFixedRange();
        int64_t left = range.left, right = range.right;
        if (node.kind == ExpressionKind::ElementSelect) {
            const auto& select = node.as<ElementSelectExpression>();
            const auto index = PortOperands::knownInteger(select.selector());
            if (!index)
                return false;
            base = &select.value();
            left = right = *index;
        }
        else if (node.kind == ExpressionKind::RangeSelect) {
            const auto& select = node.as<RangeSelectExpression>();
            const auto first = PortOperands::knownInteger(select.left());
            const auto last = PortOperands::knownInteger(select.right());
            if (!first || !last)
                return false;
            base = &select.value();
            left = *first;
            right = *last;
            if (select.getSelectionKind() != RangeSelectionKind::Simple) {
                if (*last <= 0)
                    return false;
                right = left;
                if (select.getSelectionKind() == RangeSelectionKind::IndexedUp)
                    left += int64_t(*last) - 1;
                else
                    right -= int64_t(*last) - 1;
                if (base->type->getFixedRange().left < base->type->getFixedRange().right)
                    std::swap(left, right);
            }
        }
        if ((base->kind != ExpressionKind::NamedValue &&
             base->kind != ExpressionKind::HierarchicalValue) || !base->type->isSimpleBitVector())
            return false;
        range = base->type->getFixedRange();
        if (left < std::min(range.left, range.right) || left > std::max(range.left, range.right) ||
            right < std::min(range.left, range.right) || right > std::max(range.left, range.right) ||
            (left > right ? left - right : right - left) + 1 != node.type->getBitWidth())
            return false;
        PortOperands leaf;
        leaf.add(base->getSymbolReference(), node.sourceRange);
        if (leaf.signals.empty())
            return false;
        const auto& operand = leaf.signals.front();
        const int64_t step = left < right ? 1 : -1;
        for (int64_t bit = left; bit != right + step; bit += step)
            bits.push_back({operand.symbol, operand.range, bit});
        return true;
    }

    static bool isWiringPermutation(const Expression& node) {
        if (node.kind == ExpressionKind::Conversion)
            return isWiringPermutation(node.as<ConversionExpression>().operand());
        return node.kind == ExpressionKind::Concatenation || node.kind == ExpressionKind::Replication;
    }

    bool indexBitMapping(const AssignmentExpression& assignment, string_view sourceRole,
                         string_view destinationRole) {
        if (assignment.isCompound() ||
            (!isWiringPermutation(assignment.left()) && !isWiringPermutation(assignment.right())))
            return false;
        std::vector<MappedBit> input, output;
        if (!mapBits(assignment.left(), output) || !mapBits(assignment.right(), input) ||
            input.size() != output.size())
            return false;

        PortOperands from, to;
        for (size_t i = 0; i < input.size(); ++i) {
            if (!input[i].symbol || !output[i].symbol)
                continue;
            from.signals.assign(1, {input[i].symbol, input[i].range,
                                    "[" + std::to_string(input[i].index) + "]"});
            to.signals.assign(1, {output[i].symbol, output[i].range,
                                  "[" + std::to_string(output[i].index) + "]"});
            indexDependencies(from, to, sourceRole, destinationRole);
        }
        return true;
    }

    // Index data flow and the controls active at this assignment separately.
    // Индексирует поток данных и активные управляющие сигналы по отдельности.
    void indexAssignment(const AssignmentExpression& assignment, string_view sourceRole,
                         string_view destinationRole) {
        if (assignment.bad() || assignment.isLValueArg())
            return;

        PortOperands inputs, outputs;
        assignment.right().visit(inputs);
        outputs.destinations(assignment.left());
        if (!indexBitMapping(assignment, sourceRole, destinationRole))
            indexDependencies(inputs, outputs, sourceRole, destinationRole);
        for (const auto& control : controls)
            indexDependencies(control.inputs, outputs, control.sourceRole, control.destinationRole);
    }

    // Apply each if condition only to assignments in its own branches.
    // Применяет условие if только к присваиваниям в его ветвях.
    void handle(const ConditionalStatement& node) {
        node.visitExprs(*this);
        const auto depth = controls.size();
        ControlOperands condition{{}, "condition-source", "condition-destination"};
        for (const auto& term : node.conditions)
            term.expr->visit(condition.inputs);
        controls.push_back(std::move(condition));
        node.visitStmts(*this);
        controls.resize(depth);
    }

    // Earlier case items can also affect which branch executes.
    void handle(const CaseStatement& node) {
        node.visitExprs(*this);
        const auto depth = controls.size();
        controls.push_back({{}, "condition-source", "condition-destination"});
        node.expr.visit(controls[depth].inputs);
        for (const auto& item : node.items) {
            for (const auto* expression : item.expressions)
                expression->visit(controls[depth].inputs);
            item.stmt->visit(*this);
        }
        if (node.defaultCase)
            node.defaultCase->visit(*this);
        controls.resize(depth);
    }

    // Preserve the edge polarity of explicit event controls and their iff guards.
    // Сохраняет полярность явных событий и их условия iff.
    void collectEventControls(const TimingControl& timing) {
        if (timing.kind == TimingControlKind::EventList) {
            for (const auto* event : timing.as<EventListControl>().events)
                collectEventControls(*event);
            return;
        }
        if (timing.kind != TimingControlKind::SignalEvent)
            return;
        const auto& event = timing.as<SignalEventControl>();
        ControlOperands input{{}, "event-source", "event-destination"};
        if (event.edge == EdgeKind::PosEdge) {
            input.sourceRole = "posedge-source";
            input.destinationRole = "posedge-destination";
        }
        else if (event.edge == EdgeKind::NegEdge) {
            input.sourceRole = "negedge-source";
            input.destinationRole = "negedge-destination";
        }
        else if (event.edge == EdgeKind::BothEdges) {
            input.sourceRole = "edge-source";
            input.destinationRole = "edge-destination";
        }
        event.expr.visit(input.inputs);
        controls.push_back(std::move(input));
        if (event.iffCondition) {
            ControlOperands guard{{}, "condition-source", "condition-destination"};
            event.iffCondition->visit(guard.inputs);
            controls.push_back(std::move(guard));
        }
    }

    // Restrict an event dependency to the statement controlled by that event.
    // Ограничивает зависимость от события управляемым им оператором.
    void handle(const TimedStatement& node) {
        node.visitExprs(*this);
        const auto depth = controls.size();
        collectEventControls(node.timing);
        node.visitStmts(*this);
        controls.resize(depth);
    }

    // Index continuous assignments once, then retain ordinary source references.
    // Индексирует непрерывное присваивание один раз и сохраняет обычные ссылки.
    void handle(const ContinuousAssignSymbol& node) {
        const auto& expression = node.getAssignment();
        if (expression.kind == ExpressionKind::Assignment)
            indexAssignment(expression.as<AssignmentExpression>(), "assign-source", "assign-destination");
        visitDefault(node);
    }

    // Limit procedural dependency edges to the body of an HDL process.
    // Ограничивает связи процедурных присваиваний телом процесса HDL.
    void handle(const ProceduralBlockSymbol& node) {
        const bool previous = inProceduralBlock;
        inProceduralBlock = true;
        visitDefault(node);
        inProceduralBlock = previous;
    }

    // An explicit modport expression has its own width and coordinates.
    // Keep a directional edge so that a slice on the alias stays distinct.
    void handle(const ModportPortSymbol& node) {
        add("D", node.name, node.location, identity(node), "ModportPort", "");
        const auto* expression = node.explicitConnection;
        if (expression && !expression->bad()) {
            PortOperands alias;
            alias.add(&node, SourceRange(node.location, node.location));
            if (node.direction == ArgumentDirection::In ||
                node.direction == ArgumentDirection::InOut) {
                PortOperands inputs;
                expression->visit(inputs);
                indexDependencies(inputs, alias, "interface-source", "interface-destination");
            }
            if (node.direction == ArgumentDirection::Out ||
                node.direction == ArgumentDirection::InOut ||
                node.direction == ArgumentDirection::Ref) {
                PortOperands outputs;
                outputs.destinations(*expression);
                indexDependencies(alias, outputs, "interface-source", "interface-destination");
                if (node.direction == ArgumentDirection::Ref)
                    indexDependencies(outputs, alias, "interface-source", "interface-destination");
            }
        }
        visitDefault(node);
    }

    // Index each direct port connection from both sides of the instance boundary.
    // Индексирует прямое подключение порта с обеих сторон границы экземпляра.
    void indexPortConnections(const InstanceSymbol& instance) {
        for (const auto* connection : instance.getPortConnections()) {
            if (connection->port.kind != SymbolKind::Port)
                continue;

            const auto& port = connection->port.as<PortSymbol>();
            if (!port.internalSymbol)
                continue;

            const auto* expression = connection->getExpression();
            if (!expression || expression->bad() ||
                (port.hasInitializer() && expression == port.getInitializer()))
                continue;

            PortOperands operands;
            if (port.direction == ArgumentDirection::In) {
                expression->visit(operands);
                // Implicit named connections may have no syntax to visit.
                if (operands.signals.empty())
                    operands.add(expression->getSymbolReference(), expression->sourceRange,
                             PortOperands::selectionOf(*expression));
            }
            else if (port.direction == ArgumentDirection::Out) {
                operands.destinations(*expression);
            }
            else {
                operands.add(expression->getSymbolReference(), expression->sourceRange,
                             PortOperands::selectionOf(*expression));
            }

            const auto internalRole = port.direction == ArgumentDirection::In ? "port-source" :
                port.direction == ArgumentDirection::Out ? "port-destination" : "port-peer";
            const auto externalRole = port.direction == ArgumentDirection::In ? "port-destination" :
                port.direction == ArgumentDirection::Out ? "port-source" : "port-peer";
            const std::string portName = std::string(instance.name) + "." + std::string(port.name);
            const auto inside = identity(instance) + "." + std::string(port.internalSymbol->name);
            for (const auto& [external, range, selection] : operands.signals) {
                reference(portName, range, external->location, externalRole);
                reference(external->name, range, port.internalSymbol->location, internalRole);

                const auto location = sources.getFullyOriginalLoc(range.start());
                const auto outside = identity(*external);
                if (!outside.empty() && !instance.name.empty() && !port.internalSymbol->name.empty()) {
                    add("P", inside, location, outside, endpointSelections(selection, {}), externalRole);
                    add("P", outside, location, inside, endpointSelections({}, selection), internalRole);
                }
            }
        }
    }

    void handle(const InstanceSymbol& node) {
        indexPortConnections(node);
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
struct IndexDiagnostics : DiagnosticClient {
    ProjectIndex& index;
    std::string fallback;
    IndexDiagnostics(ProjectIndex& index, const std::string& path) : index(index), fallback(ghc::filesystem::weakly_canonical(path).string()) {}
    void report(const ReportedDiagnostic& diagnostic) override {
        if (diagnostic.severity == DiagnosticSeverity::Ignored) return;
        std::string message(diagnostic.formattedMessage);
        for (char& c : message) if (c == '\r' || c == '\n' || c == '\t') c = ' ';
        if (message.size() > 16000) {size_t length = 16000;while (length && (static_cast<unsigned char>(message[length]) & 0xc0) == 0x80) --length;message.resize(length);message += "…";}
        auto location = sourceManager->getFullyOriginalLoc(diagnostic.location);
        bool located = index.located(location);
        index.push("G\t" + hex(getSeverityString(diagnostic.severity)) + "\t" + hex(located ? sourceManager->getFullPath(location.buffer()).string() : fallback) + "\t" +
            std::to_string(located ? sourceManager->getLineNumber(location) : 0) + "\t" + std::to_string(located ? sourceManager->getColumnNumber(location) : 0) + "\t" +
            hex(toString(diagnostic.originalDiagnostic.code)) + "\t" + hex(message) + "\t" + hex(located ? "located" : "global"));
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
#ifdef _WIN32
        // Keep the editor protocol byte-for-byte identical to Unix stdout.
        if (_setmode(_fileno(stdout), _O_BINARY) == -1)
            throw std::runtime_error("cannot set binary stdout");
#endif
        if (argc < 4) throw std::runtime_error("usage: insight-slang-semantic --symbols|--references|--lookup NAME|--complete SCOPE PREFIX -- FILE...");
        std::string mode = argv[1], scope, prefix, sourcePath;
        size_t sourceLine = 0;
        bool editor = mode == "--editor-complete";
        bool definition = mode == "--editor-definition";
        bool waveDefinition = mode == "--editor-wave-definition";
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
        else if (waveDefinition || mode == "--lookup" || mode == "--complete") {
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
        std::string vivadoTop;
        Bag options;
        loadVivadoSettings(sources, options, vivadoTop);
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
        Compilation compilation(options);
        std::unordered_set<std::string> units;
        for (int i = first; i < argc; ++i) {
            if (hasUnit && header(argv[i])) continue;
            auto name = ghc::filesystem::weakly_canonical(argv[i]).string();
            if (units.insert(name).second) compilation.addSyntaxTree(syntax::SyntaxTree::fromFile(name, sources, options));
        }
        const auto& root = compilation.getRoot();
        const auto diagnostics = compilation.getAllDiagnostics().size();
        if (mode == "--editor-index") {
            ProjectIndex index(sources);root.visit(index);
            DiagnosticEngine engine(sources);engine.addClient(std::make_shared<IndexDiagnostics>(index,argv[first]));
            for (const auto& diagnostic : compilation.getAllDiagnostics()) engine.issue(diagnostic);
            std::ostringstream output;output << "insight-slang-index-v2\n" << diagnostics << '\n';
            for (const auto& row : index.rows) output << row << '\n';
            std::cout << output.str();return 0;
        }
        if (waveDefinition) {
            std::cout << "insight-slang-definition-v1\n";
            if (const auto* symbol = root.lookupName(scope)) {
                if (symbol->location) {
                    auto location = sources.getFullyOriginalLoc(symbol->location);
                    std::cout << hex(sources.getFullPath(location.buffer()).string()) << '\t'
                              << sources.getLineNumber(location) << '\t' << sources.getColumnNumber(location) << '\n';
                }
            }
            return 0;
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
