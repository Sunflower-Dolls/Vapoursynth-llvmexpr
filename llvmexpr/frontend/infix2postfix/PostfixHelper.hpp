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

#ifndef LLVMEXPR_INFIX2POSTFIX_POSTFIXHELPER_HPP
#define LLVMEXPR_INFIX2POSTFIX_POSTFIXHELPER_HPP

#include <cstdint>
#include <string>

namespace infix2postfix {

enum class PostfixMode : std::uint8_t {
    Expr,
    SingleExpr,
};

int compute_postfix_stack_effect(const std::string& postfix_expr,
                                 PostfixMode mode, int line, int num_inputs);

} // namespace infix2postfix

#endif