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

#include "PostfixHelper.hpp"
#include "../Tokenizer.hpp"

#include <format>
#include <stdexcept>

namespace infix2postfix {

int compute_postfix_stack_effect(const std::string& postfix_expr,
                                 PostfixMode mode, int line, int num_inputs,
                                 int num_intermediate_inputs) {
    ::ExprMode expr_mode = ::ExprMode::SingleExpr;
    if (mode == PostfixMode::Expr) {
        expr_mode = (num_intermediate_inputs > 0) ? ::ExprMode::VkExpr
                                                  : ::ExprMode::Expr;
    }

    std::vector<::Token> tokens;
    try {
        tokens = ::tokenize(postfix_expr, num_inputs, expr_mode,
                            num_intermediate_inputs);
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::format("Line {}: Failed to tokenize postfix expression: {}",
                        line, e.what()));
    }

    int stack = 0;
    for (const auto& token : tokens) {
        ::TokenBehavior behavior = ::get_token_behavior(token);
        stack += behavior.stack_effect;
        if (stack < 0) {
            throw std::runtime_error(
                std::format("Line {}: Stack underflow while processing '{}'",
                            line, token.text));
        }
    }
    return stack;
}

} // namespace infix2postfix
