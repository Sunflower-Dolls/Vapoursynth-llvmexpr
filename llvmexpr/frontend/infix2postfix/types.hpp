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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_TYPES_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_TYPES_HPP

#include "llvmexpr/frontend/Tokenizer.hpp"
#include "llvmexpr/utils/FixedString.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace infix2postfix {

struct SourceLocation {
    int line = 0;
    int column = 0;
};

struct Range {
    SourceLocation start;
    SourceLocation end;

    // NOLINTNEXTLINE(readability-identifier-naming)
    [[nodiscard]] std::string to_string() const {
        return std::format("{}:{} - {}:{}", start.line, start.column, end.line,
                           end.column);
    }
};

enum class Type : std::uint8_t {
    Value,
    Clip,
    Literal,
    LiteralString,
    Array,
    Void,
};

enum class TokenType : std::uint8_t {
    // Keywords
    If,       // if
    Else,     // else
    While,    // while
    Goto,     // goto
    Function, // function
    Return,   // return

    // Operators
    Plus,       // +
    Minus,      // -
    Star,       // *
    Slash,      // /
    Percent,    // %
    StarStar,   // **
    LogicalAnd, // &&
    LogicalOr,  // ||
    BitAnd,     // &
    BitOr,      // |
    BitXor,     // ^
    BitNot,     // ~
    Eq,         // ==
    Ne,         // !=
    Lt,         // <
    Le,         // <=
    Gt,         // >
    Ge,         // >=
    Assign,     // =
    Question,   // ?
    Colon,      // :
    Not,        // !

    // Punctuation
    LParen,    // (
    RParen,    // )
    LBrace,    // {
    RBrace,    // }
    LBracket,  // [
    RBracket,  // ]
    Comma,     // ,
    Dot,       // .
    Semicolon, // ;

    // Literals
    Identifier,
    Number,

    // Special
    Global, // <global...>
    Newline,
    EndOfFile,
    Invalid,
};

struct TokenMapping {
    TokenType type;
    std::string_view str;
};

constexpr std::array TOKEN_MAPPINGS = {
    // Keywords
    TokenMapping{.type = TokenType::If, .str = "if"},
    TokenMapping{.type = TokenType::Else, .str = "else"},
    TokenMapping{.type = TokenType::While, .str = "while"},
    TokenMapping{.type = TokenType::Goto, .str = "goto"},
    TokenMapping{.type = TokenType::Function, .str = "function"},
    TokenMapping{.type = TokenType::Return, .str = "return"},

    // Operators
    TokenMapping{.type = TokenType::Plus, .str = "+"},
    TokenMapping{.type = TokenType::Minus, .str = "-"},
    TokenMapping{.type = TokenType::Star, .str = "*"},
    TokenMapping{.type = TokenType::Slash, .str = "/"},
    TokenMapping{.type = TokenType::Percent, .str = "%"},
    TokenMapping{.type = TokenType::StarStar, .str = "**"},
    TokenMapping{.type = TokenType::LogicalAnd, .str = "&&"},
    TokenMapping{.type = TokenType::LogicalOr, .str = "||"},
    TokenMapping{.type = TokenType::BitAnd, .str = "&"},
    TokenMapping{.type = TokenType::BitOr, .str = "|"},
    TokenMapping{.type = TokenType::BitXor, .str = "^"},
    TokenMapping{.type = TokenType::BitNot, .str = "~"},
    TokenMapping{.type = TokenType::Eq, .str = "=="},
    TokenMapping{.type = TokenType::Ne, .str = "!="},
    TokenMapping{.type = TokenType::Lt, .str = "<"},
    TokenMapping{.type = TokenType::Le, .str = "<="},
    TokenMapping{.type = TokenType::Gt, .str = ">"},
    TokenMapping{.type = TokenType::Ge, .str = ">="},
    TokenMapping{.type = TokenType::Assign, .str = "="},
    TokenMapping{.type = TokenType::Question, .str = "?"},
    TokenMapping{.type = TokenType::Colon, .str = ":"},
    TokenMapping{.type = TokenType::Not, .str = "!"},

    // Punctuation
    TokenMapping{.type = TokenType::LParen, .str = "("},
    TokenMapping{.type = TokenType::RParen, .str = ")"},
    TokenMapping{.type = TokenType::LBrace, .str = "{"},
    TokenMapping{.type = TokenType::RBrace, .str = "}"},
    TokenMapping{.type = TokenType::LBracket, .str = "["},
    TokenMapping{.type = TokenType::RBracket, .str = "]"},
    TokenMapping{.type = TokenType::Comma, .str = ","},
    TokenMapping{.type = TokenType::Dot, .str = "."},
    TokenMapping{.type = TokenType::Semicolon, .str = ";"},
};

inline std::string token_type_to_string(TokenType type) {
    if (const auto* it = std::ranges::find_if(
            TOKEN_MAPPINGS,
            [type](const auto& mapping) { return mapping.type == type; });
        it != TOKEN_MAPPINGS.end()) {
        return std::string(it->str);
    }
    // Handle special cases not in the table
    switch (type) {
    case TokenType::Identifier:
        return "identifier";
    case TokenType::Number:
        return "number";
    case TokenType::Global:
        return "global declaration";
    case TokenType::Newline:
        return "newline";
    case TokenType::Semicolon:
        return "semicolon";
    case TokenType::EndOfFile:
        return "end of file";
    case TokenType::Invalid:
        return "invalid token";
    default:
        std::unreachable();
    }
}

struct Token {
    TokenType type;
    std::string value;
    Range range;
};

enum class Mode : std::uint8_t { Expr, Single, VkExpr };

enum class GlobalMode : std::uint8_t { None, All, Specific };

struct ParameterInfo {
    std::string name;
    Type type;
};

struct FunctionSignature {
    std::string name;
    std::vector<ParameterInfo> params;
    bool has_return;
    bool returns_value;
    Range range;
    GlobalMode global_mode = GlobalMode::None;
    std::set<std::string> specific_globals;

    // For <global.all>, track which global variables are actually used in the function body
    std::set<std::string> used_globals;
};

template <FixedString Prefix>
inline std::optional<int> get_index_with_prefix(const std::string& s) {
    if (!s.starts_with(Prefix.view())) {
        return std::nullopt;
    }
    auto idx_str = s.substr(Prefix.view().size());
    if (idx_str.empty()) {
        return std::nullopt;
    }

    if (!std::ranges::all_of(idx_str,
                             [](char c) { return std::isdigit(c) != 0; })) {
        return std::nullopt;
    }

    int idx = 0;
    const char* begin = idx_str.data();
    const char* end = begin + static_cast<std::ptrdiff_t>(idx_str.size());
    auto res = std::from_chars(begin, end, idx);
    if (res.ec != std::errc{} || res.ptr != end) {
        return std::nullopt;
    }
    return idx;
}

template <FixedString Prefix> inline bool is_name(const std::string& s) {
    return get_index_with_prefix<Prefix>(s).has_value();
}

inline std::optional<int> get_clip_index(const std::string& s) {
    if (s.length() == 1 && s[0] >= 'a' && s[0] <= 'z') {
        return parse_std_clip_idx(s[0]);
    }
    return get_index_with_prefix<FixedString{"src"}>(s);
}

inline bool is_clip_name(const std::string& s) {
    return get_clip_index(s).has_value();
}

inline std::optional<int> get_buffer_index(const std::string& s) {
    return get_index_with_prefix<FixedString{"buf"}>(s);
}

inline bool is_buffer_name(const std::string& s) {
    return is_name<FixedString{"buf"}>(s);
}

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_TYPES_HPP
