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

#ifndef LLVMEXPR_INFIX2POSTFIX_STDLIB_META_HPP
#define LLVMEXPR_INFIX2POSTFIX_STDLIB_META_HPP

#include "LibraryBase.hpp"

namespace infix2postfix::stdlib {

struct meta {
    static constexpr std::string_view name = "meta";

    // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    static constexpr char code_data[] = {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#embed "meta.expr"
#pragma clang diagnostic pop
        ,
        0 // null terminator
    };
    static constexpr std::string_view code = std::string_view(
        static_cast<const char*>(code_data), sizeof(code_data) - 1);

    using dependencies = std::tuple<>;

    static constexpr std::array<ExportedFunction, 5> exports = {{
        ExportedFunction{.name = "ASSERT_CONST", .param_count = 3},
        ExportedFunction{.name = "ERROR", .param_count = 1},
        ExportedFunction{.name = "JOIN", .param_count = 3},
        ExportedFunction{.name = "PASTE", .param_count = 2},
        ExportedFunction{.name = "UNROLL", .param_count = 2},
    }};
};

} // namespace infix2postfix::stdlib

#endif // LLVMEXPR_INFIX2POSTFIX_STDLIB_META_HPP
