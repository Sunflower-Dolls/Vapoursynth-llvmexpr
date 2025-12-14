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

#ifndef LLVMEXPR_ANALYSIS_RELOOP_PASS_HPP
#define LLVMEXPR_ANALYSIS_RELOOP_PASS_HPP

#include "../framework/DataStructures.hpp"
#include "../framework/Pass.hpp"
#include <map>
#include <set>
#include <vector>

namespace analysis {

struct ReloopResult {
    // Whether we believe the CFG is structurizable.
    bool success = true;

    // If the CFG appears reducible (single-entry strongly-connected regions).
    bool reducible = true;

    // Post-dominator tree (immediate post-dominator) for each CFG block index.
    // -1 = no post-dominator
    std::vector<int> ipdom;

    // Natural-loop information indexed by loop header block index.
    std::map<int, std::set<int>>
        loop_body;                  // header -> blocks in loop (incl hdr)
    std::map<int, int> loop_follow; // header -> follow block (may be -1)

    // For each block, the innermost loop header containing it, or -1.
    std::vector<int> innermost_loop_header;

    // Optional CFG rewrite for codegen (node splitting / tail duplication).
    // If empty, codegen should use the original CFG from BlockAnalysisPass.
    std::vector<CFGBlock> structured_cfg_blocks;

    // Map: structured block index -> original block index.
    // Only valid when structured_cfg_blocks is non-empty.
    std::vector<int> structured_block_origin;

    // Stack depth at entry for structured_cfg_blocks.
    // Only valid when structured_cfg_blocks is non-empty.
    std::vector<int> structured_stack_depth_in;

    [[nodiscard]] bool isLoopHeader(int block_idx) const {
        return loop_body.contains(block_idx);
    }

    [[nodiscard]] bool inLoop(int header, int block_idx) const {
        auto it = loop_body.find(header);
        if (it == loop_body.end()) {
            return false;
        }
        return it->second.contains(block_idx);
    }
};

/**
    Computes CFG structuring information for Relooper-style codegen.
    Responsibilities:
    - Compute post-dominators (for join point selection)
    - Identify natural loops (backedges + loop body closure)
    - Compute loop follow blocks
    Depends on: BuildCFGPass, BlockAnalysisPass
*/
class ReloopPass : public AnalysisPass<ReloopPass, ReloopResult> {
  public:
    using Result = ReloopResult;

    [[nodiscard]] const char* getName() const override {
        return "Reloop (Control Flow Structuring) Pass";
    }

    Result run(const std::vector<Token>& tokens, AnalysisManager& am) override;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_RELOOP_PASS_HPP
