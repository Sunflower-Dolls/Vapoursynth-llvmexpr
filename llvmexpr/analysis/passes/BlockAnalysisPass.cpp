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

#include "BlockAnalysisPass.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "../framework/AnalysisManager.hpp"
#include "BuildCFGPass.hpp"

#include <algorithm>

namespace analysis {

BlockAnalysisPass::Result
BlockAnalysisPass::run(const std::vector<Token>& tokens, AnalysisManager& am) {
    const auto& cfg_result = am.getResult<BuildCFGPass>();

    Result result;
    result.cfg_blocks = cfg_result.cfg_blocks;

    for (auto& block : result.cfg_blocks) {
        int current_stack = 0;
        int min_stack_in_block = 0;

        for (int j = block.start_token_idx; j < block.end_token_idx; ++j) {
            const auto& token = tokens[j];
            const auto behavior = get_token_behavior(token);

            int items_needed = behavior.arity;
            if (current_stack < items_needed) {
                min_stack_in_block =
                    std::max(min_stack_in_block, items_needed - current_stack);
            }

            current_stack += behavior.stack_effect;
        }

        block.stack_effect = current_stack;
        block.min_stack_needed = min_stack_in_block;
    }

    return result;
}

} // namespace analysis
