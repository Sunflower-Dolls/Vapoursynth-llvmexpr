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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_STDLIB_STD_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_STDLIB_STD_HPP

#include "LibraryBase.hpp"

#include "Meta.hpp"
#include <array>

namespace infix2postfix::stdlib {

struct Std {
    static constexpr ::std::string_view NAME = "std";

    //NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    static constexpr char CODE_DATA[] = {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#embed "std.expr"
#pragma clang diagnostic pop
        , 0 // null terminator
    };
    static constexpr ::std::string_view CODE = ::std::string_view(
        static_cast<const char*>(CODE_DATA), sizeof(CODE_DATA) - 1);

    using dependencies = ::std::tuple<Meta>;

    static constexpr ::std::array<ExportedFunction, 15> EXPORTS = {{
        ExportedFunction{.name = "get_width",
                         .param_count = 1,
                         .mode = ExportMode::Expr,
                         .internal_name_override =
                             "___stdlib_std_get_width_expr"},
        ExportedFunction{.name = "get_width",
                         .param_count = 2,
                         .mode = ExportMode::SingleExpr,
                         .internal_name_override =
                             "___stdlib_std_get_width_single"},
        ExportedFunction{.name = "get_height",
                         .param_count = 1,
                         .mode = ExportMode::Expr,
                         .internal_name_override =
                             "___stdlib_std_get_height_expr"},
        ExportedFunction{.name = "get_height",
                         .param_count = 2,
                         .mode = ExportMode::SingleExpr,
                         .internal_name_override =
                             "___stdlib_std_get_height_single"},
        ExportedFunction{.name = "get_bitdepth", .param_count = 1},
        ExportedFunction{.name = "get_sampletype", .param_count = 1},
        ExportedFunction{.name = "get_colorfamily", .param_count = 1},
        ExportedFunction{.name = "cfUndefined", .param_count = 0},
        ExportedFunction{.name = "cfGray", .param_count = 0},
        ExportedFunction{.name = "cfRGB", .param_count = 0},
        ExportedFunction{.name = "cfYUV", .param_count = 0},
        ExportedFunction{.name = "stInteger", .param_count = 0},
        ExportedFunction{.name = "stFloat", .param_count = 0},
    }};
};

} // namespace infix2postfix::stdlib

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_STDLIB_STD_HPP
