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

#include "Builtins.hpp"
#include "AST.hpp"
#include "CodeGenerator.hpp"
#include "PostfixBuilder.hpp"
#include <format>
#include <string>

namespace infix2postfix {

namespace {

PostfixBuilder handle_dyn_expr_3args(CodeGenerator* codegen,
                                     const CallExpr& expr) {
    // Signature: dyn($clip, x, y)
    // Default boundary mode is clamped.
    PostfixBuilder b;
    b.append(codegen->generate_expr(expr.args[1].get()).postfix);
    b.append(codegen->generate_expr(expr.args[2].get()).postfix);

    auto clip_res = codegen->generate_expr(expr.args[0].get());
    std::string clip_name = clip_res.postfix.get_expression();

    b.add_dyn_pixel_access_expr(clip_name, ":c");
    return b;
}

PostfixBuilder handle_dyn_expr_4args(CodeGenerator* codegen,
                                     const CallExpr& expr) {
    // Signature: dyn($clip, x, y, boundary_mode)
    PostfixBuilder b;
    b.append(codegen->generate_expr(expr.args[1].get()).postfix);
    b.append(codegen->generate_expr(expr.args[2].get()).postfix);

    auto clip_res = codegen->generate_expr(expr.args[0].get());
    std::string clip_name = clip_res.postfix.get_expression();

    auto* boundary_expr = get_if<NumberExpr>(expr.args[3].get());
    int boundary_mode = std::stoi(boundary_expr->value.value);
    std::string suffix;
    switch (boundary_mode) {
    case 0: // global
        suffix = "";
        break;
    case 1: // mirrored
        suffix = ":m";
        break;
    case 2: // clamped
        suffix = ":c";
        break;
    default:
        throw CodeGenError(
            std::format("Invalid boundary mode '{}' for dyn()", boundary_mode),
            expr.range);
    }
    b.add_dyn_pixel_access_expr(clip_name, suffix);
    return b;
}

PostfixBuilder handle_dyn_single(CodeGenerator* codegen, const CallExpr& expr) {
    // Signature: dyn($clip, x, y, plane)
    auto clip_res = codegen->generate_expr(expr.args[0].get());
    std::string clip_name = clip_res.postfix.get_expression();

    auto* plane_expr = get_if<NumberExpr>(expr.args[3].get());
    std::string plane_idx = plane_expr->value.value;

    PostfixBuilder b;
    b.append(codegen->generate_expr(expr.args[1].get()).postfix);
    b.append(codegen->generate_expr(expr.args[2].get()).postfix);
    b.add_dyn_pixel_access_single(clip_name, plane_idx);
    return b;
}

PostfixBuilder handle_store_expr(CodeGenerator* codegen, const CallExpr& expr) {
    // store(x, y, val)
    PostfixBuilder b;
    b.append(codegen->generate_expr(expr.args[2].get()).postfix);
    b.append(codegen->generate_expr(expr.args[0].get()).postfix);
    b.append(codegen->generate_expr(expr.args[1].get()).postfix);
    b.add_store_expr();
    return b;
}

PostfixBuilder handle_store_single(CodeGenerator* codegen,
                                   const CallExpr& expr) {
    // store(x, y, plane, value)
    auto* plane_expr = get_if<NumberExpr>(expr.args[2].get());
    std::string plane_idx = plane_expr->value.value;

    PostfixBuilder b;
    b.append(codegen->generate_expr(expr.args[3].get()).postfix);
    b.append(codegen->generate_expr(expr.args[0].get()).postfix);
    b.append(codegen->generate_expr(expr.args[1].get()).postfix);
    b.add_store_single(plane_idx);
    return b;
}

PostfixBuilder handle_set_prop_typed(CodeGenerator* codegen,
                                     const CallExpr& expr,
                                     const std::string& suffix) {
    // set_prop*(prop_name, value)
    auto* prop_name_expr = get_if<VariableExpr>(expr.args[0].get());

    PostfixBuilder b;
    b.append(codegen->generate_expr(expr.args[1].get()).postfix);
    b.add_set_prop(prop_name_expr->name.value, suffix);
    return b;
}

const auto handle_set_propf = [](CodeGenerator* codegen, const CallExpr& expr) {
    return handle_set_prop_typed(codegen, expr, "f");
};

const auto handle_set_propi = [](CodeGenerator* codegen, const CallExpr& expr) {
    return handle_set_prop_typed(codegen, expr, "i");
};

const auto handle_set_propaf = [](CodeGenerator* codegen,
                                  const CallExpr& expr) {
    return handle_set_prop_typed(codegen, expr, "af");
};

const auto handle_set_propai = [](CodeGenerator* codegen,
                                  const CallExpr& expr) {
    return handle_set_prop_typed(codegen, expr, "ai");
};

PostfixBuilder handle_remove_prop([[maybe_unused]] CodeGenerator* codegen,
                                  const CallExpr& expr) {
    // remove_prop(prop_name)
    auto* prop_name_expr = get_if<VariableExpr>(expr.args[0].get());

    PostfixBuilder b;
    b.add_delete_prop(prop_name_expr->name.value);
    return b;
}

PostfixBuilder handle_exit([[maybe_unused]] CodeGenerator* codegen,
                           [[maybe_unused]] const CallExpr& expr) {
    PostfixBuilder b;
    b.add_exit_marker();
    return b;
}

// nth_N functions are not handled here
const std::map<std::string, std::vector<BuiltinFunction>> builtin_functions = {
    // Standard math
    {"sin",
     {BuiltinFunction{.name = "sin",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"cos",
     {BuiltinFunction{.name = "cos",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"tan",
     {BuiltinFunction{.name = "tan",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"asin",
     {BuiltinFunction{.name = "asin",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"acos",
     {BuiltinFunction{.name = "acos",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"atan",
     {BuiltinFunction{.name = "atan",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"atan2",
     {BuiltinFunction{.name = "atan2",
                      .arity = 2,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value, Type::Value}}}},
    {"exp",
     {BuiltinFunction{.name = "exp",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"exp2",
     {BuiltinFunction{.name = "exp2",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"log",
     {BuiltinFunction{.name = "log",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"log2",
     {BuiltinFunction{.name = "log2",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"log10",
     {BuiltinFunction{.name = "log10",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"sqrt",
     {BuiltinFunction{.name = "sqrt",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"abs",
     {BuiltinFunction{.name = "abs",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"sgn",
     {BuiltinFunction{.name = "sgn",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"floor",
     {BuiltinFunction{.name = "floor",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"ceil",
     {BuiltinFunction{.name = "ceil",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"round",
     {BuiltinFunction{.name = "round",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"trunc",
     {BuiltinFunction{.name = "trunc",
                      .arity = 1,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value}}}},
    {"min",
     {BuiltinFunction{.name = "min",
                      .arity = 2,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value, Type::Value}}}},
    {"max",
     {BuiltinFunction{.name = "max",
                      .arity = 2,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value, Type::Value}}}},
    {"copysign",
     {BuiltinFunction{.name = "copysign",
                      .arity = 2,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value, Type::Value}}}},
    {"clamp",
     {BuiltinFunction{.name = "clamp",
                      .arity = 3,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value, Type::Value, Type::Value}}}},
    {"fma",
     {BuiltinFunction{.name = "fma",
                      .arity = 3,
                      .mode_restriction = std::nullopt,
                      .param_types = {Type::Value, Type::Value, Type::Value}}}},

    // Mode-specific & special handling
    {"set_prop",
     {BuiltinFunction{.name = "set_prop",
                      .arity = 2,
                      .mode_restriction = Mode::Single,
                      .param_types = {Type::Literal_string, Type::Value},
                      .special_handler =
                          handle_set_propf, // alias for set_propf
                      .returns_value = false}}},
    {"set_propf",
     {BuiltinFunction{.name = "set_propf",
                      .arity = 2,
                      .mode_restriction = Mode::Single,
                      .param_types = {Type::Literal_string, Type::Value},
                      .special_handler = handle_set_propf,
                      .returns_value = false}}},
    {"set_propi",
     {BuiltinFunction{.name = "set_propi",
                      .arity = 2,
                      .mode_restriction = Mode::Single,
                      .param_types = {Type::Literal_string, Type::Value},
                      .special_handler = handle_set_propi,
                      .returns_value = false}}},
    {"set_propaf",
     {BuiltinFunction{.name = "set_propaf",
                      .arity = 2,
                      .mode_restriction = Mode::Single,
                      .param_types = {Type::Literal_string, Type::Value},
                      .special_handler = handle_set_propaf,
                      .returns_value = false}}},
    {"set_propai",
     {BuiltinFunction{.name = "set_propai",
                      .arity = 2,
                      .mode_restriction = Mode::Single,
                      .param_types = {Type::Literal_string, Type::Value},
                      .special_handler = handle_set_propai,
                      .returns_value = false}}},
    {"remove_prop",
     {BuiltinFunction{.name = "remove_prop",
                      .arity = 1,
                      .mode_restriction = Mode::Single,
                      .param_types = {Type::Literal_string},
                      .special_handler = handle_remove_prop,
                      .returns_value = false}}},
    {"dyn",
     {
         BuiltinFunction{.name = "dyn",
                         .arity = 3,
                         .mode_restriction = Mode::Expr,
                         .param_types = {Type::Clip, Type::Value, Type::Value},
                         .special_handler = &handle_dyn_expr_3args},
         BuiltinFunction{.name = "dyn",
                         .arity = 4,
                         .mode_restriction = Mode::Expr,
                         .param_types = {Type::Clip, Type::Value, Type::Value,
                                         Type::Literal},
                         .special_handler = &handle_dyn_expr_4args},
         BuiltinFunction{.name = "dyn",
                         .arity = 4,
                         .mode_restriction = Mode::Single,
                         .param_types = {Type::Clip, Type::Value, Type::Value,
                                         Type::Literal},
                         .special_handler = &handle_dyn_single},
     }},
    {"store",
     {
         BuiltinFunction{.name = "store",
                         .arity = 3,
                         .mode_restriction = Mode::Expr,
                         .param_types = {Type::Value, Type::Value, Type::Value},
                         .special_handler = &handle_store_expr,
                         .returns_value = false},
         BuiltinFunction{.name = "store",
                         .arity = 4,
                         .mode_restriction = Mode::Single,
                         .param_types = {Type::Value, Type::Value,
                                         Type::Literal, Type::Value},
                         .special_handler = &handle_store_single,
                         .returns_value = false},
     }},
    {"exit",
     {BuiltinFunction{.name = "exit",
                      .arity = 0,
                      .mode_restriction = Mode::Expr,
                      .param_types = {},
                      .special_handler = &handle_exit,
                      .returns_value = false}}},
};

} // namespace

const std::map<std::string, std::vector<BuiltinFunction>>&
get_builtin_functions() {
    return builtin_functions;
}

} // namespace infix2postfix