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

#ifndef LLVMEXPR_INFIX2POSTFIX_STDLIB_ALGORITHMS_HPP
#define LLVMEXPR_INFIX2POSTFIX_STDLIB_ALGORITHMS_HPP

#include "LibraryBase.hpp"

#include <array>
#include <string_view>
#include <tuple>

namespace infix2postfix::stdlib {

struct Algorithms {
    static constexpr std::string_view NAME = "algorithms";

    //NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    static constexpr char CODE_DATA[] = {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc23-extensions"
#embed "algorithms.expr"
#pragma clang diagnostic pop
        , 0 // null terminator
    };
    static constexpr std::string_view CODE = std::string_view(
        static_cast<const char*>(CODE_DATA), sizeof(CODE_DATA) - 1);

    using dependencies = std::tuple<>;

    static constexpr std::array<ExportedFunction, 4> EXPORTS = {{
        ExportedFunction{.name = "sort", .param_count = 3},
        ExportedFunction{.name = "reverse", .param_count = 3},
        ExportedFunction{.name = "swap", .param_count = 3},
        ExportedFunction{.name = "find_kth_smallest", .param_count = 4},
    }};
};

} // namespace infix2postfix::stdlib

#endif // LLVMEXPR_INFIX2POSTFIX_STDLIB_ALGORITHMS_HPP