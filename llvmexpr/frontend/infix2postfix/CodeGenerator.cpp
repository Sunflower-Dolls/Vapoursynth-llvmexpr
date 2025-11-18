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

#include "CodeGenerator.hpp"
#include "Builtins.hpp"
#include "PostfixBuilder.hpp"
#include "PostfixHelper.hpp"
#include "types.hpp"
#include <format>
#include <functional>
#include <utility>

namespace infix2postfix {

CodeGenerator::CodeGenerator(Mode mode, int num_inputs)
    : mode(mode), num_inputs(num_inputs) {}

std::string CodeGenerator::generate(const Program* program) {
    PostfixBuilder main_builder;
    for (const auto& stmt : program->statements) {
        generate(stmt.get(), main_builder);
    }

    if (mode == Mode::Expr) {
        main_builder.add_variable_load("RESULT");
    }

    return main_builder.get_expression();
}

CodeGenerator::ExprResult CodeGenerator::generate_expr(Expr* expr) {
    PostfixBuilder b;
    Type t = generate(expr, b);
    return {.postfix = b, .type = t};
}

std::string CodeGenerator::generate_expr_to_string(Expr* expr) {
    if (expr == nullptr) {
        return "";
    }
    PostfixBuilder temp_builder;
    generate(expr, temp_builder);
    return temp_builder.get_expression();
}

Type CodeGenerator::generate(Expr* expr, PostfixBuilder& builder) {
    if (expr == nullptr) {
        return Type::Value;
    }
    return std::visit(
        [this, &builder](auto& e) { return this->handle(e, builder); },
        expr->value);
}

void CodeGenerator::generate(Stmt* stmt, PostfixBuilder& builder) {
    if (stmt == nullptr) {
        return;
    }
    std::visit([this, &builder](auto& s) { this->handle(s, builder); },
               stmt->value);
}

Type CodeGenerator::handle(const NumberExpr& expr, PostfixBuilder& builder) {
    builder.add_number(expr.value.value);
    return Type::Literal;
}

Type CodeGenerator::handle(const VariableExpr& expr, PostfixBuilder& builder) {
    std::string name = expr.name.value;

    if (param_substitutions.contains(name)) {
        return generate(param_substitutions.at(name), builder);
    }

    if (name.starts_with("$")) {
        std::string base_name = name.substr(1);
        builder.add_constant(base_name);

        if (is_clip_name(base_name)) {
            return Type::Clip;
        }
        return Type::Value;
    }

    std::string var_name = name;
    if (var_rename_map.contains(name)) {
        var_name = var_rename_map.at(name);
    } else if (expr.symbol) {
        var_name = expr.symbol->name;
    }

    builder.add_variable_load(var_name);

    if (expr.symbol) {
        return expr.symbol->type;
    }

    return Type::Value;
}

Type CodeGenerator::handle(const UnaryExpr& expr, PostfixBuilder& builder) {
    if (expr.op.type == TokenType::Minus) {
        if (auto* num = get_if<NumberExpr>(expr.right.get())) {
            std::string val = num->value.value;
            if (!val.starts_with("-")) {
                val = std::format("-{}", val);
            }
            builder.add_number(val);
            return Type::Literal;
        }
    }

    generate(expr.right.get(), builder);

    if (expr.op.type == TokenType::Not) {
        builder.add_number("0");
        builder.add_op(TokenType::Eq);
        return Type::Value;
    }

    builder.add_unary_op(expr.op.type);
    return Type::Value;
}

Type CodeGenerator::handle(const BinaryExpr& expr, PostfixBuilder& builder) {
    generate(expr.left.get(), builder);

    if (expr.op.type == TokenType::LogicalAnd ||
        expr.op.type == TokenType::LogicalOr) {
        builder.add_number("0");
        builder.add_op(TokenType::Eq);
        builder.add_unary_op(TokenType::Not);

        generate(expr.right.get(), builder);
        builder.add_number("0");
        builder.add_op(TokenType::Eq);
        builder.add_unary_op(TokenType::Not);

        builder.add_op(expr.op.type);
        return Type::Value;
    }

    generate(expr.right.get(), builder);
    builder.add_op(expr.op.type);
    return Type::Value;
}

Type CodeGenerator::handle(const TernaryExpr& expr, PostfixBuilder& builder) {
    generate(expr.cond.get(), builder);
    builder.add_number("0");
    builder.add_op(TokenType::Eq);
    builder.add_unary_op(TokenType::Not);

    PostfixBuilder true_branch_builder;
    generate(expr.true_expr.get(), true_branch_builder);

    PostfixBuilder false_branch_builder;
    generate(expr.false_expr.get(), false_branch_builder);

    builder.append(true_branch_builder);
    builder.append(false_branch_builder);
    builder.add_ternary_op();
    return Type::Value;
}

Type CodeGenerator::handle(const CallExpr& expr, PostfixBuilder& builder) {
    // User-defined functions are inlined
    if (expr.resolved_signature != nullptr && expr.resolved_def != nullptr) {
        const auto& sig = *expr.resolved_signature;
        FunctionDef* func_def = expr.resolved_def;

        inline_function_call(sig, func_def, expr.args, expr.range, builder);
        return Type::Value;
    }

    // Built-in functions
    if (expr.resolved_builtin != nullptr) {
        const BuiltinFunction* builtin = expr.resolved_builtin;

        if (builtin->special_handler) {
            builder.append(builtin->special_handler(this, expr));
            return Type::Value;
        }

        for (size_t i = 0; i < expr.args.size(); ++i) {
            if (builtin->param_types[i] != Type::Literal_string) {
                generate(expr.args[i].get(), builder);
            }
        }
        builder.add_function_call(expr.callee);
        return Type::Value;
    }

    // nth_N functions
    if (expr.callee.starts_with("nth_")) {
        std::string n_str = expr.callee.substr(4);
        int n = std::stoi(n_str);
        int arg_count = static_cast<int>(expr.args.size());

        for (const auto& arg : expr.args) {
            generate(arg.get(), builder);
        }
        builder.add_sortN(arg_count);
        builder.add_dropN(n - 1);
        builder.add_swapN(arg_count - n);
        builder.add_dropN(arg_count - n);

        return Type::Value;
    }

    std::unreachable();
}

Type CodeGenerator::handle(const PropAccessExpr& expr,
                           PostfixBuilder& builder) {
    std::string clip_name = expr.clip.value;
    if (param_substitutions.contains(clip_name)) {
        auto* subst_expr = param_substitutions.at(clip_name);
        clip_name = generate_expr_to_string(subst_expr);
    } else {
        clip_name = clip_name.substr(1);
    }

    builder.add_prop_access(clip_name, expr.prop.value);
    return Type::Value;
}

Type CodeGenerator::handle(const StaticRelPixelAccessExpr& expr,
                           PostfixBuilder& builder) {
    std::string clip_name = expr.clip.value;
    if (param_substitutions.contains(clip_name)) {
        auto* subst_expr = param_substitutions.at(clip_name);
        clip_name = generate_expr_to_string(subst_expr);
    } else {
        clip_name = clip_name.substr(1);
    }

    builder.add_static_pixel_access(clip_name, expr.offsetX.value,
                                    expr.offsetY.value, expr.boundary_suffix);
    return Type::Value;
}

Type CodeGenerator::handle(const FrameDimensionExpr& expr,
                           PostfixBuilder& builder) {
    std::string plane_idx_str =
        generate_expr_to_string(expr.plane_index_expr.get());
    builder.add_frame_dimension(expr.dimension_name, plane_idx_str);
    return Type::Value;
}

Type CodeGenerator::handle(const ArrayAccessExpr& expr,
                           PostfixBuilder& builder) {
    // Get the array variable name
    std::string array_name;
    auto* var_expr = get_if<VariableExpr>(expr.array.get());
    if (var_expr != nullptr) {
        array_name = var_expr->name.value;

        if (var_rename_map.contains(array_name)) {
            array_name = var_rename_map.at(array_name);
        } else if (expr.array_symbol) {
            array_name = expr.array_symbol->name;
        }
    } else if (expr.array_symbol) {
        array_name = expr.array_symbol->name;
    }

    generate(expr.index.get(), builder);
    builder.add_array_load(array_name);

    return Type::Value;
}

void CodeGenerator::handle(const ExprStmt& stmt, PostfixBuilder& builder) {
#ifndef NDEBUG
    PostfixBuilder temp_builder;
    generate(stmt.expr.get(), temp_builder);
    check_stack_effect(temp_builder.get_expression(), 0, stmt.range);
    builder.append(temp_builder);
#else
    generate(stmt.expr.get(), builder);
#endif
}

void CodeGenerator::handle(const AssignStmt& stmt, PostfixBuilder& builder) {
    std::string name = stmt.name.value;

    std::string var_name = name;
    if (var_rename_map.contains(name)) {
        var_name = var_rename_map.at(name);
    } else if (stmt.symbol) {
        var_name = stmt.symbol->name;
    }

    // Check if this is a new() or resize() call for array allocation
    if (auto* call_expr = get_if<CallExpr>(stmt.value.get())) {
        if (call_expr->callee == "new" || call_expr->callee == "resize") {
#ifndef NDEBUG
            PostfixBuilder b;
#else
            PostfixBuilder& b = builder;
#endif
            if (mode == Mode::Expr) {
                auto* arg_expr = call_expr->args[0].get();

                std::string size_value;

                if (auto* num_expr = get_if<NumberExpr>(arg_expr)) {
                    size_value = num_expr->value.value;
                } else if (auto* var_expr = get_if<VariableExpr>(arg_expr)) {
                    std::string var_name_arg = var_expr->name.value;
                    if (param_substitutions.contains(var_name_arg)) {
                        auto* subst_expr = param_substitutions.at(var_name_arg);
                        if (auto* subst_num = get_if<NumberExpr>(subst_expr)) {
                            size_value = subst_num->value.value;
                        }
                    }
                }

                b.add_array_alloc_static(var_name, size_value);
            } else {
                generate(call_expr->args[0].get(), b);
                b.add_array_alloc_dynamic(var_name);
            }
#ifndef NDEBUG
            check_stack_effect(b.get_expression(), 0, stmt.range);
            builder.append(b);
#endif
            return;
        }
    }

#ifndef NDEBUG
    PostfixBuilder b;
    generate(stmt.value.get(), b);
    b.add_variable_store(var_name);
    check_stack_effect(b.get_expression(), 0, stmt.range);
    builder.append(b);
#else
    generate(stmt.value.get(), builder);
    builder.add_variable_store(var_name);
#endif
}

void CodeGenerator::handle(const ArrayAssignStmt& stmt,
                           PostfixBuilder& builder) {
    auto* array_access = get_if<ArrayAccessExpr>(stmt.target.get());

    std::string array_name;
    auto* var_expr = get_if<VariableExpr>(array_access->array.get());
    if (var_expr != nullptr) {
        array_name = var_expr->name.value;

        if (var_rename_map.contains(array_name)) {
            array_name = var_rename_map.at(array_name);
        } else if (array_access->array_symbol) {
            array_name = array_access->array_symbol->name;
        }
    } else if (array_access->array_symbol) {
        array_name = array_access->array_symbol->name;
    }

#ifndef NDEBUG
    PostfixBuilder b;
    generate(stmt.value.get(), b);
    generate(array_access->index.get(), b);
    b.add_array_store(array_name);
    check_stack_effect(b.get_expression(), 0, stmt.range);
    builder.append(b);
#else
    generate(stmt.value.get(), builder);
    generate(array_access->index.get(), builder);
    builder.add_array_store(array_name);
#endif
}

void CodeGenerator::handle(const BlockStmt& stmt, PostfixBuilder& builder) {
    for (const auto& s : stmt.statements) {
        generate(s.get(), builder);
    }
}

void CodeGenerator::handle(const IfStmt& stmt, PostfixBuilder& builder) {
    std::string else_label = std::format("__internal_else_{}", label_counter++);
    std::string endif_label =
        std::format("__internal_endif_{}", label_counter++);

    generate(stmt.condition.get(), builder);
    builder.add_number("0");
    builder.add_op(TokenType::Eq);
    builder.add_conditional_jump(else_label);

    generate(stmt.then_branch.get(), builder);

    if (stmt.else_branch) {
        builder.add_unconditional_jump(endif_label);
        builder.add_label(else_label);
        generate(stmt.else_branch.get(), builder);
        builder.add_label(endif_label);
    } else {
        builder.add_label(else_label);
    }
}

void CodeGenerator::handle(const WhileStmt& stmt, PostfixBuilder& builder) {
    std::string start_label =
        std::format("__internal_while_start_{}", label_counter++);
    std::string end_label =
        std::format("__internal_while_end_{}", label_counter++);

    builder.add_label(start_label);
    generate(stmt.condition.get(), builder);
    builder.add_number("0");
    builder.add_op(TokenType::Eq);
    builder.add_conditional_jump(end_label);
    generate(stmt.body.get(), builder);
    builder.add_unconditional_jump(start_label);
    builder.add_label(end_label);
}

void CodeGenerator::handle(const ReturnStmt& stmt, PostfixBuilder& builder) {
    if (stmt.value) {
        generate(stmt.value.get(), builder);
        const int call_id = call_site_id_stack.back();
        std::string ret_var_name = std::format("__internal_ret_{}_{}",
                                               current_function->name, call_id);
        builder.add_variable_store(ret_var_name);
    }

    const int call_id = call_site_id_stack.back();
    std::string ret_label = std::format("__internal_ret_label_{}_{}",
                                        current_function->name, call_id);
    builder.add_unconditional_jump(ret_label);
}

void CodeGenerator::handle(const LabelStmt& stmt, PostfixBuilder& builder) {
    std::string label_name = stmt.symbol ? stmt.symbol->name : stmt.name.value;
    builder.add_label(label_name);
}

void CodeGenerator::handle(const GotoStmt& stmt, PostfixBuilder& builder) {
    std::string label_name = stmt.target_label_symbol
                                 ? stmt.target_label_symbol->name
                                 : stmt.label.value;
    if (stmt.condition) {
        generate(stmt.condition.get(), builder);
        builder.add_number("0");
        builder.add_op(TokenType::Eq);
        builder.add_unary_op(TokenType::Not);
        builder.add_conditional_jump(label_name);
    } else {
        builder.add_unconditional_jump(label_name);
    }
}

void CodeGenerator::handle([[maybe_unused]] const FunctionDef& stmt,
                           [[maybe_unused]] PostfixBuilder& builder) {
    // Inlined at call sites
}

void CodeGenerator::handle([[maybe_unused]] const GlobalDecl& stmt,
                           [[maybe_unused]] PostfixBuilder& builder) {
    // Global declarations don't generate code
}

void CodeGenerator::check_stack_effect([[maybe_unused]] const std::string& s,
                                       [[maybe_unused]] int expected,
                                       [[maybe_unused]] const Range& range) {
#ifndef NDEBUG
    int effect = compute_stack_effect(s, range);
    if (effect != expected) {
        throw CodeGenError(std::format("Unbalanced stack. "
                                       "Expected {}, got {}",
                                       expected, effect),
                           range);
    }
#endif
}

int CodeGenerator::compute_stack_effect(const std::string& s,
                                        const Range& range) {
    PostfixMode postfix_mode =
        (mode == Mode::Expr) ? PostfixMode::EXPR : PostfixMode::SINGLE_EXPR;

    try {
        return compute_postfix_stack_effect(s, postfix_mode, range.start.line,
                                            num_inputs);
    } catch (const std::exception& e) {
        throw CodeGenError(e.what(), range);
    }
}

void CodeGenerator::inline_function_call(
    const FunctionSignature& sig, FunctionDef* func_def,
    const std::vector<std::unique_ptr<Expr>>& args,
    [[maybe_unused]] const Range& call_range, PostfixBuilder& builder) {

    const auto& func_name = sig.name;
    const int call_id = call_site_counter++;
    call_site_id_stack.push_back(call_id);

    // Save current state
    auto saved_param_substitutions = param_substitutions;
    auto saved_var_rename_map = var_rename_map;
    const auto* saved_current_function = current_function;

    param_substitutions.clear();
    std::map<std::string, std::string> param_map;

    PostfixBuilder param_assignments;

    for (size_t i = 0; i < sig.params.size(); ++i) {
        const auto& param_info = sig.params[i];
        const std::string& param_name = param_info.name;
        const Type param_type = param_info.type;

        auto* arg_expr = args[i].get();

        if (param_type == Type::Literal || param_type == Type::Clip) {
            param_substitutions[param_name] = arg_expr;
        } else if (param_type == Type::Array) {
            auto* var_expr = get_if<VariableExpr>(arg_expr);
            if (var_expr != nullptr) {
                std::string arg_var_name = var_expr->name.value;
                if (saved_var_rename_map.contains(arg_var_name)) {
                    param_map[param_name] =
                        saved_var_rename_map.at(arg_var_name);
                } else if (var_expr->symbol) {
                    param_map[param_name] = var_expr->symbol->name;
                } else {
                    param_map[param_name] = arg_var_name;
                }
            }
        } else {
            std::string renamed_param =
                std::format("__internal_func_{}_"
                            "{}_{}",
                            func_name, call_id, param_name);

            generate(args[i].get(), param_assignments);
            param_assignments.add_variable_store(renamed_param);
            param_map[param_name] = renamed_param;
        }
    }

    // Collect local variables from function body
    std::function<void(Stmt*)> collect_locals = [&](Stmt* stmt) {
        if (!stmt) {
            return;
        }
        if (auto* assign = get_if<AssignStmt>(stmt)) {
            std::string var_name = assign->name.value;
            if (!param_map.contains(var_name)) {
                std::string renamed_var =
                    std::format("__"
                                "interna"
                                "l_func_"
                                "{}_{}_{"
                                "}",
                                func_name, call_id, var_name);
                param_map[var_name] = renamed_var;
            }
        } else if (auto* block = get_if<BlockStmt>(stmt)) {
            for (const auto& s : block->statements) {
                collect_locals(s.get());
            }
        } else if (auto* if_stmt = get_if<IfStmt>(stmt)) {
            collect_locals(if_stmt->then_branch.get());
            if (if_stmt->else_branch) {
                collect_locals(if_stmt->else_branch.get());
            }
        } else if (auto* while_stmt = get_if<WhileStmt>(stmt)) {
            collect_locals(while_stmt->body.get());
        }
    };

    for (const auto& s : func_def->body->statements) {
        collect_locals(s.get());
    }

    // Update context for function body generation
    var_rename_map = param_map;
    current_function = &sig;

    std::string label_prefix =
        std::format("__internal_{}_{}_", func_name, call_id);

    PostfixBuilder body_builder;
    handle(*func_def->body, body_builder);
    body_builder.prefix_labels(label_prefix);

    std::string ret_label =
        std::format("__internal_ret_label_{}_{}", func_name, call_id);
    body_builder.add_label(ret_label);

    if (sig.returns_value) {
        std::string ret_var =
            std::format("__internal_ret_{}_{}", func_name, call_id);
        body_builder.add_variable_load(ret_var);
    }

    // Restore state
    param_substitutions = saved_param_substitutions;
    var_rename_map = saved_var_rename_map;
    current_function = saved_current_function;
    call_site_id_stack.pop_back();

    PostfixBuilder inlined_builder;
    inlined_builder.append(param_assignments);
    inlined_builder.append(body_builder);

#ifndef NDEBUG
    int expected_effect = sig.returns_value ? 1 : 0;
    try {
        int actual_effect =
            compute_stack_effect(inlined_builder.get_expression(), call_range);
        if (actual_effect != expected_effect) {
            throw CodeGenError(std::format("Function '{}' has "
                                           "unbalanced stack. "
                                           "Expected "
                                           "effect: {}, "
                                           "actual: {}",
                                           func_name, expected_effect,
                                           actual_effect),
                               sig.range);
        }
    } catch (const CodeGenError& e) {
        throw CodeGenError(
            std::format("In function '{}': {}", func_name, e.what()),
            sig.range);
    }
#endif
    builder.append(inlined_builder);
}

} // namespace infix2postfix
