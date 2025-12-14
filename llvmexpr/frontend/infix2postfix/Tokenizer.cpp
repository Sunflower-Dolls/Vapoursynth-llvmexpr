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

#include "Tokenizer.hpp"
#include <algorithm>
#include <cctype>

namespace infix2postfix {

namespace {

std::map<std::string, TokenType> build_keywords_map() {
    std::map<std::string, TokenType> map;
    for (const auto& mapping : TOKEN_MAPPINGS) {
        if (mapping.str.length() > 1 && (std::isalpha(mapping.str[0]) != 0)) {
            map.emplace(mapping.str, mapping.type);
        }
    }
    return map;
}

using OpMap = std::map<char, std::vector<TokenMapping>>;

OpMap build_operator_map() {
    OpMap map;
    for (const auto& mapping : TOKEN_MAPPINGS) {
        if (!mapping.str.empty() && (std::isalpha(mapping.str[0]) == 0)) {
            map[mapping.str[0]].push_back(mapping);
        }
    }

    // Sort by length descending for greedy matching
    for (auto& pair : map) {
        std::ranges::sort(pair.second,
                          [](const TokenMapping& a, const TokenMapping& b) {
                              return a.str.length() > b.str.length();
                          });
    }
    return map;
}

} // namespace

const std::map<std::string, TokenType> Tokenizer::keywords =
    build_keywords_map();
static const OpMap operator_map = build_operator_map();

Tokenizer::Tokenizer(std::string source) : source(std::move(source)) {}

std::vector<Token> Tokenizer::tokenize() {
    std::vector<Token> tokens;
    while (peek() != '\0') {
        tokens.push_back(nextToken());
    }
    tokens.push_back(makeToken(TokenType::EndOfFile));
    return tokens;
}

Token Tokenizer::nextToken() {
    skipWhitespaceAndComments();
    start = current;
    start_line = line;
    start_column = column;
    if (peek() == '\0') {
        return makeToken(TokenType::EndOfFile);
    }

    char c = peek();

    if (c == '\n') {
        advance();
        return makeToken(TokenType::Newline);
    }

    if ((std::isalpha(c) != 0) || c == '_') {
        return identifier();
    }
    if (std::isdigit(c) != 0) {
        return number();
    }
    if (c == '$') {
        if ((std::isalpha(peek(1)) != 0) || peek(1) == '_') {
            advance(); // '$'
            while ((std::isalnum(peek()) != 0) || peek() == '_') {
                advance();
            }
            return makeToken(TokenType::Identifier);
        }
    }

    if (c == '<') {
        if (source.substr(current, std::string("<global").length()) ==
            "<global") {
            return globalDeclaration();
        }
    }

    if (operator_map.contains(c)) {
        const auto& possible_tokens = operator_map.at(c);
        auto it =
            std::ranges::find_if(possible_tokens, [&](const auto& mapping) {
                return source.substr(current, mapping.str.length()) ==
                       mapping.str;
            });
        if (it != possible_tokens.end()) {
            current += it->str.length();
            return makeToken(it->type);
        }
    }

    advance();
    return makeToken(TokenType::Invalid, std::string(1, c));
}

void Tokenizer::skipWhitespaceAndComments() {
    while (true) {
        char c = peek();
        switch (c) {
        case ' ':
        case '\r':
        case '\t':
            advance();
            break;
        case '#':
            while (peek() != '\n' && peek() != '\0') {
                advance();
            }
            break;
        default:
            return;
        }
    }
}

char Tokenizer::peek(int offset) const {
    if (current + offset >= source.length()) {
        return '\0';
    }
    return source[current + offset];
}

char Tokenizer::advance() {
    if (current < source.length()) {
        if (source[current] == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        current++;
    }
    return source[current - 1];
}

Token Tokenizer::makeToken(TokenType type, const std::string& value) const {
    Range range;
    range.start.line = start_line;
    range.start.column = start_column;
    range.end.line = line;
    range.end.column =
        column - 1; // column is already advanced past the last character

    return {.type = type,
            .value =
                value.empty() ? source.substr(start, current - start) : value,
            .range = range};
}

Token Tokenizer::identifier() {
    while ((std::isalnum(peek()) != 0) || peek() == '_') {
        advance();
    }
    std::string text = source.substr(start, current - start);
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return makeToken(it->second);
    }
    return makeToken(TokenType::Identifier);
}

Token Tokenizer::number() {
    bool is_hex = false;
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        is_hex = true;
        advance(); // 0
        advance(); // x
    }

    while ((std::isdigit(peek()) != 0) ||
           (is_hex && (std::isxdigit(peek()) != 0))) {
        advance();
    }

    if (peek() == '.' && (std::isdigit(peek(1)) != 0)) {
        advance(); // '.'
        while (std::isdigit(peek()) != 0) {
            advance();
        }
    }

    if (is_hex && (peek() == 'p' || peek() == 'P')) {
        advance(); // 'p'
        if (peek() == '+' || peek() == '-') {
            advance(); // sign
        }
        while (std::isdigit(peek()) != 0) {
            advance();
        }
    } else if (!is_hex && (peek() == 'e' || peek() == 'E')) {
        advance(); // 'e'
        if (peek() == '+' || peek() == '-') {
            advance(); // sign
        }
        while (std::isdigit(peek()) != 0) {
            advance();
        }
    }

    return makeToken(TokenType::Number);
}

Token Tokenizer::globalDeclaration() {
    advance();     // Consume initial '<'
    int depth = 1; // We've already seen the opening '<'
    while (depth > 0 && peek() != '\0') {
        char c = peek();
        if (c == '<') {
            depth++;
        } else if (c == '>') {
            depth--;
        }
        advance();
    }

    if (depth == 0) {
        return makeToken(TokenType::Global);
    }
    return makeToken(TokenType::Invalid, source.substr(start, current - start));
}

} // namespace infix2postfix