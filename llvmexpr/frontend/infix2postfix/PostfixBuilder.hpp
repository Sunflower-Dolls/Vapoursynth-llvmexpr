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

#ifndef LLVMEXPR_INFIX2POSTFIX_POSTFIXBUILDER_HPP
#define LLVMEXPR_INFIX2POSTFIX_POSTFIXBUILDER_HPP

#include "types.hpp"
#include <string>
#include <vector>

namespace infix2postfix {

class PostfixBuilder {
  public:
    PostfixBuilder() = default;

    // General
    void add_op(TokenType type);
    void add_unary_op(TokenType type);
    void add_ternary_op();
    void add_function_call(const std::string& func_name);
    void append(const PostfixBuilder& other);
    [[nodiscard]] std::string get_expression() const;
    void clear();
    [[nodiscard]] bool empty() const;

    // Literals & Variables
    void add_number(const std::string& num_literal);
    void add_constant(const std::string& const_name);
    void add_variable_load(const std::string& var_name);
    void add_variable_store(const std::string& var_name);

    // Control Flow
    void add_label(const std::string& label_name);
    void add_conditional_jump(const std::string& label_name);
    void add_unconditional_jump(const std::string& label_name);
    void prefix_labels(const std::string& prefix);

    // Data Access & I/O
    void add_prop_access(const std::string& clip_name,
                         const std::string& prop_name);
    void add_set_prop(const std::string& prop_name, const std::string& suffix);
    void add_delete_prop(const std::string& prop_name);
    void add_static_pixel_access(const std::string& clip_name,
                                 const std::string& x, const std::string& y,
                                 const std::string& suffix);
    void add_dyn_pixel_access_expr(const std::string& clip_name,
                                   const std::string& suffix);
    void add_dyn_pixel_access_single(const std::string& clip_name,
                                     const std::string& plane);
    void add_store_expr();
    void add_store_single(const std::string& plane);
    void add_frame_dimension(const std::string& dim, const std::string& plane);
    void add_exit_marker();

    // Stack manipulation
    void add_dropN(int count = 1);
    void add_dupN(int count = 0);
    void add_swapN(int count = 1);
    void add_sortN(int count);

    // Array operations
    void add_array_alloc_static(const std::string& array_name,
                                const std::string& size);
    void add_array_alloc_dynamic(const std::string& array_name);
    void add_array_load(const std::string& array_name);
    void add_array_store(const std::string& array_name);

    // For raw/unstructured parts
    void add_raw(const std::string& raw_string);

  private:
    void push_token(const std::string& token);
    std::vector<std::string> tokens;
};

} // namespace infix2postfix

#endif
