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

#ifndef LLVMEXPR_ANALYSIS_BLOCK_ANALYSIS_PASS_HPP
#define LLVMEXPR_ANALYSIS_BLOCK_ANALYSIS_PASS_HPP

#include "../framework/DataStructures.hpp"
#include "../framework/Pass.hpp"

#include <vector>

namespace analysis {

struct BlockAnalysisResult {
    std::vector<CFGBlock> cfg_blocks;
};
/**
    Computes per-block properties
    Responsibilities:
    - Calculate stack_effect for each block
    - Calculate min_stack_needed for each block
    - These are local properties that don't depend on other blocks.
    Depends on: BuildCFGPass
*/
class BlockAnalysisPass
    : public AnalysisPass<BlockAnalysisPass, BlockAnalysisResult> {
  public:
    using Result = BlockAnalysisResult;

    [[nodiscard]] const char* getName() const override {
        return "Block Analysis Pass";
    }

    Result run(const std::vector<Token>& tokens, AnalysisManager& am) override;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_BLOCK_ANALYSIS_PASS_HPP
