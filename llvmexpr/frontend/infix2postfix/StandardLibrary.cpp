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

#include "StandardLibrary.hpp"

#include <stdexcept>
#include <string>

namespace infix2postfix {

std::vector<std::string_view>
StandardLibraryManager::resolveDependencies(std::string_view library_name) {
    std::vector<std::string_view> result;

    std::apply(
        [&](auto... libs) {
            (
                [&] {
                    using LibType = std::decay_t<decltype(libs)>;
                    if (result.empty() && LibType::NAME == library_name) {
                        using Resolved = ResolveLibraryDependencies<LibType>;
                        std::apply(
                            [&](auto... rs) {
                                (result.push_back(
                                     std::decay_t<decltype(rs)>::NAME),
                                 ...);
                            },
                            Resolved{});
                    }
                }(),
                ...);
        },
        AllStandardLibraries{});

    if (result.empty()) {
        throw std::runtime_error(std::string("Library '") +
                                 std::string(library_name) + "' not found");
    }

    return result;
}

std::optional<std::string_view>
StandardLibraryManager::getLibraryCode(std::string_view library_name) {
    std::optional<std::string_view> result;

    std::apply(
        [&](auto... libs) {
            (
                [&] {
                    using LibType = std::decay_t<decltype(libs)>;
                    if (!result.has_value() && LibType::NAME == library_name) {
                        result = LibType::CODE;
                    }
                }(),
                ...);
        },
        AllStandardLibraries{});

    return result;
}

std::optional<std::vector<ExportedFunction>>
StandardLibraryManager::getExports(std::string_view library_name) {
    std::optional<std::vector<ExportedFunction>> result;

    std::apply(
        [&](auto... libs) {
            (
                [&] {
                    using LibType = std::decay_t<decltype(libs)>;
                    if (!result.has_value() && LibType::NAME == library_name) {
                        std::vector<ExportedFunction> out;
                        out.reserve(std::size(LibType::EXPORTS));
                        for (const auto& e : LibType::EXPORTS) {
                            out.push_back(e);
                        }
                        result = std::move(out);
                    }
                }(),
                ...);
        },
        AllStandardLibraries{});

    return result;
}

} // namespace infix2postfix
