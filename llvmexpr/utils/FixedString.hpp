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

#ifndef LLVMEXPR_UTILS_FIXEDSTRING_HPP
#define LLVMEXPR_UTILS_FIXEDSTRING_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <string_view>

template <std::size_t N> struct FixedString {
    std::array<char, N> value;

    // NOLINTNEXTLINE(modernize-avoid-c-arrays,cppcoreguidelines-avoid-c-arrays)
    explicit constexpr FixedString(const char (&str)[N]) {
        std::ranges::copy(str, value.begin());
    }

    [[nodiscard]] constexpr std::string_view view() const {
        return {value.data(), N - 1};
    }
};

#endif // LLVMEXPR_UTILS_FIXEDSTRING_HPP
