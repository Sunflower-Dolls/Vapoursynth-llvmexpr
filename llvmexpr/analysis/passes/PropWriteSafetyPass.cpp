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
#include <algorithm>
#include <format>
#include <iterator>
#include <map>
#include <set>
#include <vector>

namespace analysis {

namespace {

std::map<int, std::set<int>>
computeDominators(const std::vector<CFGBlock>& cfg_blocks) {
    if (cfg_blocks.empty()) {
        return {};
    }

    size_t num_blocks = cfg_blocks.size();
    std::map<int, std::set<int>> dominators;

    // Initialize Dom(entry) = {entry}
    dominators[0] = {0};

    // Initialize Dom(B) = {all blocks} for B != entry
    std::set<int> all_blocks;
    for (size_t i = 0; i < num_blocks; ++i) {
        all_blocks.insert(static_cast<int>(i));
    }
    for (size_t i = 1; i < num_blocks; ++i) {
        dominators[static_cast<int>(i)] = all_blocks;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (size_t i = 1; i < num_blocks; ++i) {
            const auto& block = cfg_blocks[i];
            if (block.predecessors.empty()) {
                continue; // unreachable blocks
            }

            // new_dom = intersect(Dom(P) for all P in predecessors(B))
            std::set<int> new_dom = dominators.at(block.predecessors[0]);
            for (size_t j = 1; j < block.predecessors.size(); ++j) {
                const auto& pred_dom = dominators.at(block.predecessors[j]);
                std::set<int> intersection;
                std::ranges::set_intersection(
                    new_dom, pred_dom,
                    std::inserter(intersection, intersection.begin()));
                new_dom = std::move(intersection);
            }

            // new_dom = {B} U new_dom
            new_dom.insert(static_cast<int>(i));

            if (new_dom != dominators.at(static_cast<int>(i))) {
                dominators[static_cast<int>(i)] = new_dom;
                changed = true;
            }
        }
    }

    return dominators;
}

} // namespace

PropWriteSafetyPass::Result
PropWriteSafetyPass::run(const std::vector<Token>& tokens,
                         AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    if (cfg_blocks.empty()) {
        return {};
    }

    std::vector<std::pair<int, int>>
        prop_store_locations; // {token_idx, block_idx}
    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        const auto& block = cfg_blocks[i];
        for (int j = block.start_token_idx; j < block.end_token_idx; ++j) {
            if (tokens[j].type == TokenType::PROP_STORE) {
                prop_store_locations.emplace_back(j, static_cast<int>(i));
            }
        }
    }

    if (prop_store_locations.empty()) {
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
        throw AnalysisError(
            "Prop write operations exist but the expression has no "
            "reachable terminal points.",
            prop_store_locations[0].first);
    }

    auto dominators = computeDominators(cfg_blocks);

    for (const auto& [token_idx, block_idx] : prop_store_locations) {
        for (int terminal_block_idx : terminal_blocks) {
            const auto& term_doms = dominators.at(terminal_block_idx);
            if (!term_doms.contains(block_idx)) {
                throw AnalysisError(
                    std::format(
                        "Prop write operation '{}' is not guaranteed to be "
                        "executed on all paths. (idx: {})",
                        tokens[token_idx].text, token_idx),
                    token_idx);
            }
        }
    }

    return {};
}

} // namespace analysis
