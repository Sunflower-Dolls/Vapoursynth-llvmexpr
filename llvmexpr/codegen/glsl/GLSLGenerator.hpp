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
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../../analysis/AnalysisResults.hpp"
#include "../../frontend/Tokenizer.hpp"

/**
 * Control flow is handled using a while+switch state machine pattern where
 * each CFG block becomes a case in the switch statement. This approach
 * handles all control flow patterns uniformly.
 *
 * Variable naming conventions:
 *   t_N  - Temporary values (stack simulation)
 *   u_X  - User-defined variables (from var!)
 *   s_N  - Stack slot variables (for merge points)
 *   a_X  - Array variables
 */
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
    void emit_block_code(int block_idx);

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
