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

#include "PropWriteSafetyPass.hpp"
#include "../framework/AnalysisError.hpp"
#include "../framework/AnalysisManager.hpp"
#include "BlockAnalysisPass.hpp"
#include "StackSafetyPass.hpp"
#include <format>
#include <map>
#include <set>
#include <vector>

namespace analysis {

PropWriteSafetyPass::Result
PropWriteSafetyPass::run(const std::vector<Token>& tokens,
                         AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    if (cfg_blocks.empty()) {
        return {};
    }

    std::map<std::string, std::vector<std::pair<int, int>>>
        prop_writes_by_name; // {prop_name: [{token_idx, block_idx}]}
    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        const auto& block = cfg_blocks[i];
        for (int j = block.start_token_idx; j < block.end_token_idx; ++j) {
            if (tokens[j].type == TokenType::PROP_STORE) {
                const auto& payload =
                    std::get<TokenPayload_PropStore>(tokens[j].payload);
                prop_writes_by_name[payload.prop_name].emplace_back(
                    j, static_cast<int>(i));
            }
        }
    }

    if (prop_writes_by_name.empty()) {
        return {};
    }

    // Find reachable terminal blocks
    const auto& stack_safety_result = am.getResult<StackSafetyPass>();
    std::vector<int> terminal_blocks;
    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        if (stack_safety_result.stack_depth_in[i] != -1 &&
            cfg_blocks[i].successors.empty()) {
            terminal_blocks.push_back(static_cast<int>(i));
        }
    }

    if (terminal_blocks.empty()) {
        const auto& first_prop_writes = prop_writes_by_name.begin()->second;
        throw AnalysisError(
            "Prop write operations exist but the expression has no "
            "reachable terminal points.",
            first_prop_writes[0].first);
    }

    for (const auto& [prop_name, locations] : prop_writes_by_name) {
        std::set<int> write_blocks;
        for (const auto& loc : locations) {
            write_blocks.insert(loc.second);
        }

        std::set<int> visited;
        std::vector<int> queue;

        if (!write_blocks.contains(0)) {
            queue.push_back(0);
            visited.insert(0);
        }

        size_t head = 0;
        while (head < queue.size()) {
            int current_block_idx = queue[head++];
            const auto& current_block = cfg_blocks[current_block_idx];

            for (int successor_idx : current_block.successors) {
                if (!visited.contains(successor_idx) &&
                    !write_blocks.contains(successor_idx)) {
                    visited.insert(successor_idx);
                    queue.push_back(successor_idx);
                }
            }
        }

        for (int terminal_block_idx : terminal_blocks) {
            if (visited.contains(terminal_block_idx)) {
                throw AnalysisError(
                    std::format("Prop write to '{}' is not guaranteed to be "
                                "executed on all paths.",
                                prop_name),
                    locations[0].first); // Report error at first occurrence
            }
        }
    }

    return {};
}

} // namespace analysis
