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
#include <numeric>
#include <sstream>

namespace infix2postfix {

void PostfixBuilder::pushToken(const std::string& token) {
    if (!token.empty()) {
        tokens.push_back(token);
    }
}

void PostfixBuilder::addOp(TokenType type) {
    switch (type) {
    case TokenType::Plus:
        pushToken("+");
        break;
    case TokenType::Minus:
        pushToken("-");
        break;
    case TokenType::Star:
        pushToken("*");
        break;
    case TokenType::Slash:
        pushToken("/");
        break;
    case TokenType::Percent:
        pushToken("%");
        break;
    case TokenType::StarStar:
        pushToken("pow");
        break;
    case TokenType::LogicalAnd:
        pushToken("and");
        break;
    case TokenType::LogicalOr:
        pushToken("or");
        break;
    case TokenType::BitAnd:
        pushToken("bitand");
        break;
    case TokenType::BitOr:
        pushToken("bitor");
        break;
    case TokenType::BitXor:
        pushToken("bitxor");
        break;
    case TokenType::Eq:
        pushToken("=");
        break;
    case TokenType::Ne:
        pushToken("=");
        pushToken("not");
        break;
    case TokenType::Lt:
        pushToken("<");
        break;
    case TokenType::Le:
        pushToken("<=");
        break;
    case TokenType::Gt:
        pushToken(">");
        break;
    case TokenType::Ge:
        pushToken(">=");
        break;
    default:
        std::unreachable();
    }
}

void PostfixBuilder::addUnaryOp(TokenType type) {
    switch (type) {
    case TokenType::Minus:
        pushToken("neg");
        break;
    case TokenType::Not:
        pushToken("not");
        break;
    case TokenType::BitNot:
        pushToken("bitnot");
        break;
    default:
        std::unreachable();
    }
}

void PostfixBuilder::addTernaryOp() { pushToken("?"); }

void PostfixBuilder::addFunctionCall(const std::string& func_name) {
    pushToken(func_name);
}

void PostfixBuilder::addNumber(const std::string& num_literal) {
    pushToken(num_literal);
}

void PostfixBuilder::addConstant(const std::string& const_name) {
    pushToken(const_name);
}

void PostfixBuilder::addVariableLoad(const std::string& var_name) {
    pushToken(var_name + "@");
}

void PostfixBuilder::addVariableStore(const std::string& var_name) {
    pushToken(var_name + "!");
}

void PostfixBuilder::addLabel(const std::string& label_name) {
    pushToken("#" + label_name);
}

void PostfixBuilder::addConditionalJump(const std::string& label_name) {
    pushToken(label_name + "#");
}

void PostfixBuilder::addUnconditionalJump(const std::string& label_name) {
    pushToken("1");
    addConditionalJump(label_name);
}

void PostfixBuilder::addPropAccess(const std::string& clip_name,
                                   const std::string& prop_name) {
    pushToken(std::format("{}.{}", clip_name, prop_name));
}

void PostfixBuilder::addPropExist(const std::string& clip_name,
                                  const std::string& prop_name) {
    pushToken(std::format("{}.{}?", clip_name, prop_name));
}

void PostfixBuilder::addSetProp(const std::string& prop_name,
                                const std::string& suffix) {
    pushToken(std::format("{}${}", prop_name, suffix));
}

void PostfixBuilder::addDeleteProp(const std::string& prop_name) {
    pushToken(std::format("{}$d", prop_name));
}

void PostfixBuilder::addStaticPixelAccess(const std::string& clip_name,
                                          const std::string& x,
                                          const std::string& y,
                                          const std::string& suffix) {
    pushToken(std::format("{}[{},{}]{}", clip_name, x, y, suffix));
}

void PostfixBuilder::addDynPixelAccessExpr(const std::string& clip_name,
                                           const std::string& suffix) {
    pushToken(std::format("{}[]{}", clip_name, suffix));
}

void PostfixBuilder::addDynPixelAccessSingle(const std::string& clip_name,
                                             const std::string& plane) {
    pushToken(std::format("{}^{}[]", clip_name, plane));
}

void PostfixBuilder::addStoreExpr() { pushToken("@[]"); }

void PostfixBuilder::addStoreSingle(const std::string& plane) {
    pushToken(std::format("@[]^{}", plane));
}

void PostfixBuilder::addFrameDimension(const std::string& dim,
                                       const std::string& plane) {
    pushToken(std::format("{}^{}", dim, plane));
}

void PostfixBuilder::addExitMarker() { pushToken("^exit^"); }

void PostfixBuilder::addDropN(int count) {
    pushToken(std::format("drop{}", count));
}

void PostfixBuilder::addDupN(int count) {
    pushToken(std::format("dup{}", count));
}

void PostfixBuilder::addSwapN(int count) {
    pushToken(std::format("swap{}", count));
}

void PostfixBuilder::addSortN(int count) {
    pushToken(std::format("sort{}", count));
}

void PostfixBuilder::addArrayAllocStatic(const std::string& array_name,
                                         const std::string& size) {
    pushToken(std::format("{}{{}}^{}", array_name, size));
}

void PostfixBuilder::addArrayAllocDynamic(const std::string& array_name) {
    pushToken(std::format("{}{{}}^", array_name));
}

void PostfixBuilder::addArrayLoad(const std::string& array_name) {
    pushToken(std::format("{}{{}}@", array_name));
}

void PostfixBuilder::addArrayStore(const std::string& array_name) {
    pushToken(std::format("{}{{}}!", array_name));
}

void PostfixBuilder::addRaw(const std::string& raw_string) {
    std::stringstream ss(raw_string);
    std::string token;
    while (ss >> token) {
        pushToken(token);
    }
}

std::string PostfixBuilder::getExpression() const {
    if (tokens.empty()) {
        return "";
    }

    size_t total_len =
        std::accumulate(tokens.begin(), tokens.end(), size_t{0},
                        [](size_t sum, const std::string& token) {
                            return sum + token.length();
                        });
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

void PostfixBuilder::prefixLabels(const std::string& prefix) {
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
