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

#ifndef LLVMEXPR_ANALYSIS_BUILD_CFG_PASS_HPP
#define LLVMEXPR_ANALYSIS_BUILD_CFG_PASS_HPP

#include "../framework/DataStructures.hpp"
#include "../framework/Pass.hpp"

#include <map>
#include <string>
#include <vector>

namespace analysis {

struct BuildCFGResult {
    std::vector<CFGBlock> cfg_blocks;
    std::map<std::string, int> label_to_block_idx;
};
/**
    Constructs the Control Flow Graph
    Responsibilities:
    - Identify basic blocks
    - Build successor/predecessor relationships
    - Map label names to block indices
    - Validate jump targets
    Depends on: None
*/
class BuildCFGPass : public AnalysisPass<BuildCFGPass, BuildCFGResult> {
  public:
    using Result = BuildCFGResult;

    [[nodiscard]] const char* getName() const override {
        return "Build CFG Pass";
    }

    Result run(const std::vector<Token>& tokens, AnalysisManager& am) override;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_BUILD_CFG_PASS_HPP
