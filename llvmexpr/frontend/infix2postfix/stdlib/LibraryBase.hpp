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

#ifndef LLVMEXPR_INFIX2POSTFIX_STDLIB_LIBRARYBASE_HPP
#define LLVMEXPR_INFIX2POSTFIX_STDLIB_LIBRARYBASE_HPP

#include <concepts>
#include <string_view>

namespace infix2postfix::stdlib {

enum class ExportMode : std::uint8_t { All, Expr, SingleExpr };

struct ExportedFunction {
    std::string_view name; /// User-visible name
    int param_count;
    ExportMode mode = ExportMode::All;
    // NOLINTNEXTLINE(readability-redundant-string-init)
    std::string_view internal_name_override = "";
};

template <typename T>
concept IsLibrary = requires {
    { T::name } -> std::convertible_to<std::string_view>;
    { T::code } -> std::convertible_to<std::string_view>;
    typename T::dependencies;
    { T::exports };
};

} // namespace infix2postfix::stdlib

#endif // LLVMEXPR_INFIX2POSTFIX_STDLIB_LIBRARYBASE_HPP
