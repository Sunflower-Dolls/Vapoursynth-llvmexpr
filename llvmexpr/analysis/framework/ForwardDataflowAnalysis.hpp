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

#ifndef LLVMEXPR_ANALYSIS_FRAMEWORK_FORWARD_DATAFLOW_ANALYSIS_HPP
#define LLVMEXPR_ANALYSIS_FRAMEWORK_FORWARD_DATAFLOW_ANALYSIS_HPP

#include "../../frontend/Tokenizer.hpp"
#include "DataStructures.hpp"

#include <algorithm>
#include <iterator>
#include <vector>

namespace analysis {

template <typename DomainT> class ForwardDataflowAnalysis {
  public:
    ForwardDataflowAnalysis() = default;
    virtual ~ForwardDataflowAnalysis() = default;
    ForwardDataflowAnalysis(const ForwardDataflowAnalysis&) = default;
    ForwardDataflowAnalysis&
    operator=(const ForwardDataflowAnalysis&) = default;
    ForwardDataflowAnalysis(ForwardDataflowAnalysis&&) = default;
    ForwardDataflowAnalysis& operator=(ForwardDataflowAnalysis&&) = default;

    virtual DomainT computeGenSet(size_t block_idx,
                                  const std::vector<Token>& tokens,
                                  const std::vector<CFGBlock>& cfg_blocks) = 0;

    virtual DomainT meetOperation(const std::vector<DomainT>& inputs) = 0;

    virtual DomainT transferFunction(const DomainT& in_value,
                                     const DomainT& gen_set) = 0;

    virtual DomainT getBoundaryValue() = 0;

    virtual DomainT getInitialOutValue() { return DomainT{}; }

    std::pair<std::vector<DomainT>, std::vector<DomainT>>
    analyze(const std::vector<Token>& tokens,
            const std::vector<CFGBlock>& cfg_blocks) {
        const size_t num_blocks = cfg_blocks.size();

        std::vector<DomainT> gen_sets(num_blocks);
        for (size_t i = 0; i < num_blocks; ++i) {
            gen_sets[i] = computeGenSet(i, tokens, cfg_blocks);
        }

        std::vector<DomainT> in_sets(num_blocks);
        std::vector<DomainT> out_sets(num_blocks, getInitialOutValue());

        if (!cfg_blocks.empty()) {
            in_sets[0] = getBoundaryValue();
        }

        bool changed = true;
        while (changed) {
            changed = false;

            for (size_t i = 0; i < num_blocks; ++i) {
                DomainT new_in;
                if (i == 0 || cfg_blocks[i].predecessors.empty()) {
                    new_in = getBoundaryValue();
                } else {
                    std::vector<DomainT> pred_outs;
                    pred_outs.reserve(cfg_blocks[i].predecessors.size());
                    std::transform(cfg_blocks[i].predecessors.begin(),
                                   cfg_blocks[i].predecessors.end(),
                                   std::back_inserter(pred_outs),
                                   [&out_sets](int pred_idx) {
                                       return out_sets[pred_idx];
                                   });
                    new_in = meetOperation(pred_outs);
                }

                if (new_in != in_sets[i]) {
                    in_sets[i] = std::move(new_in);
                }

                DomainT new_out = transferFunction(in_sets[i], gen_sets[i]);

                if (new_out != out_sets[i]) {
                    out_sets[i] = std::move(new_out);
                    changed = true;
                }
            }
        }

        return {std::move(in_sets), std::move(out_sets)};
    }
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_FRAMEWORK_FORWARD_DATAFLOW_ANALYSIS_HPP
