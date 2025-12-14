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

#ifndef LLVMEXPR_INFIX2POSTFIX_BUILTINS_HPP
#define LLVMEXPR_INFIX2POSTFIX_BUILTINS_HPP

#include "AST.hpp"
#include "CodeGenerator.hpp"
#include "types.hpp"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace infix2postfix {

struct CallExpr; // Forward declaration

struct BuiltinFunction {
    std::string name;
    int arity;
    std::optional<Mode> mode_restriction;
    std::vector<Type> param_types;
    std::function<PostfixBuilder(CodeGenerator*, const CallExpr&)>
        special_handler = nullptr;
    bool returns_value = true;
};

const std::map<std::string, std::vector<BuiltinFunction>>&
get_builtin_functions();

} // namespace infix2postfix

#endif
