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

#include "ValidationPass.hpp"
#include "../framework/AnalysisError.hpp"
#include "../framework/AnalysisManager.hpp"
#include "../utils/VarNaming.hpp"
#include "BlockAnalysisPass.hpp"
#include "StaticAllocReachabilityPass.hpp"
#include "VarInitPass.hpp"

#include <format>

namespace analysis {

ValidationPass::Result ValidationPass::run(const std::vector<Token>& tokens,
                                           AnalysisManager& am) {
    // Depend on required analysis passes
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    const auto& var_init_result = am.getResult<VarInitPass>();
    const auto& var_init_in = var_init_result.var_init_in;

    const auto& static_alloc_result =
        am.getResult<StaticAllocReachabilityPass>();
    const auto& static_alloc_in = static_alloc_result.static_alloc_in;

    for (size_t i = 0; i < cfg_blocks.size(); ++i) {
        std::set<std::string> defined_in_block = var_init_in[i];
        std::set<std::string> static_in_block = static_alloc_in[i];

        for (int j = cfg_blocks[i].start_token_idx;
             j < cfg_blocks[i].end_token_idx; ++j) {
            const auto& token = tokens[j];

            if (token.type == TokenType::VarLoad) {
                const auto& payload = std::get<TokenPayloadVar>(token.payload);
                if (!defined_in_block.contains(payload.name)) {
                    throw AnalysisError(
                        std::format("Variable is uninitialized: {}",
                                    payload.name),
                        j);
                }
            } else if (token.type == TokenType::VarStore) {
                defined_in_block.insert(
                    std::get<TokenPayloadVar>(token.payload).name);
            } else if (token.type == TokenType::ArrayLoad ||
                       token.type == TokenType::ArrayStore) {
                const auto& payload =
                    std::get<TokenPayloadArrayOp>(token.payload);
                std::string array_name = var_naming::getArrayName(payload.name);
                if (!defined_in_block.contains(array_name)) {
                    throw AnalysisError(
                        std::format("Array is uninitialized: {}", payload.name),
                        j);
                }
            } else if (token.type == TokenType::ArrayAllocStatic ||
                       token.type == TokenType::ArrayAllocDyn) {
                const auto& payload =
                    std::get<TokenPayloadArrayOp>(token.payload);
                std::string array_name = var_naming::getArrayName(payload.name);

                if (static_in_block.contains(array_name)) {
                    throw AnalysisError(
                        std::format("Statically allocated array cannot be "
                                    "reallocated: {}",
                                    payload.name),
                        j);
                }

                if (token.type == TokenType::ArrayAllocStatic) {
                    static_in_block.insert(array_name);
                }
                defined_in_block.insert(array_name);
            }
        }
    }

    return Result{};
}

} // namespace analysis
