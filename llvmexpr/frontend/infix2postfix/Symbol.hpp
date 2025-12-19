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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_SYMBOL_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_SYMBOL_HPP

#include "types.hpp"

#include <cstdint>
#include <string>

namespace infix2postfix {

struct Stmt;
struct Expr;

enum class SymbolKind : std::uint8_t { Variable, Function, Parameter, Label };

struct Symbol {
    SymbolKind kind;
    std::string name;
    Type type;
    Range definition_range;
    bool is_used = false;

    const FunctionSignature* signature = nullptr;
};

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_SYMBOL_HPP
