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

#ifndef LLVMEXPR_CODEGEN_LLVM_GLSL_GENERATOR_HPP
#define LLVMEXPR_CODEGEN_LLVM_GLSL_GENERATOR_HPP

#include "../../analysis/AnalysisResults.hpp"
#include "../../frontend/Tokenizer.hpp"

#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

class GLSLGenerator {
  public:
    GLSLGenerator(const std::vector<Token>& tokens, int num_inputs, int width,
                  int height, bool mirror_boundary,
                  const std::map<std::pair<int, std::string>, int>& prop_map,
                  const analysis::ExpressionAnalysisResults& analysis_results);

    [[nodiscard]] std::string generate();

  private:
    const std::vector<Token>& tokens;
    int num_inputs;
    bool mirror_boundary;
    const std::map<std::pair<int, std::string>, int>& prop_map;
    const analysis::ExpressionAnalysisResults& analysis;

    std::ostringstream out;

    int indent_level = 0;

    std::vector<std::string> stack;

    int temp_counter = 0;

    int slot_counter = 0;

    std::set<std::string> user_variables;

    // Array name -> static size
    std::map<std::string, int> arrays;

    // Block -> entry stack variable names (for merge points)
    std::map<int, std::vector<std::string>> block_entry_stack;

#ifndef NDEBUG
    bool debug_structurize_cfg = false;
#endif
    void debugEmitCfgComment();

    int break_flag_counter = 0;
    std::map<int, std::string> loop_break_flags; // loop header -> bool var

    void emit(const std::string& code);
    void emitLine(const std::string& code);
    void emitNewline();
    void indent();
    void dedent();

    void emitHeader();
    void emitBufferDeclarations();
    void emitHelperFunctions();
    void emitVariableDeclarations();
    void emitMainFunction();
    void emitMainFunctionStateMachine();
    void emitMainFunctionStructured();
    void emitBlockCode(int block_idx);
    void emitStoreAndReturn(const std::string& result_expr);

    struct LoopContext {
        std::vector<int> header_stack;
    };

    [[nodiscard]] const std::vector<analysis::CFGBlock>&
    getCodegenCfgBlocks() const;
    [[nodiscard]] const std::vector<int>& getCodegenStackDepthIn() const;
    [[nodiscard]] int computeBranchJoin(int t, int f, int stop_block) const;

    [[nodiscard]] std::string getLoopBreakFlag(int header);
    [[nodiscard]] std::optional<int>
    findEnclosingLoopForFollow(int target_block,
                               const LoopContext& loop_ctx) const;
    void emitUnwindBreakIfNeeded(const LoopContext& loop_ctx);

    template <typename Visitor>
    bool traverseStructure(int start_block, int stop_block,
                           LoopContext& loop_ctx, Visitor& visitor) const {
        const auto& cfg_blocks = getCodegenCfgBlocks();
        const auto& structurize = analysis.getStructurizeCFGResult();

        std::set<int> visited_in_region;
        int block = start_block;
        while (block != stop_block) {
            if (block < 0 || static_cast<size_t>(block) >= cfg_blocks.size()) {
                return false;
            }

            // Loop header handling
            if (structurize.isLoopHeader(block) &&
                !isLoopHeaderActive(block, loop_ctx)) {
                int follow = -1;
                auto it = structurize.loop_follow.find(block);
                if (it != structurize.loop_follow.end()) {
                    follow = it->second;
                }
                if (!visitor.handleLoop(block, follow, loop_ctx)) {
                    return false;
                }
                block = follow;
                continue;
            }

            if (visited_in_region.contains(block)) {
                return false;
            }
            visited_in_region.insert(block);

            if (!visitor.visitBlock(block)) {
                return false;
            }

            auto succ = cfg_blocks[block].successors;
            // Treat duplicated successors as a single edge.
            if (succ.size() == 2 && succ[0] == succ[1]) {
                succ.pop_back();
            }

            if (succ.empty()) {
                return visitor.handleNoSuccessors(block);
            }

            if (succ.size() == 1) {
                int next = succ[0];
                int current_header = loop_ctx.header_stack.empty()
                                         ? -1
                                         : loop_ctx.header_stack.back();
                if (current_header != -1) {
                    int follow = -1;
                    auto it = structurize.loop_follow.find(current_header);
                    if (it != structurize.loop_follow.end()) {
                        follow = it->second;
                    }
                    if (next == current_header || next == follow) {
                        return visitor.handleLoopExitOrContinue(
                            block, next, current_header, follow);
                    }
                    if (!structurize.inLoop(current_header, next)) {
                        return visitor.handleNonlocalEdge(block, next,
                                                          stop_block, loop_ctx);
                    }
                }
                if (next == stop_block) {
                    return true;
                }

                if (!visitor.handleSimpleEdge(block, next)) {
                    return false;
                }
                block = next;
                continue;
            }

            if (succ.size() == 2) {
                int t = succ[0];
                int f = succ[1];
                // Conditional goto:
                //   if (cond > 0) goto t;
                //   fallthrough continues at f.
                int join = computeBranchJoin(t, f, stop_block);

                if (!visitor.handleBranch(block, t, f, join, stop_block,
                                          loop_ctx)) {
                    return false;
                }

                int current_header = loop_ctx.header_stack.empty()
                                         ? -1
                                         : loop_ctx.header_stack.back();
                if (current_header != -1) {
                    int follow = -1;
                    auto it = structurize.loop_follow.find(current_header);
                    if (it != structurize.loop_follow.end()) {
                        follow = it->second;
                    }
                    if (join == stop_block && stop_block == follow) {
                        return visitor.handleLoopBreak(join);
                    }
                }

                block = join;
                continue;
            }

            return false;
        }
        return true;
    }

    void emitStructuredFrom(int start_block, int stop_block,
                            LoopContext& loop_ctx, bool& ok);
    void emitEdgeToBlock(int target_block, int stop_block,
                         LoopContext& loop_ctx, bool& ok);
    void emitStackToEntrySlots(int target_block);

    [[nodiscard]] bool canStructureFrom(int start_block, int stop_block,
                                        LoopContext& loop_ctx) const;
    [[nodiscard]] bool canEdgeToBlock(int target_block, int stop_block,
                                      LoopContext& loop_ctx) const;

    [[nodiscard]] int lcaPostdom(int a, int b,
                                 const std::vector<int>& ipdom) const;
    [[nodiscard]] bool isLoopHeaderActive(int header,
                                          const LoopContext& loop_ctx) const;

    void processToken(const Token& token);

    [[nodiscard]] std::string newTemp();
    [[nodiscard]] std::string newSlot();
    [[nodiscard]] std::string pop();
    void push(const std::string& val);
    [[nodiscard]] std::string peek(int offset = 0) const;

    [[nodiscard]] static std::string floatLiteral(double val);
    [[nodiscard]] std::string binaryOp(const std::string& op);
    [[nodiscard]] std::string binaryCmp(const std::string& op);
    [[nodiscard]] std::string unaryFn(const std::string& fn);
    [[nodiscard]] std::string binaryFn(const std::string& fn);

    [[nodiscard]] std::string emitClampCoord(const std::string& coord,
                                             const std::string& max_dim);
    [[nodiscard]] std::string emitMirrorCoord(const std::string& coord,
                                              const std::string& max_dim);
    [[nodiscard]] std::string emitFinalCoord(const std::string& coord,
                                             const std::string& max_dim,
                                             bool use_mirror);

    [[nodiscard]] std::string emitPixelLoad(int clip_idx, const std::string& x,
                                            const std::string& y,
                                            bool use_mirror);
    [[nodiscard]] std::string emitPixelIndex(const std::string& x,
                                             const std::string& y);
};

#endif // LLVMEXPR_CODEGEN_LLVM_GLSL_GENERATOR_HPP
