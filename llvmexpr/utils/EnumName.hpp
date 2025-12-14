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
 *
 * The implementation is modified from https://github.com/Neargye/magic_enum
 * which is licensed under the MIT License.
 *
 * MIT License
 *
 * Copyright (c) 2019 - 2024 Daniil Goncharov
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef LLVMEXPR_UTILS_ENUMNAME_HPP
#define LLVMEXPR_UTILS_ENUMNAME_HPP

#include <array>
#include <string_view>
#include <type_traits>
#include <utility>

#ifndef __clang__
#error "Vapoursynth-llvmexpr is only supported by Clang"
#endif

template <auto value> consteval std::string_view enum_name() {
    constexpr std::string_view NAME = __PRETTY_FUNCTION__;
    constexpr auto START = NAME.find('=') + 2;
    constexpr auto END = NAME.size() - 1;
    constexpr auto FULL = NAME.substr(START, END - START);
    constexpr auto LAST_COLON = FULL.rfind("::");
    return LAST_COLON == std::string_view::npos ? FULL
                                                : FULL.substr(LAST_COLON + 2);
}

namespace llvmexpr_enum_detail {
template <typename T, std::size_t N> consteval bool is_valid_enum() {
    return !enum_name<static_cast<T>(N)>().contains(")");
}

template <typename T, std::size_t N = 1>
consteval std::size_t find_upper_bound() {
    if constexpr (!is_valid_enum<T, N>()) {
        return N;
    } else {
        return find_upper_bound<T, N * 2>();
    }
}

template <typename T, std::size_t Low, std::size_t High>
consteval std::size_t binary_search_count() {
    if constexpr (High - Low <= 1) {
        return is_valid_enum<T, Low>() ? High : Low;
    } else {
        constexpr std::size_t MID = Low + ((High - Low) / 2);
        if constexpr (is_valid_enum<T, MID>()) {
            return binary_search_count<T, MID, High>();
        } else {
            return binary_search_count<T, Low, MID>();
        }
    }
}

template <typename T> consteval std::size_t enum_count() {
    constexpr std::size_t UPPER = find_upper_bound<T>();
    return binary_search_count<T, UPPER / 2, UPPER>();
}

template <typename T, std::size_t... Is>
consteval auto make_names_impl(std::index_sequence<Is...> /*unused*/) {
    return std::array{enum_name<static_cast<T>(Is)>()...};
}

template <typename T> consteval auto make_names() {
    return make_names_impl<T>(std::make_index_sequence<enum_count<T>()>{});
}
} // namespace llvmexpr_enum_detail

template <typename T> constexpr std::size_t enum_max() {
    return llvmexpr_enum_detail::enum_count<T>();
}

template <typename T>
    requires std::is_enum_v<T> && std::is_scoped_enum_v<T>
constexpr std::string_view enum_name(T value) {
    static constexpr auto NAMES = llvmexpr_enum_detail::make_names<T>();
    const auto index = static_cast<std::size_t>(std::to_underlying(value));
    return index < NAMES.size() ? NAMES[index] : std::string_view{};
}

#endif // LLVMEXPR_UTILS_ENUMNAME_HPP
