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

#include "BuildCFGPass.hpp"
#include "../framework/AnalysisError.hpp"
#include "../framework/AnalysisManager.hpp"

#include <format>

namespace analysis {

BuildCFGPass::Result BuildCFGPass::run(const std::vector<Token>& tokens,
                                       AnalysisManager& /* am */) {
    Result result;
    std::map<int, int> token_idx_to_block_idx;

    int current_token_idx = 0;
    while (static_cast<size_t>(current_token_idx) < tokens.size()) {
        int block_idx = static_cast<int>(result.cfg_blocks.size());
        CFGBlock block;
        block.start_token_idx = current_token_idx;

        int block_start_idx = current_token_idx;
        token_idx_to_block_idx[block_start_idx] = block_idx;

        // Register label if this block starts with one
        if (tokens[current_token_idx].type == TokenType::LabelDef) {
            const auto& payload =
                std::get<TokenPayloadLabel>(tokens[current_token_idx].payload);
            if (result.label_to_block_idx.contains(payload.name)) {
                throw AnalysisError(
                    std::format("Duplicate label: {}", payload.name),
                    current_token_idx);
            }
            result.label_to_block_idx[payload.name] = block_idx;
        }

        // End of block: hit a jump or another label
        int scan_idx = current_token_idx;
        while (static_cast<size_t>(scan_idx) < tokens.size()) {
            const auto& token = tokens[scan_idx];
            if (token.type == TokenType::Jump) {
                scan_idx++;
                break;
            }
            if (scan_idx > block_start_idx &&
                token.type == TokenType::LabelDef) {
                break;
            }
            scan_idx++;
        }
        block.end_token_idx = scan_idx;
        result.cfg_blocks.push_back(block);
        current_token_idx = scan_idx;
    }

    // Build successor/predecessor relationships
    for (size_t i = 0; i < result.cfg_blocks.size(); ++i) {
        CFGBlock& block = result.cfg_blocks[i];
        const auto& last_token = tokens[block.end_token_idx - 1];

        if (last_token.type == TokenType::Jump) {
            const auto& payload =
                std::get<TokenPayloadLabel>(last_token.payload);

            if (!result.label_to_block_idx.contains(payload.name)) {
                throw AnalysisError(
                    std::format("Undefined label for jump: {}", payload.name),
                    block.end_token_idx - 1);
            }

            int target_block_idx = result.label_to_block_idx.at(payload.name);
            block.successors.push_back(target_block_idx);
            result.cfg_blocks[target_block_idx].predecessors.push_back(
                static_cast<int>(i));

            if (static_cast<size_t>(block.end_token_idx) < tokens.size()) {
                int fallthrough_block_idx =
                    token_idx_to_block_idx.at(block.end_token_idx);
                block.successors.push_back(fallthrough_block_idx);
                result.cfg_blocks[fallthrough_block_idx].predecessors.push_back(
                    static_cast<int>(i));
            }
        } else {
            if (static_cast<size_t>(block.end_token_idx) < tokens.size()) {
                int fallthrough_block_idx =
                    token_idx_to_block_idx.at(block.end_token_idx);
                block.successors.push_back(fallthrough_block_idx);
                result.cfg_blocks[fallthrough_block_idx].predecessors.push_back(
                    static_cast<int>(i));
            }
        }
    }

    return result;
}

} // namespace analysis
