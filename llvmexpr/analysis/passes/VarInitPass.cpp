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

#include "VarInitPass.hpp"
#include "../framework/AnalysisManager.hpp"
#include "../framework/ForwardDataflowAnalysis.hpp"
#include "../utils/VarNaming.hpp"
#include "BlockAnalysisPass.hpp"

#include <algorithm>
#include <iterator>
#include <set>

namespace analysis {

class VarInitAnalysis : public ForwardDataflowAnalysis<std::set<std::string>> {
    const std::set<std::string>& all_vars;

  public:
    explicit VarInitAnalysis(const std::set<std::string>& all_vars)
        : all_vars(all_vars) {}

    std::set<std::string>
    computeGenSet(size_t block_idx, const std::vector<Token>& tokens,
                  const std::vector<CFGBlock>& cfg_blocks) override {
        std::set<std::string> gen_set;
        const auto& block = cfg_blocks[block_idx];
        for (int j = block.start_token_idx; j < block.end_token_idx; ++j) {
            if (tokens[j].type == TokenType::VarStore) {
                gen_set.insert(
                    std::get<TokenPayloadVar>(tokens[j].payload).name);
            } else if (tokens[j].type == TokenType::ArrayAllocStatic ||
                       tokens[j].type == TokenType::ArrayAllocDyn) {
                const auto& array_name =
                    std::get<TokenPayloadArrayOp>(tokens[j].payload).name;
                gen_set.insert(var_naming::getArrayName(array_name));
            }
        }
        return gen_set;
    }

    std::set<std::string>
    meetOperation(const std::vector<std::set<std::string>>& inputs) override {
        if (inputs.empty()) {
            return {};
        }

        std::set<std::string> result = inputs[0];
        for (size_t i = 1; i < inputs.size(); ++i) {
            std::set<std::string> temp_intersect;
            std::set_intersection(
                result.begin(), result.end(), inputs[i].begin(),
                inputs[i].end(),
                std::inserter(temp_intersect, temp_intersect.begin()));
            result = std::move(temp_intersect);
        }
        return result;
    }

    std::set<std::string>
    transferFunction(const std::set<std::string>& in_value,
                     const std::set<std::string>& gen_set) override {
        std::set<std::string> out_value = gen_set;
        out_value.insert(in_value.begin(), in_value.end());
        return out_value;
    }

    std::set<std::string> getBoundaryValue() override { return {}; }

    std::set<std::string> getInitialOutValue() override { return all_vars; }
};

VarInitPass::Result VarInitPass::run(const std::vector<Token>& tokens,
                                     AnalysisManager& am) {
    const auto& block_result = am.getResult<BlockAnalysisPass>();
    const auto& cfg_blocks = block_result.cfg_blocks;

    // Collect all variables and arrays
    std::set<std::string> all_vars;
    for (const auto& token : tokens) {
        if (token.type == TokenType::VarStore ||
            token.type == TokenType::VarLoad) {
            all_vars.insert(std::get<TokenPayloadVar>(token.payload).name);
        } else if (token.type == TokenType::ArrayAllocStatic ||
                   token.type == TokenType::ArrayAllocDyn ||
                   token.type == TokenType::ArrayStore ||
                   token.type == TokenType::ArrayLoad) {
            const auto& array_name =
                std::get<TokenPayloadArrayOp>(token.payload).name;
            all_vars.insert(var_naming::getArrayName(array_name));
        }
    }

    VarInitAnalysis analysis(all_vars);
    auto [in_sets, out_sets] = analysis.analyze(tokens, cfg_blocks);

    Result result;
    result.var_init_in = std::move(in_sets);
    result.var_init_out = std::move(out_sets);

    return result;
}

} // namespace analysis
