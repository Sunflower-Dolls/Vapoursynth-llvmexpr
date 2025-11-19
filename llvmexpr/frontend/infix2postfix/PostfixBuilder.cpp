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

#include "PostfixBuilder.hpp"
#include <cctype>
#include <format>
#include <sstream>

namespace infix2postfix {

void PostfixBuilder::push_token(const std::string& token) {
    if (!token.empty()) {
        tokens.push_back(token);
    }
}

void PostfixBuilder::add_op(TokenType type) {
    switch (type) {
    case TokenType::Plus:
        push_token("+");
        break;
    case TokenType::Minus:
        push_token("-");
        break;
    case TokenType::Star:
        push_token("*");
        break;
    case TokenType::Slash:
        push_token("/");
        break;
    case TokenType::Percent:
        push_token("%");
        break;
    case TokenType::StarStar:
        push_token("pow");
        break;
    case TokenType::LogicalAnd:
        push_token("and");
        break;
    case TokenType::LogicalOr:
        push_token("or");
        break;
    case TokenType::BitAnd:
        push_token("bitand");
        break;
    case TokenType::BitOr:
        push_token("bitor");
        break;
    case TokenType::BitXor:
        push_token("bitxor");
        break;
    case TokenType::Eq:
        push_token("=");
        break;
    case TokenType::Ne:
        push_token("=");
        push_token("not");
        break;
    case TokenType::Lt:
        push_token("<");
        break;
    case TokenType::Le:
        push_token("<=");
        break;
    case TokenType::Gt:
        push_token(">");
        break;
    case TokenType::Ge:
        push_token(">=");
        break;
    default:
        std::unreachable();
    }
}

void PostfixBuilder::add_unary_op(TokenType type) {
    switch (type) {
    case TokenType::Minus:
        push_token("neg");
        break;
    case TokenType::Not:
        push_token("not");
        break;
    case TokenType::BitNot:
        push_token("bitnot");
        break;
    default:
        std::unreachable();
    }
}

void PostfixBuilder::add_ternary_op() { push_token("?"); }

void PostfixBuilder::add_function_call(const std::string& func_name) {
    push_token(func_name);
}

void PostfixBuilder::add_number(const std::string& num_literal) {
    push_token(num_literal);
}

void PostfixBuilder::add_constant(const std::string& const_name) {
    push_token(const_name);
}

void PostfixBuilder::add_variable_load(const std::string& var_name) {
    push_token(var_name + "@");
}

void PostfixBuilder::add_variable_store(const std::string& var_name) {
    push_token(var_name + "!");
}

void PostfixBuilder::add_label(const std::string& label_name) {
    push_token("#" + label_name);
}

void PostfixBuilder::add_conditional_jump(const std::string& label_name) {
    push_token(label_name + "#");
}

void PostfixBuilder::add_unconditional_jump(const std::string& label_name) {
    push_token("1");
    add_conditional_jump(label_name);
}

void PostfixBuilder::add_prop_access(const std::string& clip_name,
                                     const std::string& prop_name) {
    push_token(std::format("{}.{}", clip_name, prop_name));
}

void PostfixBuilder::add_prop_exist(const std::string& clip_name,
                                    const std::string& prop_name) {
    push_token(std::format("{}.{}?", clip_name, prop_name));
}

void PostfixBuilder::add_set_prop(const std::string& prop_name,
                                  const std::string& suffix) {
    push_token(std::format("{}${}", prop_name, suffix));
}

void PostfixBuilder::add_delete_prop(const std::string& prop_name) {
    push_token(std::format("{}$d", prop_name));
}

void PostfixBuilder::add_static_pixel_access(const std::string& clip_name,
                                             const std::string& x,
                                             const std::string& y,
                                             const std::string& suffix) {
    push_token(std::format("{}[{},{}]{}", clip_name, x, y, suffix));
}

void PostfixBuilder::add_dyn_pixel_access_expr(const std::string& clip_name,
                                               const std::string& suffix) {
    push_token(std::format("{}[]{}", clip_name, suffix));
}

void PostfixBuilder::add_dyn_pixel_access_single(const std::string& clip_name,
                                                 const std::string& plane) {
    push_token(std::format("{}^{}[]", clip_name, plane));
}

void PostfixBuilder::add_store_expr() { push_token("@[]"); }

void PostfixBuilder::add_store_single(const std::string& plane) {
    push_token(std::format("@[]^{}", plane));
}

void PostfixBuilder::add_frame_dimension(const std::string& dim,
                                         const std::string& plane) {
    push_token(std::format("{}^{}", dim, plane));
}

void PostfixBuilder::add_exit_marker() { push_token("^exit^"); }

void PostfixBuilder::add_dropN(int count) {
    push_token(std::format("drop{}", count));
}

void PostfixBuilder::add_dupN(int count) {
    push_token(std::format("dup{}", count));
}

void PostfixBuilder::add_swapN(int count) {
    push_token(std::format("swap{}", count));
}

void PostfixBuilder::add_sortN(int count) {
    push_token(std::format("sort{}", count));
}

void PostfixBuilder::add_array_alloc_static(const std::string& array_name,
                                            const std::string& size) {
    push_token(std::format("{}{{}}^{}", array_name, size));
}

void PostfixBuilder::add_array_alloc_dynamic(const std::string& array_name) {
    push_token(std::format("{}{{}}^", array_name));
}

void PostfixBuilder::add_array_load(const std::string& array_name) {
    push_token(std::format("{}{{}}@", array_name));
}

void PostfixBuilder::add_array_store(const std::string& array_name) {
    push_token(std::format("{}{{}}!", array_name));
}

void PostfixBuilder::add_raw(const std::string& raw_string) {
    std::stringstream ss(raw_string);
    std::string token;
    while (ss >> token) {
        push_token(token);
    }
}

std::string PostfixBuilder::get_expression() const {
    if (tokens.empty()) {
        return "";
    }

    size_t total_len = 0;
    for (const auto& token : tokens) {
        total_len += token.length();
    }
    total_len += tokens.size() - 1; // for spaces

    std::string result;
    result.reserve(total_len);

    result.append(tokens[0]);
    for (size_t i = 1; i < tokens.size(); ++i) {
        result.push_back(' ');
        result.append(tokens[i]);
    }

    return result;
}

void PostfixBuilder::clear() { tokens.clear(); }

bool PostfixBuilder::empty() const { return tokens.empty(); }

void PostfixBuilder::append(const PostfixBuilder& other) {
    tokens.insert(tokens.end(), other.tokens.begin(), other.tokens.end());
}

void PostfixBuilder::prefix_labels(const std::string& prefix) {
    for (auto& token : tokens) {
        if (token.empty()) {
            continue;
        }

        bool is_label_def = token.front() == '#';
        bool is_jump = token.back() == '#';

        if (is_label_def || is_jump) {
            std::string label_name = is_label_def
                                         ? token.substr(1)
                                         : token.substr(0, token.size() - 1);

            if (label_name.empty()) {
                continue;
            }

            if (label_name.starts_with("__internal_")) {
                continue;
            }

            std::string new_label = prefix + label_name;
            if (is_label_def) {
                token = "#" + new_label;
            } else {
                token = new_label + "#";
            }
        }
    }
}

} // namespace infix2postfix
