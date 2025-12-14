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

#include "VariableUsagePass.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "../framework/AnalysisManager.hpp"

namespace analysis {

VariableUsageResult
VariableUsagePass::run(const std::vector<Token>& tokens,
                       [[maybe_unused]] AnalysisManager& am) {
    VariableUsageResult result;
    for (const auto& token : tokens) {
        if (token.type == TokenType::VarStore ||
            token.type == TokenType::VarLoad) {
            const auto& payload = std::get<TokenPayloadVar>(token.payload);
            result.all_vars.insert(payload.name);
        }
    }
    return result;
}

} // namespace analysis
