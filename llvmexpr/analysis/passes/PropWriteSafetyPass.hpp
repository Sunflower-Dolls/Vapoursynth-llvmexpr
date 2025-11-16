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

#ifndef LLVMEXPR_ANALYSIS_PROP_WRITE_SAFETY_PASS_HPP
#define LLVMEXPR_ANALYSIS_PROP_WRITE_SAFETY_PASS_HPP

#include "../framework/Pass.hpp"

namespace analysis {

struct PropWriteSafetyResult {};

/**
    Ensures that all prop write operations (`prop$`) are guaranteed to be
    executed on every possible execution path. If a prop write can be
    bypassed due to control flow (e.g., conditional jumps), this pass
    will raise an AnalysisError.

    Depends on: BlockAnalysisPass, StackSafetyPass
 */
class PropWriteSafetyPass
    : public AnalysisPass<PropWriteSafetyPass, PropWriteSafetyResult> {
  public:
    using Result = PropWriteSafetyResult;

    [[nodiscard]] const char* getName() const override {
        return "Prop Write Safety Pass";
    }

    Result run(const std::vector<Token>& tokens, AnalysisManager& am) override;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_PROP_WRITE_SAFETY_PASS_HPP
