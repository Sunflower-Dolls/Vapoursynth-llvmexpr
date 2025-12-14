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

#ifndef LLVMEXPR_ANALYSIS_STATIC_ALLOC_REACHABILITY_PASS_HPP
#define LLVMEXPR_ANALYSIS_STATIC_ALLOC_REACHABILITY_PASS_HPP

#include "../framework/Pass.hpp"

#include <set>
#include <string>
#include <vector>

namespace analysis {

struct StaticAllocReachabilityResult {
    std::vector<std::set<std::string>> static_alloc_in;
    std::vector<std::set<std::string>> static_alloc_out;
};

/**
    Tracks which arrays have been allocated as static (ARRAY_ALLOC_STATIC)
    on all paths reaching each program point.
    This is used by ValidationPass to detect illegal re-allocation of statically
    allocated arrays.
    Depends on: BlockAnalysisPass
 */
class StaticAllocReachabilityPass
    : public AnalysisPass<StaticAllocReachabilityPass,
                          StaticAllocReachabilityResult> {
  public:
    using Result = StaticAllocReachabilityResult;

    [[nodiscard]] const char* getName() const override {
        return "Static Allocation Reachability Pass";
    }

    Result run(const std::vector<Token>& tokens, AnalysisManager& am) override;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_STATIC_ALLOC_REACHABILITY_PASS_HPP
