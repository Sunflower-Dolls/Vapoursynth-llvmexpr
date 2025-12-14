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

#ifndef LLVMEXPR_ANALYSIS_PASSES_VARIABLEUSAGEPASS_HPP
#define LLVMEXPR_ANALYSIS_PASSES_VARIABLEUSAGEPASS_HPP

#include "../framework/Pass.hpp"

#include <set>
#include <string>

namespace analysis {

struct VariableUsageResult {
    std::set<std::string> all_vars;
};

/**
    Analyzes the expression to find all declared and used variables.
    Collects:
    - A set of all unique variable names used in the expression.
    This information is used by the IR generator to pre-allocate stack space
    for all variables at the beginning of the function.
    Depends on: None
 */
class VariableUsagePass
    : public AnalysisPass<VariableUsagePass, VariableUsageResult> {
  public:
    VariableUsageResult run(const std::vector<Token>& tokens,
                            AnalysisManager& am) override;

    [[nodiscard]] const char* getName() const override {
        return "VariableUsagePass";
    }
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_PASSES_VARIABLEUSAGEPASS_HPP
