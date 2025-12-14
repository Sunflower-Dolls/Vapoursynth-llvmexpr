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

#include "DynamicArrayAllocOptPass.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "../framework/AnalysisManager.hpp"
#include "BlockAnalysisPass.hpp"

#include <map>
#include <queue>
#include <set>
#include <string>
#include <vector>

namespace analysis {

namespace {

int find_block_for_token(int token_idx,
                         const std::vector<CFGBlock>& cfg_blocks) {
    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        if (token_idx >= cfg_blocks[i].start_token_idx &&
            token_idx < cfg_blocks[i].end_token_idx) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool is_array_used_in_range(const std::string& array_name,
                            const std::vector<Token>& tokens,
                            int start_token_idx, int end_token_idx) {
    for (int i = start_token_idx; i < end_token_idx; ++i) {
        const auto& token = tokens[i];
        if (token.type == TokenType::ArrayLoad ||
            token.type == TokenType::ArrayStore) {
            const auto& payload = std::get<TokenPayloadArrayOp>(token.payload);
            if (payload.name == array_name) {
                return true;
            }
        }
    }
    return false;
}

bool is_array_used_between(const std::string& array_name,
                           const std::vector<Token>& tokens,
                           const std::vector<CFGBlock>& cfg_blocks,
                           int start_token_idx, int end_token_idx) {
    int start_block = find_block_for_token(start_token_idx, cfg_blocks);
    int end_block = find_block_for_token(end_token_idx, cfg_blocks);

    if (start_block == -1 || end_block == -1) {
        return true; // Assume used if we can't determine
    }

    if (start_block == end_block) {
        return is_array_used_in_range(array_name, tokens, start_token_idx + 1,
                                      end_token_idx);
    }

    std::queue<int> to_visit;
    std::set<int> visited;

    to_visit.push(start_block);
    visited.insert(start_block);

    while (!to_visit.empty()) {
        int current_block = to_visit.front();
        to_visit.pop();

        const auto& block = cfg_blocks[current_block];

        int check_start = block.start_token_idx;
        int check_end = block.end_token_idx;

        if (current_block == start_block) {
            check_start = start_token_idx + 1;
        }

        if (current_block == end_block) {
            check_end = end_token_idx;
        }

        if (is_array_used_in_range(array_name, tokens, check_start,
                                   check_end)) {
            return true;
        }

        if (current_block == end_block) {
            continue;
        }

        for (int successor : block.successors) {
            if (!visited.contains(successor)) {
                visited.insert(successor);
                to_visit.push(successor);
            }
        }
    }

    return false;
}

} // namespace

PreservedAnalyses DynamicArrayAllocOptPass::run(std::vector<Token>& tokens,
                                                AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    std::set<int> allocations_to_remove;

    std::map<std::string, std::vector<int>> dynamic_allocations;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (token.type == TokenType::ArrayAllocDyn) {
            const auto& payload = std::get<TokenPayloadArrayOp>(token.payload);
            dynamic_allocations[payload.name].push_back(static_cast<int>(i));
        }
    }

    for (const auto& [array_name, alloc_indices] : dynamic_allocations) {
        if (alloc_indices.size() < 2) {
            continue;
        }

        for (size_t i = 0; i < alloc_indices.size() - 1; ++i) {
            int first_alloc = alloc_indices[i];
            int second_alloc = alloc_indices[i + 1];

            bool used = is_array_used_between(array_name, tokens, cfg_blocks,
                                              first_alloc, second_alloc);

            if (!used) {
                allocations_to_remove.insert(first_alloc);
            }
        }
    }

    if (allocations_to_remove.empty()) {
        return PreservedAnalyses::all();
    }

    for (int token_idx : allocations_to_remove) {
        auto& token = tokens[token_idx];
        token.type = TokenType::Drop;
        token.text = "drop1";
        token.payload = TokenPayloadStackOp{.n = 1};
    }

    return PreservedAnalyses::none();
}

} // namespace analysis
