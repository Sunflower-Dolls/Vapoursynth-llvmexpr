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

#include "StaticAllocReachabilityPass.hpp"
#include "../framework/AnalysisManager.hpp"
#include "../framework/ForwardDataflowAnalysis.hpp"
#include "../utils/VarNaming.hpp"
#include "BlockAnalysisPass.hpp"

namespace analysis {

class StaticAllocReachabilityAnalysis
    : public ForwardDataflowAnalysis<std::set<std::string>> {
  public:
    std::set<std::string>
    computeGenSet(size_t block_idx, const std::vector<Token>& tokens,
                  const std::vector<CFGBlock>& cfg_blocks) override {
        std::set<std::string> gen_set;
        const auto& block = cfg_blocks[block_idx];

        for (int j = block.start_token_idx; j < block.end_token_idx; ++j) {
            if (tokens[j].type == TokenType::ArrayAllocStatic) {
                const auto& array_name =
                    std::get<TokenPayloadArrayOp>(tokens[j].payload).name;
                gen_set.insert(var_naming::getArrayName(array_name));
            }
        }
        return gen_set;
    }

    std::set<std::string>
    meetOperation(const std::vector<std::set<std::string>>& inputs) override {
        // Union: an array is statically allocated if allocated on ANY path
        std::set<std::string> result;
        for (const auto& input : inputs) {
            result.insert(input.begin(), input.end());
        }
        return result;
    }

    std::set<std::string>
    transferFunction(const std::set<std::string>& in_value,
                     const std::set<std::string>& gen_set) override {
        // OUT = GEN U IN
        std::set<std::string> out_value = gen_set;
        out_value.insert(in_value.begin(), in_value.end());
        return out_value;
    }

    std::set<std::string> getBoundaryValue() override {
        // Entry block starts with no statically allocated arrays
        return std::set<std::string>{};
    }
};

StaticAllocReachabilityPass::Result
StaticAllocReachabilityPass::run(const std::vector<Token>& tokens,
                                 AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    Result result;

    StaticAllocReachabilityAnalysis analysis;
    auto [in_sets, out_sets] = analysis.analyze(tokens, cfg_blocks);

    result.static_alloc_in = std::move(in_sets);
    result.static_alloc_out = std::move(out_sets);

    return result;
}

} // namespace analysis
