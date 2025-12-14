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

#include "StackSafetyPass.hpp"
#include "../framework/AnalysisError.hpp"
#include "../framework/AnalysisManager.hpp"
#include "BlockAnalysisPass.hpp"

#include <format>

namespace analysis {

StackSafetyPass::Result StackSafetyPass::run(const std::vector<Token>& tokens,
                                             AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    int expected_final_depth = am.getExpectedFinalDepth();

    Result result;
    result.stack_depth_in.assign(cfg_blocks.size(), -1);

    std::vector<int> worklist;

    if (!cfg_blocks.empty()) {
        result.stack_depth_in[0] = 0;
        worklist.push_back(0);
    }

    size_t processed_count = 0;
    while (!worklist.empty()) {
        processed_count++;
        if (processed_count >
            cfg_blocks.size() * cfg_blocks.size()) { // Heuristic
            throw AnalysisError(std::format(
                "Failed to prove stack safety; check loops. "
                "processed_count = {}, cfg_blocks = {}, heuristic_limit = {}, "
                "worklist_size = {}, next_block_candidate = {}",
                processed_count, cfg_blocks.size(),
                cfg_blocks.size() * cfg_blocks.size(), worklist.size(),
                worklist.back()));
        }

        int block_idx = worklist.back();
        worklist.pop_back();

        const auto& block = cfg_blocks[block_idx];
        int depth_in = result.stack_depth_in[block_idx];

        if (depth_in < block.min_stack_needed) {
            throw AnalysisError(
                std::format("Stack underflow before executing block {}: "
                            "depth_in = {}, min_needed = {}, start token '{}'",
                            block_idx, depth_in, block.min_stack_needed,
                            tokens[block.start_token_idx].text),
                block.start_token_idx);
        }

        int depth_out = depth_in + block.stack_effect;

        for (int succ_idx : block.successors) {
            if (result.stack_depth_in[succ_idx] == -1) {
                result.stack_depth_in[succ_idx] = depth_out;
                worklist.push_back(succ_idx);
            } else if (result.stack_depth_in[succ_idx] != depth_out) {
                throw AnalysisError(
                    std::format(
                        "Stack depth mismatch on converging paths: "
                        "block {} -> successor {}. "
                        "incoming depth = {}, recorded successor depth = {}, "
                        "current start token '{}', successor start token '{}'",
                        block_idx, succ_idx, depth_out,
                        result.stack_depth_in[succ_idx],
                        tokens[block.start_token_idx].text,
                        tokens[cfg_blocks[succ_idx].start_token_idx].text),
                    cfg_blocks[succ_idx].start_token_idx);
            }
        }
    }

    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        if (result.stack_depth_in[i] != -1 &&
            cfg_blocks[i].successors.empty()) { // Reachable terminal block
            int final_depth =
                result.stack_depth_in[i] + cfg_blocks[i].stack_effect;
            if (final_depth != expected_final_depth) {
                throw AnalysisError(
                    std::format(
                        "Expression stack not balanced on "
                        "reachable terminal block {}: "
                        "final depth = {}, expected = {}, start token '{}'",
                        i, final_depth, expected_final_depth,
                        tokens[cfg_blocks[i].start_token_idx].text),
                    cfg_blocks[i].start_token_idx);
            }
        }
    }

    return result;
}

} // namespace analysis
