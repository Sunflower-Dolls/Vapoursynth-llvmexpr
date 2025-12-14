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

#include "StaticArrayOptPass.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "../framework/AnalysisManager.hpp"
#include "BlockAnalysisPass.hpp"
#include "ConstPropPass.hpp"

#include <algorithm>
#include <map>
#include <optional>
#include <ranges>

namespace {
constexpr int SINGLEEXPR_STACK_ALLOC_THRESHOLD = 1024;
} // namespace

namespace analysis {

PreservedAnalyses StaticArrayOptPass::run(std::vector<Token>& tokens,
                                          AnalysisManager& am) {
    const auto& const_result = am.getResult<ConstPropPass>();
    const auto& in_stacks = const_result.const_stack_in;

    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    std::map<std::string, int> array_alloc_count;
    for (const auto& token : tokens) {
        if (token.type == TokenType::ArrayAllocStatic ||
            token.type == TokenType::ArrayAllocDyn) {
            const auto& payload = std::get<TokenPayloadArrayOp>(token.payload);
            array_alloc_count[payload.name]++;
        }
    }

    std::map<int, int> dyn_to_static;

    for (size_t block_idx = 0; block_idx < cfg_blocks.size(); ++block_idx) {
        const auto& block = cfg_blocks[block_idx];

        auto current_stack = in_stacks[block_idx];

        for (int token_idx = block.start_token_idx;
             token_idx < block.end_token_idx; ++token_idx) {
            const auto& token = tokens[token_idx];
            const auto behavior = get_token_behavior(token);

            while (current_stack.size() < static_cast<size_t>(behavior.arity)) {
                current_stack.insert(current_stack.begin(), std::nullopt);
            }

            std::vector<std::optional<double>> args;
            for (int k = 0; k < behavior.arity; ++k) {
                args.push_back(current_stack.back());
                current_stack.pop_back();
            }
            std::ranges::reverse(args);

            if (token.type == TokenType::ArrayAllocDyn) {
                const auto& payload =
                    std::get<TokenPayloadArrayOp>(token.payload);
                // Convert to static allocation if:
                // 1. Size is a compile-time constant (from ConstPropPass)
                // 2. Size <= threshold
                // 3. Only allocated once
                if (args[0].has_value() &&
                    array_alloc_count[payload.name] == 1) {
                    int size = static_cast<int>(args[0].value());
                    if (size > 0 && size <= SINGLEEXPR_STACK_ALLOC_THRESHOLD) {
                        dyn_to_static[token_idx] = size;
                    }
                }
            }

            for (int k = 0; k < behavior.arity + behavior.stack_effect; ++k) {
                current_stack.emplace_back(std::nullopt);
            }
        }
    }

    std::vector<std::pair<int, Token>> insertions; // position, token to insert

    for (auto& it : std::ranges::reverse_view(dyn_to_static)) {
        int token_idx = it.first;
        int size = it.second;

        auto& token = tokens[token_idx];
        auto& payload = std::get<TokenPayloadArrayOp>(token.payload);

        token.type = TokenType::ArrayAllocStatic;
        token.text = payload.name + "{}^" + std::to_string(size);
        payload.static_size = size;

        Token drop_token{.type = TokenType::Drop,
                         .text = "drop1",
                         .payload = TokenPayloadStackOp{.n = 1}};

        insertions.emplace_back(token_idx, drop_token);
    }

    for (const auto& [pos, drop_token] : insertions) {
        tokens.insert(tokens.begin() + pos, drop_token);
    }

    if (!dyn_to_static.empty()) {
        return PreservedAnalyses::none();
    }

    return PreservedAnalyses::all();
}

} // namespace analysis
