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

#ifndef LLVMEXPR_GLSL_GENERATOR_HPP
#define LLVMEXPR_GLSL_GENERATOR_HPP

#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../analysis/AnalysisResults.hpp"
#include "../../frontend/Tokenizer.hpp"

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
    void debug_emit_cfg_comment();

    int break_flag_counter = 0;
    std::map<int, std::string> loop_break_flags; // loop header -> bool var

    void emit(const std::string& code);
    void emit_line(const std::string& code);
    void emit_newline();
    void indent();
    void dedent();

    void emit_header();
    void emit_buffer_declarations();
    void emit_helper_functions();
    void emit_variable_declarations();
    void emit_main_function();
    void emit_main_function_state_machine();
    void emit_main_function_structured();
    void emit_block_code(int block_idx);
    void emit_store_and_return(const std::string& result_expr);

    struct LoopContext {
        std::vector<int> header_stack;
    };

    [[nodiscard]] const std::vector<analysis::CFGBlock>&
    get_codegen_cfg_blocks() const;
    [[nodiscard]] const std::vector<int>& get_codegen_stack_depth_in() const;
    [[nodiscard]] int compute_branch_join(int t, int f, int stop_block) const;

    [[nodiscard]] std::string get_loop_break_flag(int header);
    [[nodiscard]] std::optional<int>
    find_enclosing_loop_for_follow(int target_block,
                                   const LoopContext& loop_ctx) const;
    void emit_unwind_break_if_needed(const LoopContext& loop_ctx);

    template <typename Visitor>
    bool traverse_structure(int start_block, int stop_block,
                            LoopContext& loop_ctx, Visitor& visitor) const {
        const auto& cfg_blocks = get_codegen_cfg_blocks();
        const auto& structurize = analysis.getStructurizeCFGResult();

        std::set<int> visited_in_region;
        int block = start_block;
        while (block != stop_block) {
            if (block < 0 || static_cast<size_t>(block) >= cfg_blocks.size()) {
                return false;
            }

            // Loop header handling
            if (structurize.isLoopHeader(block) &&
                !is_loop_header_active(block, loop_ctx)) {
                int follow = -1;
                auto it = structurize.loop_follow.find(block);
                if (it != structurize.loop_follow.end()) {
                    follow = it->second;
                }
                if (!visitor.handle_loop(block, follow, loop_ctx)) {
                    return false;
                }
                block = follow;
                continue;
            }

            if (visited_in_region.contains(block)) {
                return false;
            }
            visited_in_region.insert(block);

            if (!visitor.visit_block(block)) {
                return false;
            }

            auto succ = cfg_blocks[block].successors;
            // Treat duplicated successors as a single edge.
            if (succ.size() == 2 && succ[0] == succ[1]) {
                succ.pop_back();
            }

            if (succ.empty()) {
                return visitor.handle_no_successors(block);
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
                        return visitor.handle_loop_exit_or_continue(
                            block, next, current_header, follow);
                    }
                    if (!structurize.inLoop(current_header, next)) {
                        return visitor.handle_nonlocal_edge(
                            block, next, stop_block, loop_ctx);
                    }
                }
                if (next == stop_block) {
                    return true;
                }

                if (!visitor.handle_simple_edge(block, next)) {
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
                int join = compute_branch_join(t, f, stop_block);

                if (!visitor.handle_branch(block, t, f, join, stop_block,
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
                        return visitor.handle_loop_break(join);
                    }
                }

                block = join;
                continue;
            }

            return false;
        }
        return true;
    }

    void emit_structured_from(int start_block, int stop_block,
                              LoopContext& loop_ctx, bool& ok);
    void emit_edge_to_block(int target_block, int stop_block,
                            LoopContext& loop_ctx, bool& ok);
    void emit_stack_to_entry_slots(int target_block);

    [[nodiscard]] bool can_structure_from(int start_block, int stop_block,
                                          LoopContext& loop_ctx) const;
    [[nodiscard]] bool can_edge_to_block(int target_block, int stop_block,
                                         LoopContext& loop_ctx) const;

    [[nodiscard]] int lca_postdom(int a, int b,
                                  const std::vector<int>& ipdom) const;
    [[nodiscard]] bool is_loop_header_active(int header,
                                             const LoopContext& loop_ctx) const;

    void process_token(const Token& token);

    [[nodiscard]] std::string new_temp();
    [[nodiscard]] std::string new_slot();
    [[nodiscard]] std::string pop();
    void push(const std::string& val);
    [[nodiscard]] std::string peek(int offset = 0) const;

    [[nodiscard]] static std::string float_literal(double val);
    [[nodiscard]] std::string binary_op(const std::string& op);
    [[nodiscard]] std::string binary_cmp(const std::string& op);
    [[nodiscard]] std::string unary_fn(const std::string& fn);
    [[nodiscard]] std::string binary_fn(const std::string& fn);

    [[nodiscard]] std::string emit_clamp_coord(const std::string& coord,
                                               const std::string& max_dim);
    [[nodiscard]] std::string emit_mirror_coord(const std::string& coord,
                                                const std::string& max_dim);
    [[nodiscard]] std::string emit_final_coord(const std::string& coord,
                                               const std::string& max_dim,
                                               bool use_mirror);

    [[nodiscard]] std::string emit_pixel_load(int clip_idx,
                                              const std::string& x,
                                              const std::string& y,
                                              bool use_mirror);
    [[nodiscard]] std::string emit_pixel_index(const std::string& x,
                                               const std::string& y);
};

#endif // LLVMEXPR_GLSL_GENERATOR_HPP
