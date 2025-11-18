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

#include "ExpressionAnalyzer.hpp"
#include "framework/AnalysisError.hpp"
#include "llvmexpr/analysis/passes/StackSafetyPass.hpp"
#include "llvmexpr/analysis/passes/ValidationPass.hpp"
#include "passes/CoordinateUsagePass.hpp"
#include "passes/PropWriteTypeSafetyPass.hpp"
#include "passes/RelAccessAnalysisPass.hpp"
#include "passes/VariableUsagePass.hpp"

namespace analysis {

void ExpressionAnalyzer::analyze() {
    if (manager.getTokens().empty()) {
        if (manager.getExpectedFinalDepth() != 0) {
            throw AnalysisError("Expression cannot be empty.");
        }
    }

    manager.getResult<ValidationPass>();
    manager.getResult<StackSafetyPass>();
    manager.getResult<RelAccessAnalysisPass>();
    manager.getResult<CoordinateUsagePass>();
    manager.getResult<VariableUsagePass>();
    manager.getResult<PropWriteTypeSafetyPass>();
}

} // namespace analysis
