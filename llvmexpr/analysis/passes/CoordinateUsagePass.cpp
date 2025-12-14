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

#include "CoordinateUsagePass.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "../framework/AnalysisManager.hpp"

namespace analysis {

CoordinateUsageResult
CoordinateUsagePass::run(const std::vector<Token>& tokens,
                         [[maybe_unused]] AnalysisManager& am) {
    CoordinateUsageResult result;
    for (const auto& token : tokens) {
        if (token.type == TokenType::ConstantX) {
            result.uses_x = true;
        }
        if (token.type == TokenType::ConstantY) {
            result.uses_y = true;
        }
        if (result.uses_x && result.uses_y) {
            break;
        }
    }
    return result;
}

} // namespace analysis
