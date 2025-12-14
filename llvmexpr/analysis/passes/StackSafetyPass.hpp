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

#ifndef LLVMEXPR_ANALYSIS_STACK_SAFETY_PASS_HPP
#define LLVMEXPR_ANALYSIS_STACK_SAFETY_PASS_HPP

#include "../framework/Pass.hpp"

#include <vector>

namespace analysis {

struct StackSafetyResult {
    std::vector<int> stack_depth_in;
};

/**
    Verifies that:
    1. All execution paths have consistent stack depths at merge points
    2. No stack underflows occur (sufficient depth before pop operations)
    3. All reachable terminal blocks end with expected final depth
    This is the final safety check before code generation.
    Depends on: BlockAnalysisPass
 */
class StackSafetyPass
    : public AnalysisPass<StackSafetyPass, StackSafetyResult> {
  public:
    using Result = StackSafetyResult;

    [[nodiscard]] const char* getName() const override {
        return "Stack Safety Pass";
    }

    Result run(const std::vector<Token>& tokens, AnalysisManager& am) override;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_STACK_SAFETY_PASS_HPP
