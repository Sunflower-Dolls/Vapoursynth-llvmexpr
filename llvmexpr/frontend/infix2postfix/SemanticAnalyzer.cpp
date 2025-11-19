/**
 * Copyright (C) 2025 yuygfgg
 * 
 * This file is part of Vapoursynth-llvmexpr.
 * 
 * Vapoursynth-llvmexpr is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * Vapoursynth-llvmexpr is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with Vapoursynth-llvmexpr.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "SemanticAnalyzer.hpp"
#include "Builtins.hpp"
#include "OverloadResolution.hpp"
#include "llvmexpr/utils/EnumName.hpp"
#include <algorithm>
#include <cstddef>
#include <format>
#include <functional>
#include <string>

namespace infix2postfix {

SemanticAnalyzer::SemanticAnalyzer(Mode mode, int num_inputs,
                                   int library_line_count)
    : mode(mode), num_inputs(num_inputs),
      library_line_count(library_line_count),
      global_scope(std::make_unique<SymbolTable>()),
      current_scope(global_scope.get()) {}

bool SemanticAnalyzer::analyze(const Program* program) {
    if (program == nullptr) {
        reportError("Program AST is null", Range{});
        return false;
    }

    current_pending_gotos.clear();
    current_labels_seen.clear();

    // Collect global labels
    for (const auto& stmt : program->statements) {
        if (auto* label_def = get_if<LabelStmt>(stmt.get())) {
            if (global_labels.contains(label_def->name.value)) {
                reportError(std::format("Duplicate label '{}' in global scope",
                                        label_def->name.value),
                            label_def->range);
            }
            global_labels.insert(label_def->name.value);
        }
    }

    // Collect function signatures and build function symbols
    for (const auto& stmt : program->statements) {
        if (auto* func_def = get_if<FunctionDef>(stmt.get())) {
            if (function_signatures.contains(func_def->name.value)) {
                for (const auto& existing_sig :
                     function_signatures.at(func_def->name.value)) {
                    if (existing_sig.params.size() == func_def->params.size()) {
                        bool same = true;
                        for (size_t i = 0; i < func_def->params.size(); ++i) {
                            if (existing_sig.params[i].type !=
                                func_def->params[i].type) {
                                same = false;
                                break;
                            }
                        }
                        if (same) {
                            reportError(
                                std::format(
                                    "Duplicate function signature for '{}'",
                                    func_def->name.value),
                                func_def->range);
                        }
                    }
                }
            }

            FunctionSignature sig;
            sig.name = func_def->name.value;
            for (const auto& p : func_def->params) {
                sig.params.push_back({p.name.value, p.type});
            }
            sig.range = func_def->range;

            sig.has_return = false;
            std::optional<bool> returns_value_opt;

            std::function<void(Stmt*)> find_returns = [&](Stmt* s) {
                if (!s) {
                    return;
                }

                if (auto* ret = get_if<ReturnStmt>(s)) {
                    sig.has_return = true;
                    bool current_has_value = (ret->value != nullptr);
                    if (!returns_value_opt.has_value()) {
                        returns_value_opt = current_has_value;
                    } else if (returns_value_opt.value() != current_has_value) {
                        reportError(
                            std::format(
                                "Function '{}' has inconsistent return "
                                "statements (some with values, some without).",
                                func_def->name.value),
                            ret->range);
                    }
                } else if (auto* block = get_if<BlockStmt>(s)) {
                    for (const auto& inner_s : block->statements) {
                        find_returns(inner_s.get());
                    }
                } else if (auto* if_s = get_if<IfStmt>(s)) {
                    find_returns(if_s->then_branch.get());
                    if (if_s->else_branch) {
                        find_returns(if_s->else_branch.get());
                    }
                } else if (auto* while_s = get_if<WhileStmt>(s)) {
                    find_returns(while_s->body.get());
                }
            };
            for (const auto& s : func_def->body->statements) {
                find_returns(s.get());
            }
            sig.returns_value = returns_value_opt.value_or(false);

            if (func_def->global_decl) {
                sig.global_mode = func_def->global_decl->mode;
                for (const auto& g : func_def->global_decl->globals) {
                    sig.specific_globals.insert(g.value);
                }
            }

            std::set<std::string> local_vars;
            for (const auto& p : func_def->params) {
                local_vars.insert(p.name.value);
            }

            std::function<void(Stmt*)> collect_local_defs = [&](Stmt* s) {
                if (!s) {
                    return;
                }
                if (auto* assign = get_if<AssignStmt>(s)) {
                    local_vars.insert(assign->name.value);
                } else if (auto* block = get_if<BlockStmt>(s)) {
                    for (const auto& stmt : block->statements) {
                        collect_local_defs(stmt.get());
                    }
                } else if (auto* if_stmt = get_if<IfStmt>(s)) {
                    collect_local_defs(if_stmt->then_branch.get());
                    if (if_stmt->else_branch) {
                        collect_local_defs(if_stmt->else_branch.get());
                    }
                } else if (auto* while_stmt = get_if<WhileStmt>(s)) {
                    collect_local_defs(while_stmt->body.get());
                }
            };
            for (const auto& s : func_def->body->statements) {
                collect_local_defs(s.get());
            }

            for (const auto& s : func_def->body->statements) {
                collectUsedGlobalsInStmt(s.get(), sig.used_globals);
            }

            for (const auto& local_var : local_vars) {
                sig.used_globals.erase(local_var);
            }

            function_signatures[sig.name].push_back(sig);
            function_defs[sig.name].push_back(func_def);

            auto func_symbol = defineSymbol(SymbolKind::FUNCTION, sig.name,
                                            Type::Value, sig.range);
            func_symbol->signature = &function_signatures[sig.name].back();
            func_def->symbol = func_symbol;
        }
    }

    // Analyze all statements
    for (const auto& stmt : program->statements) {
        analyzeStmt(stmt.get());

        validateGlobalDependencies(stmt.get());

        if (auto* assign = get_if<AssignStmt>(stmt.get())) {
            if (current_scope == global_scope.get()) {
                defined_global_vars.insert(assign->name.value);
            }
        }
    }

    // Check for recursion in function call graph
    std::set<std::string> visited;
    for (const auto& [func_name, _] : function_call_graph) {
        std::set<std::string> visiting;
        std::vector<std::string> cycle_path;

        if (detectCycleInCallGraph(func_name, visiting, visited, cycle_path)) {

            std::string cycle_str;
            for (size_t i = 0; i < cycle_path.size(); ++i) {
                cycle_str += cycle_path[i];
                if (i < cycle_path.size() - 1) {
                    cycle_str += " -> ";
                }
            }
            cycle_str += " -> " + cycle_path[0];

            Range first_range;
            if (function_signatures.contains(cycle_path[0])) {
                first_range = function_signatures.at(cycle_path[0])[0].range;
            }

            reportError(std::format("Recursion is not allowed: {}", cycle_str),
                        first_range);

            for (const auto& fn : cycle_path) {
                if (function_signatures.contains(fn)) {
                    const auto& sig = function_signatures.at(fn)[0];
                    reportError(
                        std::format(
                            "  Function '{}' is part of the recursion cycle",
                            fn),
                        sig.range);
                }
            }

            break;
        }
    }

    // Check for unused global variables and functions
    for (const auto& [name, symbol] : global_scope->get_symbols()) {
        if (!symbol->is_used) {
            if (symbol->name == "RESULT" || symbol->name == "_") {
                continue;
            }

            // Skip warnings for symbols defined in standard library code
            if (symbol->definition_range.start.line <= library_line_count) {
                continue;
            }

            reportWarning(std::format("Unused symbol '{}'", name),
                          symbol->definition_range);
        }
    }

    // Check if RESULT is defined in Expr mode
    if (mode == Mode::Expr && !has_result) {
        reportError("Final result must be assigned to variable 'RESULT'!",
                    Range{});
    }

    if (mode == Mode::Expr && !result_defined_in_global_scope) {
        reportError(
            "'RESULT' must be defined in the global scope in Expr mode.",
            Range{});
    }

    return !hasErrors();
}

bool SemanticAnalyzer::hasErrors() const {
    return std::ranges::any_of(diagnostics, [](const auto& diag) {
        return diag.severity == DiagnosticSeverity::ERROR;
    });
}

void SemanticAnalyzer::reportError(const std::string& message,
                                   const Range& range) {
    diagnostics.emplace_back(DiagnosticSeverity::ERROR, message, range);
}

void SemanticAnalyzer::reportWarning(const std::string& message,
                                     const Range& range) {
    diagnostics.emplace_back(DiagnosticSeverity::WARNING, message, range);
}

bool SemanticAnalyzer::detectCycleInCallGraph(
    const std::string& func_name, std::set<std::string>& visiting,
    std::set<std::string>& visited, std::vector<std::string>& cycle_path) {

    if (visited.contains(func_name)) {
        return false;
    }

    if (visiting.contains(func_name)) {
        cycle_path.push_back(func_name);
        return true;
    }

    visiting.insert(func_name);
    cycle_path.push_back(func_name);

    if (function_call_graph.contains(func_name)) {
        for (const auto& callee : function_call_graph.at(func_name)) {
            if (detectCycleInCallGraph(callee, visiting, visited, cycle_path)) {
                if (cycle_path.front() == func_name) {
                    return true;
                }
                return true;
            }
        }
    }

    visiting.erase(func_name);
    cycle_path.pop_back();
    visited.insert(func_name);
    return false;
}

// Symbol table management
void SemanticAnalyzer::enterScope() {
    auto new_scope = std::make_unique<SymbolTable>(current_scope);
    current_scope = new_scope.get();
    scope_stack.push_back(std::move(new_scope));
}

void SemanticAnalyzer::exitScope() {
    if (!scope_stack.empty()) {
        current_scope = current_scope->get_parent();
        scope_stack.pop_back();
    }
}

std::shared_ptr<Symbol> SemanticAnalyzer::defineSymbol(SymbolKind kind,
                                                       const std::string& name,
                                                       Type type,
                                                       const Range& range) {
    auto symbol = std::make_shared<Symbol>();
    symbol->kind = kind;
    symbol->name = name;
    symbol->type = type;
    symbol->definition_range = range;

    if (!current_scope->define(symbol)) {
        return current_scope->resolve(name);
    }

    return symbol;
}

std::shared_ptr<Symbol> SemanticAnalyzer::resolveSymbol(const std::string& name,
                                                        const Range& range) {
    if (name == "_") {
        reportError("The special variable '_' cannot be used in an expression. "
                    "It is only for discarding values.",
                    range);
    }
    auto symbol = current_scope->resolve(name);
    if (!symbol) {
        reportError(
            std::format("Variable '{}' is used before being defined", name),
            range);
    } else {
        symbol->is_used = true;
    }
    return symbol;
}

// Expression analysis
Type SemanticAnalyzer::analyzeExpr(Expr* expr) {
    if (expr == nullptr) {
        return Type::Value;
    }
    return std::visit([this](auto& e) -> Type { return this->analyze(e); },
                      expr->value);
}

void SemanticAnalyzer::analyzeStmt(Stmt* stmt) {
    if (stmt == nullptr) {
        return;
    }
    std::visit([this](auto& s) { this->analyze(s); }, stmt->value);
}

Type SemanticAnalyzer::analyze([[maybe_unused]] const NumberExpr& expr) {
    return Type::Literal;
}

Type SemanticAnalyzer::analyze(VariableExpr& expr) {
    std::string name = expr.name.value;

    if (name == "_") {
        reportError("The special variable '_' cannot be used in an expression. "
                    "It is only for discarding values.",
                    expr.range);
    }

    if (name.starts_with("$")) {
        std::string base_name = name.substr(1);

        if (base_name == "X" || base_name == "Y") {
            if (mode == Mode::Single) {
                reportError(
                    "X and Y coordinates are only available in Expr mode. "
                    "SingleExpr processes the entire frame at once, not "
                    "pixel-by-pixel.",
                    expr.range);
            }
            return Type::Value;
        }
        if (get_clip_index(base_name) != -1) {
            if (get_clip_index(base_name) > num_inputs - 1) {
                reportError(
                    std::format("Clip index '{}' is out of range", base_name),
                    expr.range);
            }
            return Type::Clip;
        }
        if (base_name == "width" || base_name == "height") {
            reportWarning(std::format("The built-in constant '${}' is "
                                      "deprecated and will be removed "
                                      "in a future version.",
                                      base_name),
                          expr.range);
            return Type::Value;
        }
        if (base_name == "pi" || base_name == "N") {
            return Type::Value;
        }
        reportError(std::format("Invalid identifier '{}'.", base_name),
                    expr.range);
        return Type::Value;
    }

    auto symbol = current_scope->resolve(name);

    if (!symbol && (current_function != nullptr)) {
        if (current_function->global_mode == GlobalMode::ALL) {
            symbol = global_scope->resolve(name);
            // Global declaration is forward declared
            if (!symbol) {
                symbol = std::make_shared<Symbol>();
                symbol->kind = SymbolKind::VARIABLE;
                symbol->name = name;
                symbol->type = Type::Value;
                symbol->definition_range = expr.range;
            }
        } else if (current_function->global_mode == GlobalMode::SPECIFIC) {
            // Global declaration is forward declared
            if (current_function->specific_globals.contains(name)) {
                symbol = global_scope->resolve(name);
                if (!symbol) {
                    symbol = std::make_shared<Symbol>();
                    symbol->kind = SymbolKind::VARIABLE;
                    symbol->name = name;
                    symbol->type = Type::Value;
                    symbol->definition_range = expr.range;
                }
            }
        }
    }

    if (!symbol) {
        reportError(
            std::format("Variable '{}' is used before being defined", name),
            expr.range);
        return Type::Value;
    }

    symbol->is_used = true;
    expr.symbol = symbol;
    return symbol->type;
}

Type SemanticAnalyzer::analyze(UnaryExpr& expr) {
    if (expr.op.type == TokenType::Minus) {
        if (get_if<NumberExpr>(expr.right.get()) != nullptr) {
            return Type::Literal;
        }
    }

    auto right_type = analyzeExpr(expr.right.get());
    if (!isConvertible(right_type, Type::Value, mode)) {
        reportError(std::format("Cannot apply unary operator '{}' to type "
                                "'{}' which is not convertible to a value.",
                                token_type_to_string(expr.op.type),
                                enum_name(right_type)),
                    expr.range);
    }

    return Type::Value;
}

Type SemanticAnalyzer::analyze(BinaryExpr& expr) {
    auto left_type = analyzeExpr(expr.left.get());
    auto right_type = analyzeExpr(expr.right.get());

    if (!isConvertible(left_type, Type::Value, mode)) {
        reportError(std::format("Left operand of binary operator '{}' has type "
                                "'{}' which is not convertible to a value.",
                                token_type_to_string(expr.op.type),
                                enum_name(left_type)),
                    expr.range);
    }
    if (!isConvertible(right_type, Type::Value, mode)) {
        reportError(
            std::format("Right operand of binary operator '{}' has type "
                        "'{}' which is not convertible to a value.",
                        token_type_to_string(expr.op.type),
                        enum_name(right_type)),
            expr.range);
    }

    return Type::Value;
}

Type SemanticAnalyzer::analyze(TernaryExpr& expr) {
    auto cond_type = analyzeExpr(expr.cond.get());
    if (!isConvertible(cond_type, Type::Value, mode)) {
        reportError(std::format("Ternary condition has type '{}' which is not "
                                "convertible to a value.",
                                enum_name(cond_type)),
                    expr.range);
    }

    auto true_type = analyzeExpr(expr.true_expr.get());
    auto false_type = analyzeExpr(expr.false_expr.get());

    if (!isConvertible(true_type, Type::Value, mode) ||
        !isConvertible(false_type, Type::Value, mode)) {
        reportError("Both branches of a ternary expression must be convertible "
                    "to a value.",
                    expr.range);
    }

    return Type::Value;
}

Type SemanticAnalyzer::analyze(CallExpr& expr) {
    const auto* signature =
        resolveOverload(expr.callee, expr.args, expr.range, &expr);
    expr.resolved_signature = signature;

    if (signature != nullptr && !function_call_stack.empty()) {
        const std::string& caller = function_call_stack.back();
        function_call_graph[caller].insert(expr.callee);
    }

    if (expr.callee == "set_prop" || expr.callee == "set_propf" ||
        expr.callee == "set_propi" || expr.callee == "set_propaf" ||
        expr.callee == "set_propai" || expr.callee == "remove_prop") {

        if (expr.resolved_builtin == nullptr) {
            return Type::Void;
        }

        auto* prop_name_expr = get_if<VariableExpr>(expr.args[0].get());

        std::string prop_name = prop_name_expr->name.value;
        std::string suffix;
        std::string_view callee_view(expr.callee);

        if (callee_view == "remove_prop") {
            suffix = "d";
        } else {
            suffix = callee_view.substr(std::string_view("set_prop").length());
            if (suffix.empty()) {
                suffix = "f";
            }
        }

        auto it = written_properties.find(prop_name);
        if (it != written_properties.end()) {
            auto& [stored_suffix, stored_range] = it->second;
            if (stored_suffix != suffix) {
                if (stored_suffix == "d") {
                    stored_suffix = suffix;
                    stored_range = expr.range;
                } else if (suffix != "d") {
                    reportError(
                        std::format("Property '{}' is written with "
                                    "inconsistent types. First write was "
                                    "'{}', current write is '{}'.",
                                    prop_name, stored_suffix, suffix),
                        expr.range);
                }
            }
        } else {
            written_properties[prop_name] = {suffix, expr.range};
        }
    }

    if (expr.resolved_signature != nullptr) {
        return expr.resolved_signature->returns_value ? Type::Value
                                                      : Type::Void;
    }
    if (expr.resolved_builtin != nullptr) {
        return expr.resolved_builtin->returns_value ? Type::Value : Type::Void;
    }
    if (expr.callee.starts_with("nth_")) {
        return Type::Value;
    }

    return Type::Value;
}

const FunctionSignature* SemanticAnalyzer::resolveOverload(
    const std::string& name, const std::vector<std::unique_ptr<Expr>>& args,
    const Range& range, CallExpr* call_expr) {
    // User-defined functions
    if (function_signatures.contains(name)) {
        const auto& overloads = function_signatures.at(name);

        std::vector<Type> arg_types;
        arg_types.reserve(args.size());
        for (const auto& arg : args) {
            if (auto* var_expr = get_if<VariableExpr>(arg.get())) {
                auto symbol = current_scope->resolve(var_expr->name.value);
                if (symbol && symbol->type == Type::Array) {
                    arg_types.push_back(Type::Array);
                    continue;
                }
            }
            arg_types.push_back(analyzeExpr(arg.get()));
        }

        std::vector<const FunctionSignature*> possible_overloads;
        for (const auto& s : overloads) {
            if (s.params.size() == args.size()) {
                possible_overloads.push_back(&s);
            }
        }

        auto candidates = computeCandidates<const FunctionSignature*>(
            possible_overloads, args.size(),
            [&](const FunctionSignature* /*sig*/,
                size_t j) -> std::optional<Type> { return arg_types[j]; },
            [&](const FunctionSignature* sig, size_t j) -> std::optional<Type> {
                return sig->params[j].type;
            },
            mode);

        if (candidates.empty()) {
            std::string arg_types_str;
            for (size_t i = 0; i < arg_types.size(); ++i) {
                arg_types_str += std::string(enum_name(arg_types[i]));
                if (i < arg_types.size() - 1) {
                    arg_types_str += ", ";
                }
            }
            reportError(
                std::format(
                    "No matching user-defined function for call to '{}({})'",
                    name, arg_types_str),
                range);
            return nullptr;
        }

        // Select best candidate
        const auto* best_candidate = selectBestCandidate(candidates);

        if (isAmbiguous(candidates, best_candidate)) {
            reportError(
                std::format("Ambiguous call to overloaded function '{}'", name),
                range);
        }

        // Find and set the resolved FunctionDef
        if ((call_expr != nullptr) && (function_defs.contains(name))) {
            const auto& defs = function_defs.at(name);
            for (auto* def : defs) {
                if (def->params.size() ==
                    (*best_candidate->item)->params.size()) {
                    bool match = true;
                    for (size_t i = 0; i < def->params.size(); ++i) {
                        if (def->params[i].type !=
                            (*best_candidate->item)->params[i].type) {
                            match = false;
                            break;
                        }
                    }
                    if (match) {
                        call_expr->resolved_def = def;
                        if (def->symbol) {
                            def->symbol->is_used = true;
                        }
                        break;
                    }
                }
            }
        }

        return *best_candidate->item;
    }

    const auto& builtins = get_builtin_functions();

    if (builtins.contains(name)) {
        const auto& overloads = builtins.at(name);

        std::vector<const BuiltinFunction*> possible_overloads;
        for (const auto& o : overloads) {
            if (static_cast<size_t>(o.arity) == args.size() &&
                (!o.mode_restriction.has_value() ||
                 o.mode_restriction.value() == mode)) {
                possible_overloads.push_back(&o);
            }
        }

        std::vector<std::optional<Type>> arg_types(args.size());

        auto candidates = computeCandidates<const BuiltinFunction*>(
            possible_overloads, args.size(),
            [&](const BuiltinFunction* builtin,
                size_t j) -> std::optional<Type> {
                Type param_type = builtin->param_types[j];
                if (param_type == Type::Literal_string) {
                    auto* var_expr = get_if<VariableExpr>(args[j].get());
                    if (!var_expr || var_expr->name.value.starts_with("$")) {
                        return std::nullopt;
                    }
                    return Type::Literal_string;
                }

                if (!arg_types[j].has_value()) {
                    arg_types[j] = analyzeExpr(args[j].get());
                }
                return arg_types[j];
            },
            [&](const BuiltinFunction* builtin, size_t j)
                -> std::optional<Type> { return builtin->param_types[j]; },
            mode);

        if (candidates.empty()) {
            std::string arg_types_str;
            for (size_t i = 0; i < args.size(); ++i) {
                if (builtinParamTypeIsEvaluatable(overloads, i)) {
                    if (!arg_types[i].has_value()) {
                        arg_types[i] = analyzeExpr(args[i].get());
                    }
                    arg_types_str +=
                        std::string(enum_name(arg_types[i].value()));
                } else {
                    arg_types_str += "LiteralString";
                }

                if (i < args.size() - 1) {
                    arg_types_str += ", ";
                }
            }
            reportError(
                std::format(
                    "No matching overload for function '{}({})' in {} mode.",
                    name, arg_types_str,
                    mode == Mode::Expr ? "Expr" : "SingleExpr"),
                range);
            return nullptr;
        }

        // Select best candidate
        const auto* best_candidate = selectBestCandidate(candidates);

        if (isAmbiguous(candidates, best_candidate)) {
            reportError(
                std::format(
                    "Ambiguous call to overloaded built-in function '{}'",
                    name),
                range);
        }

        if (call_expr != nullptr) {
            call_expr->resolved_builtin = *best_candidate->item;
        }

        // Built-in functions don't have FunctionSignature, return nullptr
        return nullptr;
    }
    if (name.starts_with("nth_")) {
        std::string n_str = name.substr(4);
        if (n_str.empty() || !std::ranges::all_of(n_str, ::isdigit)) {
            reportError(std::format("Invalid nth_N function name '{}'", name),
                        range);
            return nullptr;
        }
        int n = std::stoi(n_str);
        int arg_count = static_cast<int>(args.size());
        if (arg_count < n) {
            reportError(std::format("Function '{}' requires at least {} "
                                    "arguments, but {} were provided.",
                                    name, n, arg_count),
                        range);
        }
        if (n < 1) {
            reportError(std::format("Invalid nth_N function name '{}'", name),
                        range);
        }
        for (const auto& arg : args) {
            auto arg_type = analyzeExpr(arg.get());
            if (!isConvertible(arg_type, Type::Value, mode)) {
                reportError(
                    std::format("Argument to function '{}' has type '{}' which "
                                "is not convertible to Value.",
                                name, enum_name(arg_type)),
                    arg->range());
            }
        }

        return nullptr;
    }
    reportError(std::format("Unknown function '{}'", name), range);
    return nullptr;
}

void SemanticAnalyzer::getAllSymbols(
    const SymbolTable* scope,
    std::map<std::string, std::shared_ptr<Symbol>>& symbols) const {
    if (scope == nullptr) {
        return;
    }
    for (const auto& [name, symbol] : scope->get_symbols()) {
        if (!symbols.contains(name)) {
            symbols[name] = symbol;
        }
    }
    getAllSymbols(scope->get_parent(), symbols);
}

void SemanticAnalyzer::validateClipReference(const std::string& clip_name,
                                             const Range& range) {
    std::string name = clip_name;

    if (name.starts_with("$")) {
        name = name.substr(1);
        if (get_clip_index(name) == -1) {
            reportError(std::format("Invalid clip identifier '{}'.", clip_name),
                        range);
        }
        if (get_clip_index(name) > num_inputs - 1) {
            reportError(std::format("Clip index '{}' is out of range", name),
                        range);
        }
    } else {
        // Check if it's a function parameter of Clip type
        auto symbol = current_scope->resolve(name);
        if (!symbol || symbol->type != Type::Clip) {
            reportError(
                std::format(
                    "Clip '{}' is not a clip constant or Clip parameter.",
                    clip_name),
                range);
        }
    }
}

Type SemanticAnalyzer::analyze(const PropAccessExpr& expr) {
    validateClipReference(expr.clip.value, expr.range);
    return Type::Value;
}

Type SemanticAnalyzer::analyze(const StaticRelPixelAccessExpr& expr) {
    if (mode == Mode::Single) {
        reportError("Static relative pixel access (clip[x,y]) is only "
                    "available in Expr mode. "
                    "Use dyn(clip, x, y, plane) for absolute access in "
                    "SingleExpr mode.",
                    expr.range);
    }

    validateClipReference(expr.clip.value, expr.range);
    return Type::Value;
}

Type SemanticAnalyzer::analyze(FrameDimensionExpr& expr) {
    reportWarning("The 'frame.width[N]' and 'frame.height[N]' constructs are "
                  "deprecated and will be removed in a future version.",
                  expr.range);

    if (mode == Mode::Expr) {
        reportError("frame.width[N] and frame.height[N] are only "
                    "available in SingleExpr mode. "
                    "Use width and height directly in Expr mode.",
                    expr.range);
    }

    auto plane_type = analyzeExpr(expr.plane_index_expr.get());
    if (plane_type != Type::Literal) {
        reportError("Plane index must be a literal constant.",
                    expr.plane_index_expr->range());
    }

    return Type::Value;
}

Type SemanticAnalyzer::analyze(ArrayAccessExpr& expr) {
    auto* var_expr = get_if<VariableExpr>(expr.array.get());
    if (var_expr == nullptr) {
        reportError("Array access requires a variable as the array.",
                    expr.range);
        return Type::Value;
    }

    std::string array_name = var_expr->name.value;
    auto symbol = resolveSymbol(array_name, expr.range);

    if (!symbol || symbol->type != Type::Array) {
        reportError(std::format("Variable '{}' is not an array.", array_name),
                    expr.range);
    }

    expr.array_symbol = symbol;

    auto index_type = analyzeExpr(expr.index.get());
    if (!isConvertible(index_type, Type::Value, mode)) {
        reportError("Array index must be convertible to a value.",
                    expr.index->range());
    }

    return Type::Value;
}

// Statement analysis
void SemanticAnalyzer::analyze(const ExprStmt& stmt) {
    Type expr_type = analyzeExpr(stmt.expr.get());
    if (expr_type != Type::Void) {
        reportError("Expression result is unused.", stmt.range);
    }
}

void SemanticAnalyzer::analyze(AssignStmt& stmt) {
    if (stmt.name.value == "RESULT") {
        has_result = true;
        if (mode == Mode::Expr) {
            if (current_function == nullptr && scope_stack.empty()) {
                result_defined_in_global_scope = true;
            }
        }
    }

    // Check if this is a new() or resize() call for array allocation
    if (auto* call_expr = get_if<CallExpr>(stmt.value.get())) {
        if (call_expr->callee == "new" || call_expr->callee == "resize") {
            if (call_expr->args.size() != 1) {
                reportError(
                    std::format("{}() requires exactly 1 argument (size).",
                                call_expr->callee),
                    stmt.range);
                return;
            }

            std::string var_name = stmt.name.value;
            auto existing_symbol = current_scope->resolve(var_name);
            bool is_already_array =
                (existing_symbol && existing_symbol->type == Type::Array);

            if (call_expr->callee == "resize") {
                if (mode == Mode::Expr) {
                    reportError(
                        "resize() is only available in SingleExpr mode.",
                        stmt.range);
                }
                if (!is_already_array) {
                    reportError(
                        std::format(
                            "Variable '{}' is undefined or not an array. ",
                            var_name),
                        stmt.range);
                }
            } else { // new()
                if (is_already_array) {
                    reportError(
                        std::format("Cannot reallocate array '{}'.", var_name),
                        stmt.range);
                }
            }

            if (mode == Mode::Expr) {
                auto size_type = analyzeExpr(call_expr->args[0].get());
                if (size_type != Type::Literal) {
                    reportError("Array size must be a literal constant.",
                                stmt.range);
                }
            } else {
                auto size_type = analyzeExpr(call_expr->args[0].get());
                if (!isConvertible(size_type, Type::Value, mode)) {
                    reportError("Array size must be convertible to a value.",
                                stmt.range);
                }
            }

            auto symbol = defineSymbol(SymbolKind::VARIABLE, var_name,
                                       Type::Array, stmt.range);
            stmt.symbol = symbol;

            return;
        }
    }
    auto value_type = analyzeExpr(stmt.value.get());
    if (!isConvertible(value_type, Type::Value, mode)) {
        reportError(std::format("Variable assignment value must be convertible "
                                "to a value, got {}.",
                                enum_name(value_type)),
                    stmt.range);
    }

    std::string var_name = stmt.name.value;
    auto existing_symbol = current_scope->resolve(var_name);

    if (existing_symbol && existing_symbol->type == Type::Array) {
        reportError(
            std::format(
                "Variable '{}' is an array and cannot be reassigned to a "
                "non-array value.",
                var_name),
            stmt.range);
    }

    auto symbol =
        defineSymbol(SymbolKind::VARIABLE, var_name, Type::Value, stmt.range);
    stmt.symbol = symbol;
}

void SemanticAnalyzer::analyze(ArrayAssignStmt& stmt) {
    auto* array_access = get_if<ArrayAccessExpr>(stmt.target.get());
    if (array_access == nullptr) {
        reportError("Array assignment target must be an array access.",
                    stmt.range);
        return;
    }

    analyzeExpr(stmt.target.get());

    auto value_type = analyzeExpr(stmt.value.get());
    if (!isConvertible(value_type, Type::Value, mode)) {
        reportError("Array assignment value must be convertible to a value.",
                    stmt.value->range());
    }
}

void SemanticAnalyzer::analyze(const BlockStmt& stmt) {
    ScopeGuard scope_guard(this);

    for (const auto& s : stmt.statements) {
        analyzeStmt(s.get());
    }
}

void SemanticAnalyzer::analyze(IfStmt& stmt) {
    auto cond_type = analyzeExpr(stmt.condition.get());
    if (!isConvertible(cond_type, Type::Value, mode)) {
        reportError(std::format("If condition has type '{}' which is not "
                                "convertible to a value.",
                                enum_name(cond_type)),
                    stmt.condition->range());
    }

    analyzeStmt(stmt.then_branch.get());

    if (stmt.else_branch) {
        analyzeStmt(stmt.else_branch.get());
    }
}

void SemanticAnalyzer::analyze(WhileStmt& stmt) {
    auto cond_type = analyzeExpr(stmt.condition.get());
    if (!isConvertible(cond_type, Type::Value, mode)) {
        reportError(std::format("While condition has type '{}' which is not "
                                "convertible to a value.",
                                enum_name(cond_type)),
                    stmt.condition->range());
    }

    analyzeStmt(stmt.body.get());
}

void SemanticAnalyzer::analyze(const ReturnStmt& stmt) {
    if (current_function == nullptr) {
        reportError("'return' statements are not allowed in the global scope.",
                    stmt.range);
    }
    if (stmt.value) {
        auto result_type = analyzeExpr(stmt.value.get());
        if (result_type == Type::Array) {
            reportError("Functions cannot return arrays.", stmt.range);
        }
    }
}

void SemanticAnalyzer::analyze(LabelStmt& stmt) {
    auto symbol = defineSymbol(SymbolKind::LABEL, stmt.name.value, Type::Value,
                               stmt.range);
    stmt.symbol = symbol;

    current_labels_seen.insert(stmt.name.value);

    if (current_pending_gotos.contains(stmt.name.value)) {
        symbol->is_used = true;

        std::map<std::string, std::shared_ptr<Symbol>> symbols_at_label;
        getAllSymbols(current_scope, symbols_at_label);

        const auto& pending_gotos = current_pending_gotos.at(stmt.name.value);
        for (const auto& goto_info : pending_gotos) {
            for (const auto& [name, symbol_at_label] : symbols_at_label) {

                if (symbol_at_label->kind == SymbolKind::VARIABLE &&
                    !goto_info.symbols_at_goto.contains(name)) {
                    reportError(
                        std::format(
                            "goto jumps over initialization of variable '{}'",
                            name),
                        goto_info.stmt->range);
                }
            }
        }
        current_pending_gotos.erase(stmt.name.value);
    }
}

void SemanticAnalyzer::analyze(GotoStmt& stmt) {
    if (current_function != nullptr) {
        // Inside a function
        if (global_labels.contains(stmt.label.value)) {
            reportError(std::format("goto from function '{}' to global "
                                    "label '{}' is not allowed",
                                    current_function->name, stmt.label.value),
                        stmt.range);
        }
        if (!current_function_labels.contains(stmt.label.value)) {
            reportError(
                std::format("goto target '{}' not found in function '{}'",
                            stmt.label.value, current_function->name),
                stmt.range);
        }
    } else {
        // Global scope
        if (!global_labels.contains(stmt.label.value)) {
            reportError(
                std::format("goto target '{}' not found in global scope",
                            stmt.label.value),
                stmt.range);
        }
    }

    if (!current_labels_seen.contains(stmt.label.value)) {
        // Forward jump
        ForwardGotoInfo info;
        info.stmt = &stmt;
        getAllSymbols(current_scope, info.symbols_at_goto);
        current_pending_gotos[stmt.label.value].push_back(info);
    }

    auto symbol = current_scope->resolve(stmt.label.value);
    if (symbol) {
        symbol->is_used = true;
    }
    stmt.target_label_symbol = symbol;

    if (stmt.condition) {
        analyzeExpr(stmt.condition.get());
    }
}

void SemanticAnalyzer::analyze(FunctionDef& stmt) {
    // Save global goto state and clear for function analysis
    auto saved_pending_gotos = std::move(current_pending_gotos);
    current_pending_gotos.clear();
    auto saved_labels_seen = std::move(current_labels_seen);
    current_labels_seen.clear();

    // Collect labels in function
    auto saved_current_function_labels = current_function_labels;
    current_function_labels.clear();
    for (const auto& s : stmt.body->statements) {
        collectLabels(s.get(), current_function_labels, stmt.name.value,
                      stmt.range);
    }

    // Find the function signature
    const FunctionSignature* sig = nullptr;
    if (function_signatures.contains(stmt.name.value)) {
        for (const auto& s : function_signatures.at(stmt.name.value)) {
            if (s.params.size() == stmt.params.size()) {
                bool match = true;
                for (size_t i = 0; i < s.params.size(); ++i) {
                    if (s.params[i].type != stmt.params[i].type) {
                        match = false;
                        break;
                    }
                }
                if (match) {
                    sig = &s;
                    break;
                }
            }
        }
    }

    if (sig == nullptr) {
        reportError(std::format("Could not find signature for function '{}'",
                                stmt.name.value),
                    stmt.range);
        current_function_labels = saved_current_function_labels;
        return;
    }

    if (sig->has_return && sig->returns_value) {
        bool body_always_returns = false;
        body_always_returns =
            std::ranges::any_of(stmt.body->statements, [this](const auto& s) {
                return path_always_returns(s.get());
            });
        if (!body_always_returns) {
            reportError(
                std::format(
                    "Not all control paths in function '{}' return a value.",
                    stmt.name.value),
                stmt.range);
        }
    }

    // Analyze function body with proper scope
    const auto* saved_current_function = current_function;
    current_function = sig;

    // Push function onto call stack to detect recursion
    function_call_stack.push_back(stmt.name.value);

    {
        ScopeGuard scope_guard(this);

        // Add parameters
        for (const auto& param : stmt.params) {
            defineSymbol(SymbolKind::PARAMETER, param.name.value, param.type,
                         stmt.range);
        }

        // Analyze function body
        analyze(*stmt.body);

        // Check for unused local variables and parameters
        for (const auto& [name, symbol] : current_scope->get_symbols()) {
            if (!symbol->is_used) {
                if (symbol->name == "_") {
                    continue;
                }
                // Skip warnings for symbols in standard library functions
                if (symbol->definition_range.start.line <= library_line_count) {
                    continue;
                }
                reportWarning(std::format("Unused symbol '{}'", name),
                              symbol->definition_range);
            }
        }
    }

    // Pop function from call stack
    function_call_stack.pop_back();

    // Restore context
    current_function = saved_current_function;
    current_function_labels = saved_current_function_labels;

    // Restore global goto state
    current_pending_gotos = std::move(saved_pending_gotos);
    current_labels_seen = std::move(saved_labels_seen);
}

void SemanticAnalyzer::analyze([[maybe_unused]] const GlobalDecl& stmt) {
    // Global declarations are handled by the parser
}

void SemanticAnalyzer::collectLabels(Stmt* stmt, std::set<std::string>& labels,
                                     const std::string& context,
                                     const Range& context_range) {
    if (stmt == nullptr) {
        return;
    }
    if (auto* label = get_if<LabelStmt>(stmt)) {
        if (labels.contains(label->name.value)) {
            reportError(std::format("Duplicate label '{}' in function '{}'",
                                    label->name.value, context),
                        label->range);
        }
        labels.insert(label->name.value);
    } else if (auto* block = get_if<BlockStmt>(stmt)) {
        for (auto& inner_s : block->statements) {
            collectLabels(inner_s.get(), labels, context, context_range);
        }
    } else if (auto* if_s = get_if<IfStmt>(stmt)) {
        collectLabels(if_s->then_branch.get(), labels, context, context_range);
        if (if_s->else_branch) {
            collectLabels(if_s->else_branch.get(), labels, context,
                          context_range);
        }
    } else if (auto* while_s = get_if<WhileStmt>(stmt)) {
        collectLabels(while_s->body.get(), labels, context, context_range);
    }
}

bool SemanticAnalyzer::path_always_returns(Stmt* stmt) {
    if (stmt == nullptr) {
        return false;
    }

    if (get_if<ReturnStmt>(stmt) != nullptr) {
        return true;
    }
    if (auto* if_s = get_if<IfStmt>(stmt)) {
        return path_always_returns(if_s->then_branch.get()) &&
               (if_s->else_branch &&
                path_always_returns(if_s->else_branch.get()));
    }
    if (auto* block = get_if<BlockStmt>(stmt)) {
        return std::ranges::any_of(block->statements, [this](const auto& s) {
            return path_always_returns(s.get());
        });
    }
    return false;
}

bool SemanticAnalyzer::builtinParamTypeIsEvaluatable(
    const std::vector<BuiltinFunction>& overloads, size_t param_idx) {
    return std::ranges::all_of(overloads, [param_idx](const auto& o) {
        return !(o.param_types.size() > param_idx &&
                 o.param_types[param_idx] == Type::Literal_string);
    });
}

void SemanticAnalyzer::validateGlobalDependencies(Stmt* stmt) {
    if (stmt == nullptr) {
        return;
    }

    std::function<void(Expr*)> check_expr = [&](Expr* expr) {
        if (!expr) {
            return;
        }
        if (auto* call = get_if<CallExpr>(expr)) {
            validateFunctionCall(*call);
        } else if (auto* binary = get_if<BinaryExpr>(expr)) {
            check_expr(binary->left.get());
            check_expr(binary->right.get());
        } else if (auto* unary = get_if<UnaryExpr>(expr)) {
            check_expr(unary->right.get());
        } else if (auto* ternary = get_if<TernaryExpr>(expr)) {
            check_expr(ternary->cond.get());
            check_expr(ternary->true_expr.get());
            check_expr(ternary->false_expr.get());
        }
    };

    if (auto* expr_stmt = get_if<ExprStmt>(stmt)) {
        check_expr(expr_stmt->expr.get());
    } else if (auto* assign_stmt = get_if<AssignStmt>(stmt)) {
        check_expr(assign_stmt->value.get());
    } else if (auto* if_stmt = get_if<IfStmt>(stmt)) {
        check_expr(if_stmt->condition.get());
        validateGlobalDependencies(if_stmt->then_branch.get());
        if (if_stmt->else_branch) {
            validateGlobalDependencies(if_stmt->else_branch.get());
        }
    } else if (auto* while_stmt = get_if<WhileStmt>(stmt)) {
        check_expr(while_stmt->condition.get());
        validateGlobalDependencies(while_stmt->body.get());
    } else if (auto* block = get_if<BlockStmt>(stmt)) {
        for (const auto& s : block->statements) {
            validateGlobalDependencies(s.get());
        }
    }
}

void SemanticAnalyzer::validateFunctionCall(const CallExpr& expr) {
    if (!function_signatures.contains(expr.callee)) {
        return;
    }

    const FunctionSignature* sig = expr.resolved_signature;
    if (sig == nullptr) {
        auto it =
            std::ranges::find_if(function_signatures.at(expr.callee),
                                 [&](const FunctionSignature& s) {
                                     return s.params.size() == expr.args.size();
                                 });
        if (it != function_signatures.at(expr.callee).end()) {
            sig = &*it;
        }
    }

    if (sig == nullptr) {
        return;
    }

    if (sig->global_mode == GlobalMode::ALL) {
        for (const auto& global_name : sig->used_globals) {
            if (global_name.starts_with("$")) {
                reportError(
                    std::format("Invalid global variable name {}", global_name),
                    expr.range);
            }

            if (!defined_global_vars.contains(global_name)) {
                reportError(
                    std::format(
                        "Function '{}' uses global variable '{}' which is "
                        "not defined in global scope before this call",
                        expr.callee, global_name),
                    expr.range);
            }
        }
    } else if (sig->global_mode == GlobalMode::SPECIFIC) {
        for (const auto& global_name : sig->specific_globals) {
            if (!defined_global_vars.contains(global_name)) {
                reportError(
                    std::format(
                        "Function '{}' requires global variable '{}' which is "
                        "not defined in global scope before this call",
                        expr.callee, global_name),
                    expr.range);
            }
        }
    }
}

void SemanticAnalyzer::collectUsedGlobals(Expr* expr,
                                          std::set<std::string>& used_globals) {
    if (expr == nullptr) {
        return;
    }

    if (auto* var_expr = get_if<VariableExpr>(expr)) {
        std::string name = var_expr->name.value;
        if (!name.starts_with("$") && name != "frame") {
            used_globals.insert(name);
        }
    } else if (auto* binary = get_if<BinaryExpr>(expr)) {
        collectUsedGlobals(binary->left.get(), used_globals);
        collectUsedGlobals(binary->right.get(), used_globals);
    } else if (auto* unary = get_if<UnaryExpr>(expr)) {
        collectUsedGlobals(unary->right.get(), used_globals);
    } else if (auto* ternary = get_if<TernaryExpr>(expr)) {
        collectUsedGlobals(ternary->cond.get(), used_globals);
        collectUsedGlobals(ternary->true_expr.get(), used_globals);
        collectUsedGlobals(ternary->false_expr.get(), used_globals);
    } else if (auto* call = get_if<CallExpr>(expr)) {
        for (const auto& arg : call->args) {
            collectUsedGlobals(arg.get(), used_globals);
        }
    } else if (auto* array_access = get_if<ArrayAccessExpr>(expr)) {
        collectUsedGlobals(array_access->array.get(), used_globals);
        collectUsedGlobals(array_access->index.get(), used_globals);
    } else if (auto* frame_dim = get_if<FrameDimensionExpr>(expr)) {
        collectUsedGlobals(frame_dim->plane_index_expr.get(), used_globals);
    }
}

void SemanticAnalyzer::collectUsedGlobalsInStmt(
    Stmt* stmt, std::set<std::string>& used_globals) {
    if (stmt == nullptr) {
        return;
    }

    if (auto* expr_stmt = get_if<ExprStmt>(stmt)) {
        collectUsedGlobals(expr_stmt->expr.get(), used_globals);
    } else if (auto* assign_stmt = get_if<AssignStmt>(stmt)) {
        collectUsedGlobals(assign_stmt->value.get(), used_globals);
    } else if (auto* array_assign = get_if<ArrayAssignStmt>(stmt)) {
        collectUsedGlobals(array_assign->target.get(), used_globals);
        collectUsedGlobals(array_assign->value.get(), used_globals);
    } else if (auto* if_stmt = get_if<IfStmt>(stmt)) {
        collectUsedGlobals(if_stmt->condition.get(), used_globals);
        collectUsedGlobalsInStmt(if_stmt->then_branch.get(), used_globals);
        if (if_stmt->else_branch) {
            collectUsedGlobalsInStmt(if_stmt->else_branch.get(), used_globals);
        }
    } else if (auto* while_stmt = get_if<WhileStmt>(stmt)) {
        collectUsedGlobals(while_stmt->condition.get(), used_globals);
        collectUsedGlobalsInStmt(while_stmt->body.get(), used_globals);
    } else if (auto* return_stmt = get_if<ReturnStmt>(stmt)) {
        if (return_stmt->value) {
            collectUsedGlobals(return_stmt->value.get(), used_globals);
        }
    } else if (auto* goto_stmt = get_if<GotoStmt>(stmt)) {
        if (goto_stmt->condition) {
            collectUsedGlobals(goto_stmt->condition.get(), used_globals);
        }
    } else if (auto* block = get_if<BlockStmt>(stmt)) {
        for (const auto& s : block->statements) {
            collectUsedGlobalsInStmt(s.get(), used_globals);
        }
    }
}

} // namespace infix2postfix
