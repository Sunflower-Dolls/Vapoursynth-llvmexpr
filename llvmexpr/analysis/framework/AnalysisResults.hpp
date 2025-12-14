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

#ifndef LLVMEXPR_ANALYSIS_FRAMEWORK_ANALYSIS_RESULTS_HPP
#define LLVMEXPR_ANALYSIS_FRAMEWORK_ANALYSIS_RESULTS_HPP

#include "AnalysisError.hpp"

#include <any>
#include <format>
#include <typeindex>
#include <unordered_map>

namespace analysis {

class AnalysisResults {
  public:
    template <typename PassT> typename PassT::Result& getResult() {
        auto key = std::type_index(typeid(PassT));
        auto it = results.find(key);
        if (it == results.end()) {
            throw AnalysisError(
                std::format("Analysis result not available for pass: {}",
                            typeid(PassT).name()));
        }
        return std::any_cast<typename PassT::Result&>(it->second);
    }

    template <typename PassT> const typename PassT::Result& getResult() const {
        auto key = std::type_index(typeid(PassT));
        auto it = results.find(key);
        if (it == results.end()) {
            throw AnalysisError(
                std::format("Analysis result not available for pass: {}",
                            typeid(PassT).name()));
        }
        return std::any_cast<const typename PassT::Result&>(it->second);
    }

    template <typename PassT> void setResult(typename PassT::Result&& result) {
        auto key = std::type_index(typeid(PassT));
        results[key] = std::any(std::move(result));
    }

    template <typename PassT> [[nodiscard]] bool hasResult() const {
        return results.contains(std::type_index(typeid(PassT)));
    }

    template <typename PassT> void invalidate() {
        results.erase(std::type_index(typeid(PassT)));
    }

    void invalidate(const std::type_index& pass_type) {
        results.erase(pass_type);
    }

    void clear() { results.clear(); }

    [[nodiscard]] const std::unordered_map<std::type_index, std::any>&
    getAll() const {
        return results;
    }

  private:
    std::unordered_map<std::type_index, std::any> results;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_FRAMEWORK_ANALYSIS_RESULTS_HPP
