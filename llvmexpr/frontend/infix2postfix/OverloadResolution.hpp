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

#ifndef LLVMEXPR_INFIX2POSTFIX_OVERLOADRESOLUTION_HPP
#define LLVMEXPR_INFIX2POSTFIX_OVERLOADRESOLUTION_HPP

#include "types.hpp"

#include <algorithm>
#include <optional>
#include <vector>

namespace infix2postfix {

inline bool isConvertible(Type from, Type to, Mode mode) {
    if (from == Type::Void) {
        return false;
    }
    return from == to || (to == Type::Value && from != Type::LiteralString &&
                          from != Type::Array &&
                          (mode != Mode::Single || from != Type::Clip));
}

template <typename T> struct OverloadCandidate {
    const T* item;
    int conversion_count;
    int first_conversion_index;
};

template <typename T>
const OverloadCandidate<T>*
selectBestCandidate(std::vector<OverloadCandidate<T>>& candidates) {
    if (candidates.empty()) {
        return nullptr;
    }

    OverloadCandidate<T>* best = candidates.data();
    for (size_t i = 1; i < candidates.size(); ++i) {
        if (candidates[i].conversion_count < best->conversion_count) {
            best = &candidates[i];
        } else if (candidates[i].conversion_count == best->conversion_count) {
            if (candidates[i].first_conversion_index >
                best->first_conversion_index) {
                best = &candidates[i];
            }
        }
    }
    return best;
}

template <typename T>
bool isAmbiguous(const std::vector<OverloadCandidate<T>>& candidates,
                 const OverloadCandidate<T>* best) {
    if (!best || candidates.size() <= 1) {
        return false;
    }

    int count = 0;
    count = std::ranges::count_if(candidates, [best](const auto& cand) {
        return cand.conversion_count == best->conversion_count &&
               cand.first_conversion_index == best->first_conversion_index;
    });
    return count > 1;
}

template <typename T, typename ArgTypeGetter, typename ParamTypeGetter>
std::vector<OverloadCandidate<T>>
computeCandidates(const std::vector<T>& overloads, size_t arg_count,
                  ArgTypeGetter get_arg_type, ParamTypeGetter get_param_type,
                  Mode mode) {
    std::vector<OverloadCandidate<T>> candidates;

    for (const auto& item : overloads) {
        int conversion_count = 0;
        int first_conversion_index = -1;
        bool possible = true;

        for (size_t j = 0; j < arg_count; ++j) {
            std::optional<Type> arg_type_opt = get_arg_type(item, j);
            std::optional<Type> param_type_opt = get_param_type(item, j);

            if (!arg_type_opt.has_value() || !param_type_opt.has_value()) {
                possible = false;
                break;
            }

            Type arg_type = arg_type_opt.value();
            Type param_type = param_type_opt.value();

            if (arg_type != param_type) {
                if (isConvertible(arg_type, param_type, mode)) {
                    conversion_count++;
                    if (first_conversion_index == -1) {
                        first_conversion_index = static_cast<int>(j);
                    }
                } else {
                    possible = false;
                    break;
                }
            }
        }

        if (possible) {
            candidates.push_back(
                {&item, conversion_count, first_conversion_index});
        }
    }

    return candidates;
}

} // namespace infix2postfix

#endif
