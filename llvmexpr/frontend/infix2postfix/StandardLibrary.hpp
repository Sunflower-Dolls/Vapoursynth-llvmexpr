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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_STANDARDLIBRARY_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_STANDARDLIBRARY_HPP

// Import all standard library definitions
#include "stdlib/Algorithms.hpp"
#include "stdlib/LibraryBase.hpp"
#include "stdlib/Meta.hpp"
#include "stdlib/Std.hpp"

#include <optional>
#include <string_view>
#include <type_traits>
#include <vector>

namespace infix2postfix {

using stdlib::ExportedFunction;
using stdlib::IsLibrary;

using AllStandardLibraries =
    std::tuple<stdlib::Algorithms, stdlib::Meta, stdlib::Std>;

namespace detail {

template <typename T, typename Tuple> struct tuple_contains;

template <typename T, typename... Us>
struct tuple_contains<T, std::tuple<Us...>>
    : std::bool_constant<(std::is_same_v<T, Us> || ...)> {};

template <typename T, typename Tuple>
inline constexpr bool TUPLE_CONTAINS_V = tuple_contains<T, Tuple>::value;

template <typename Tuple, typename T> struct TuplePushFront {
    using type =
        decltype(std::tuple_cat(std::tuple<T>{}, std::declval<Tuple>()));
};

template <typename Lib, typename Resolved> struct AppendIfAbsent {
    using type =
        std::conditional_t<TUPLE_CONTAINS_V<Lib, Resolved>, Resolved,
                           decltype(std::tuple_cat(std::declval<Resolved>(),
                                                   std::tuple<Lib>{}))>;
};

template <typename DepTuple, typename RecStack, typename Resolved>
struct resolve_deps;

template <IsLibrary Lib, typename RecStack, typename Resolved>
struct ResolveOne; // Forward declaration

template <IsLibrary Dep, typename... Deps, typename RecStack, typename Resolved>
struct resolve_deps<std::tuple<Dep, Deps...>, RecStack, Resolved> {
    using after_first = typename ResolveOne<Dep, RecStack, Resolved>::type;
    using type =
        typename resolve_deps<std::tuple<Deps...>, RecStack, after_first>::type;
};

template <typename RecStack, typename Resolved>
struct resolve_deps<std::tuple<>, RecStack, Resolved> {
    using type = Resolved;
};

template <IsLibrary Lib, typename RecStack, typename Resolved>
struct ResolveOne {
    static_assert(!TUPLE_CONTAINS_V<Lib, RecStack>,
                  "Circular dependency detected in standard libraries");

    using rec2 = typename TuplePushFront<RecStack, Lib>::type;
    using after_deps =
        typename resolve_deps<typename Lib::dependencies, rec2, Resolved>::type;
    using type = typename AppendIfAbsent<Lib, after_deps>::type;
};

} // namespace detail

template <IsLibrary Lib>
using ResolveLibraryDependencies =
    typename detail::ResolveOne<Lib, std::tuple<>, std::tuple<>>::type;

class StandardLibraryManager {
  public:
    static std::vector<std::string_view>
    resolveDependencies(std::string_view library_name);

    static std::optional<std::string_view>
    getLibraryCode(std::string_view library_name);

    static std::optional<std::vector<ExportedFunction>>
    getExports(std::string_view library_name);
};

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_STANDARDLIBRARY_HPP
