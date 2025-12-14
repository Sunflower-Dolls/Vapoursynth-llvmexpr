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

#ifndef LLVMEXPR_ANALYSIS_CONST_PROP_PASS_HPP
#define LLVMEXPR_ANALYSIS_CONST_PROP_PASS_HPP

#include "../framework/Pass.hpp"

#include <optional>
#include <vector>

namespace analysis {

struct ConstPropResult {
    std::vector<std::vector<std::optional<double>>> const_stack_in;
    std::vector<std::vector<std::optional<double>>> const_stack_out;
};
/*
    Performs forward dataflow analysis to track compile-time constant values on the stack through the CFG. 
    Depends on: BlockAnalysisPass
*/
class ConstPropPass : public AnalysisPass<ConstPropPass, ConstPropResult> {
  public:
    using Result = ConstPropResult;

    [[nodiscard]] const char* getName() const override {
        return "Constant Propagation Pass";
    }

    Result run(const std::vector<Token>& tokens, AnalysisManager& am) override;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_CONST_PROP_PASS_HPP
