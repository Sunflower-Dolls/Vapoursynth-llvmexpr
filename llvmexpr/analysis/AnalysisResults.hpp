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

#ifndef LLVMEXPR_ANALYSIS_RESULTS_HPP
#define LLVMEXPR_ANALYSIS_RESULTS_HPP

#include "framework/AnalysisManager.hpp"
#include "passes/BlockAnalysisPass.hpp"
#include "passes/BuildCFGPass.hpp"
#include "passes/CoordinateUsagePass.hpp"
#include "passes/RelAccessAnalysisPass.hpp"
#include "passes/ReloopPass.hpp"
#include "passes/StackSafetyPass.hpp"
#include "passes/VariableUsagePass.hpp"
#include <map>
#include <string>
#include <vector>

namespace analysis {

class ExpressionAnalysisResults {
  public:
    explicit ExpressionAnalysisResults(const AnalysisManager& manager)
        : manager(manager) {}

    [[nodiscard]] const std::vector<CFGBlock>& getCFGBlocks() const {
        return manager.getResult<BlockAnalysisPass>().cfg_blocks;
    }

    [[nodiscard]] const std::map<std::string, int>& getLabelToBlockIdx() const {
        return manager.getResult<BuildCFGPass>().label_to_block_idx;
    }

    [[nodiscard]] const std::vector<int>& getStackDepthIn() const {
        return manager.getResult<StackSafetyPass>().stack_depth_in;
    }

    [[nodiscard]] const RelAccessAnalysisResult&
    getRelAccessAnalysisResult() const {
        return manager.getResult<RelAccessAnalysisPass>();
    }

    [[nodiscard]] const CoordinateUsageResult&
    getCoordinateUsageResult() const {
        return manager.getResult<CoordinateUsagePass>();
    }

    [[nodiscard]] const VariableUsageResult& getVariableUsageResult() const {
        return manager.getResult<VariableUsagePass>();
    }

    [[nodiscard]] const ReloopResult& getReloopResult() const {
        return manager.getResult<ReloopPass>();
    }

    [[nodiscard]] const AnalysisManager& getManager() const { return manager; }

  private:
    const AnalysisManager& manager;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_RESULTS_HPP
