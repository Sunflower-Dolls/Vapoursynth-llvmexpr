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

#include "Preprocessor.hpp"
#include "StandardLibrary.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <deque>
#include <format>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>

namespace infix2postfix {

namespace preprocessor_detail {

enum class TokenType : std::uint8_t {
    Identifier,
    Number,
    Plus,
    Minus,
    Multiply,
    Divide,
    Modulo,
    Power,
    Equal,
    NotEqual,
    Greater,
    GreaterEqual,
    Less,
    LessEqual,
    LogicalAnd,
    LogicalOr,
    LogicalNot,
    BitAnd,
    BitOr,
    BitXor,
    BitNot,
    Lparen,
    Rparen,
    Lbracket,
    Rbracket,
    Lbrace,
    Rbrace,
    Comma,
    Dot,
    Question,
    Colon,
    Semicolon,
    Assign,
    AtDefine,
    AtUndef,
    AtIfdef,
    AtIfndef,
    AtIf,
    AtElse,
    AtEndif,
    AtError,
    AtRequires,
    At,
    Whitespace,
    Newline,
    Comment,
    EndOfFile,
    Concat,
    BeginMacroExpansion,
    EndMacroExpansion,
};

struct Token {
    TokenType type;
    std::string text;
    int line;
    int column;
    std::variant<int64_t, double> numeric_value;
    bool has_numeric_value = false;
    size_t expansion_idx = static_cast<size_t>(-1);

    Token() : type(TokenType::EndOfFile), line(0), column(0) {}

    Token(TokenType t, std::string txt, int ln, int col)
        : type(t), text(std::move(txt)), line(ln), column(col) {}

    Token(TokenType t, std::string txt, int ln, int col,
          const std::variant<int64_t, double>& val)
        : type(t), text(std::move(txt)), line(ln), column(col),
          numeric_value(val), has_numeric_value(true) {}
};

class PreprocessorTokenizer {
  public:
    explicit PreprocessorTokenizer(std::string_view source) : source(source) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (!eof()) {
            Token tok = nextToken();
            tokens.push_back(tok);
            if (tok.type == TokenType::EndOfFile) {
                break;
            }
        }
        return tokens;
    }

  private:
    std::string_view source;
    size_t pos = 0;
    int line = 1;
    int column = 1;

    [[nodiscard]] bool eof() const { return pos >= source.length(); }

    [[nodiscard]] char peek(size_t offset = 0) const {
        size_t p = pos + offset;
        return p < source.length() ? source[p] : '\0';
    }

    [[nodiscard]] char peekSigned(int offset) const {
        if (offset < 0) {
            auto abs_offset = static_cast<size_t>(-offset);
            if (abs_offset > pos) {
                return '\0';
            }
            return source[pos - abs_offset];
        }
        return peek(static_cast<size_t>(offset));
    }

    char consume() {
        if (eof()) {
            return '\0';
        }
        char c = source[pos++];
        if (c == '\n') {
            line++;
            column = 1;
        } else {
            column++;
        }
        return c;
    }

    Token nextToken() {
        if (eof()) {
            return {TokenType::EndOfFile, "", line, column};
        }

        int start_line = line;
        int start_column = column;
        char c = peek();

        if (c == ' ' || c == '\t' || c == '\r') {
            return consumeWhitespace(start_line, start_column);
        }
        if (c == '\n') {
            consume();
            return {TokenType::Newline, "\n", start_line, start_column};
        }
        if (c == '#') {
            return consumeComment(start_line, start_column);
        }
        if (std::isdigit(c) != 0 || (c == '.' && std::isdigit(peek(1)) != 0)) {
            return consumeNumber(start_line, start_column);
        }
        if (c == '@') {
            if (peek(1) == '@') {
                consume();
                consume();
                return {TokenType::Concat, "@@", start_line, start_column};
            }
            return consumeDirective(start_line, start_column);
        }
        if (std::isalpha(c) != 0 || c == '_' || c == '$') {
            return consumeIdentifier(start_line, start_column);
        }
        return consumeOperator(start_line, start_column);
    }

    Token consumeWhitespace(int start_line, int start_column) {
        size_t start = pos;
        while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
            consume();
        }
        std::string text(source.substr(start, pos - start));
        return {TokenType::Whitespace, text, start_line, start_column};
    }

    Token consumeComment(int start_line, int start_column) {
        size_t start = pos;
        consume();
        while (!eof() && peek() != '\n') {
            consume();
        }
        std::string text(source.substr(start, pos - start));
        return {TokenType::Comment, text, start_line, start_column};
    }

    Token consumeNumber(int start_line, int start_column) {
        size_t start = pos;

        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            consume();
            consume();
            while (!eof() && (std::isxdigit(peek()) != 0 || peek() == '.' ||
                              peek() == 'p' || peek() == 'P' ||
                              ((peek() == '+' || peek() == '-') &&
                               (std::tolower(peekSigned(-1)) == 'p')))) {
                consume();
            }
        } else if (peek() == '0' && std::isdigit(peek(1)) != 0) {
            while (!eof() && peek() >= '0' && peek() <= '7') {
                consume();
            }
        } else {
            while (!eof() && (std::isdigit(peek()) != 0 || peek() == '.' ||
                              peek() == 'e' || peek() == 'E' ||
                              ((peek() == '+' || peek() == '-') &&
                               (std::tolower(peekSigned(-1)) == 'e')))) {
                consume();
            }
        }

        std::string text(source.substr(start, pos - start));

        try {
            if (text.find('.') != std::string::npos ||
                text.find('e') != std::string::npos ||
                text.find('E') != std::string::npos) {
                double val = std::stod(text);
                return {TokenType::Number, text, start_line, start_column, val};
            }
            int64_t val = std::stoll(text, nullptr, 0);
            return {TokenType::Number, text, start_line, start_column, val};
        } catch (...) {
            return {TokenType::Number, text, start_line, start_column};
        }
    }

    Token consumeDirective(int start_line, int start_column) {
        consume();

        if (eof() || (std::isalpha(peek()) == 0 && peek() != '_')) {
            return {TokenType::At, "@", start_line, start_column};
        }

        size_t start = pos;
        while (!eof() && (std::isalnum(peek()) != 0 || peek() == '_')) {
            consume();
        }

        std::string directive(source.substr(start, pos - start));
        std::string full_text = "@" + directive;

        TokenType type = TokenType::At;
        if (directive == "define") {
            type = TokenType::AtDefine;
        } else if (directive == "undef") {
            type = TokenType::AtUndef;
        } else if (directive == "ifdef") {
            type = TokenType::AtIfdef;
        } else if (directive == "ifndef") {
            type = TokenType::AtIfndef;
        } else if (directive == "if") {
            type = TokenType::AtIf;
        } else if (directive == "else") {
            type = TokenType::AtElse;
        } else if (directive == "endif") {
            type = TokenType::AtEndif;
        } else if (directive == "error") {
            type = TokenType::AtError;
        } else if (directive == "requires") {
            type = TokenType::AtRequires;
        }

        return {type, full_text, start_line, start_column};
    }

    Token consumeIdentifier(int start_line, int start_column) {
        size_t start = pos;
        while (!eof() &&
               (std::isalnum(peek()) != 0 || peek() == '_' || peek() == '$')) {
            consume();
        }
        std::string text(source.substr(start, pos - start));
        return {TokenType::Identifier, text, start_line, start_column};
    }

    Token consumeOperator(int start_line, int start_column) {
        char c = peek();
        char next = peek(1);

        if (c == '*' && next == '*') {
            consume();
            consume();
            return {TokenType::Power, "**", start_line, start_column};
        }
        if (c == '=' && next == '=') {
            consume();
            consume();
            return {TokenType::Equal, "==", start_line, start_column};
        }
        if (c == '!' && next == '=') {
            consume();
            consume();
            return {TokenType::NotEqual, "!=", start_line, start_column};
        }
        if (c == '>' && next == '=') {
            consume();
            consume();
            return {TokenType::GreaterEqual, ">=", start_line, start_column};
        }
        if (c == '<' && next == '=') {
            consume();
            consume();
            return {TokenType::LessEqual, "<=", start_line, start_column};
        }
        if (c == '&' && next == '&') {
            consume();
            consume();
            return {TokenType::LogicalAnd, "&&", start_line, start_column};
        }
        if (c == '|' && next == '|') {
            consume();
            consume();
            return {TokenType::LogicalOr, "||", start_line, start_column};
        }

        consume();
        TokenType type = TokenType::EndOfFile;
        std::string text(1, c);

        switch (c) {
        case '+':
            type = TokenType::Plus;
            break;
        case '-':
            type = TokenType::Minus;
            break;
        case '*':
            type = TokenType::Multiply;
            break;
        case '/':
            type = TokenType::Divide;
            break;
        case '%':
            type = TokenType::Modulo;
            break;
        case '>':
            type = TokenType::Greater;
            break;
        case '<':
            type = TokenType::Less;
            break;
        case '!':
            type = TokenType::LogicalNot;
            break;
        case '&':
            type = TokenType::BitAnd;
            break;
        case '|':
            type = TokenType::BitOr;
            break;
        case '^':
            type = TokenType::BitXor;
            break;
        case '~':
            type = TokenType::BitNot;
            break;
        case '(':
            type = TokenType::Lparen;
            break;
        case ')':
            type = TokenType::Rparen;
            break;
        case '[':
            type = TokenType::Lbracket;
            break;
        case ']':
            type = TokenType::Rbracket;
            break;
        case '{':
            type = TokenType::Lbrace;
            break;
        case '}':
            type = TokenType::Rbrace;
            break;
        case ',':
            type = TokenType::Comma;
            break;
        case '.':
            type = TokenType::Dot;
            break;
        case '?':
            type = TokenType::Question;
            break;
        case ':':
            type = TokenType::Colon;
            break;
        case ';':
            type = TokenType::Semicolon;
            break;
        case '=':
            type = TokenType::Assign;
            break;
        default:
            throw PreprocessorError(
                std::format("Unexpected character '{}' at line {}, column {}",
                            c, start_line, start_column));
        }

        return {type, text, start_line, start_column};
    }
};

std::string tokensToString(const std::vector<Token>& tokens,
                           bool preserve_whitespace = false) {
    std::string result;
    for (const auto& tok : tokens) {
        if (!preserve_whitespace && (tok.type == TokenType::Whitespace ||
                                     tok.type == TokenType::Comment)) {
            continue;
        }
        result += tok.text;
    }
    return result;
}

std::vector<Token> trimTokens(const std::vector<Token>& tokens) {
    if (tokens.empty()) {
        return tokens;
    }

    size_t start = 0;
    while (start < tokens.size() &&
           (tokens[start].type == TokenType::Whitespace ||
            tokens[start].type == TokenType::Comment)) {
        start++;
    }

    size_t end = tokens.size();
    while (end > start && (tokens[end - 1].type == TokenType::Whitespace ||
                           tokens[end - 1].type == TokenType::Comment)) {
        end--;
    }

    return {tokens.begin() + static_cast<std::ptrdiff_t>(start),
            tokens.begin() + static_cast<std::ptrdiff_t>(end)};
}

bool isSkippable(const Token& t) {
    return t.type == TokenType::Whitespace || t.type == TokenType::Comment ||
           t.type == TokenType::BeginMacroExpansion ||
           t.type == TokenType::EndMacroExpansion;
}

} // namespace preprocessor_detail

using Token = preprocessor_detail::Token;
using TokenType = preprocessor_detail::TokenType;
using PreprocessorTokenizer = preprocessor_detail::PreprocessorTokenizer;

namespace preprocessor {

struct Macro {
    std::string name;
    bool is_function_like = false;
    std::vector<std::string> params;
    std::vector<Token> body;
};

class MacroTable {
  public:
    void define(Macro macro) { macros[macro.name] = std::move(macro); }

    void undef(const std::string& name) { macros.erase(name); }

    [[nodiscard]] const Macro* find(const std::string& name) const {
        auto it = macros.find(name);
        return it != macros.end() ? &it->second : nullptr;
    }

    [[nodiscard]] bool contains(const std::string& name) const {
        return macros.contains(name);
    }

    [[nodiscard]] auto begin() const { return macros.begin(); }
    [[nodiscard]] auto end() const { return macros.end(); }

  private:
    std::unordered_map<std::string, Macro> macros;
};

class TokenStream {
  public:
    explicit TokenStream(std::vector<Token> p_tokens) {
        for (auto&& tok : p_tokens) {
            tokens.push_back(std::move(tok));
        }
    }

    [[nodiscard]] bool is_eof() const {
        return tokens.empty() || tokens.front().type == TokenType::EndOfFile;
    }

    [[nodiscard]] Token peek(size_t offset = 0) const {
        if (offset >= tokens.size()) {
            static Token eof_token{TokenType::EndOfFile, "", 0, 0};
            return eof_token;
        }
        return tokens[offset];
    }

    Token consume() {
        if (is_eof()) {
            static Token eof_token{TokenType::EndOfFile, "", 0, 0};
            return eof_token;
        }
        Token tok = std::move(tokens.front());
        tokens.pop_front();
        return tok;
    }

    void prepend(const std::vector<Token>& tokens) {
        for (const auto& tok : std::views::reverse(tokens)) {
            this->tokens.push_front(tok);
        }
    }

    void skipWhitespace() {
        while (!is_eof() && preprocessor_detail::isSkippable(peek())) {
            consume();
        }
    }

  private:
    std::deque<Token> tokens;
};

class Evaluator {
  public:
    explicit Evaluator(const std::vector<Token>& tokens)
        : tokens(tokens), stream(tokens) {}

    std::variant<int64_t, double> evaluate() {
        pos = 0;
        skipWhitespace();

        if (is_eof()) {
            throw std::runtime_error("Cannot evaluate an empty expression");
        }

        Value result = parseConditional();
        skipWhitespace();

        if (!is_eof()) {
            throw std::runtime_error("Unexpected tokens at end of expression");
        }

        return result.val;
    }

    std::optional<std::variant<int64_t, double>> tryEvaluate() {
        try {
            return evaluate();
        } catch (const PreprocessorError&) {
            throw;
        } catch (const std::runtime_error&) {
            return std::nullopt;
        }
    }

    static bool is_truthy(const std::variant<int64_t, double>& val) {
        return Value(val).is_truthy();
    }

    static std::string toString(const std::variant<int64_t, double>& val) {
        return Value(val).to_string();
    }

  private:
    struct Value {
        std::variant<int64_t, double> val;

        explicit Value(std::variant<int64_t, double> v = int64_t(0)) : val(v) {}
        Value(int64_t v) : val(v) {}
        Value(double v) : val(v) {}

        [[nodiscard]] bool is_double() const {
            return std::holds_alternative<double>(val);
        }

        [[nodiscard]] double to_double() const {
            if (is_double()) {
                return std::get<double>(val);
            }
            return static_cast<double>(std::get<int64_t>(val));
        }

        [[nodiscard]] bool is_truthy() const { return to_double() != 0.0; }

        [[nodiscard]] std::string to_string() const {
            if (is_double()) {
                double d = std::get<double>(val);
                std::string str = std::format("{}", d);
                if (str.find('.') == std::string::npos &&
                    str.find('e') == std::string::npos &&
                    str.find('E') == std::string::npos) {
                    str += ".0";
                }
                return str;
            }
            return std::to_string(std::get<int64_t>(val));
        }
    };

    const std::vector<Token>& tokens;
    TokenStream stream;
    size_t pos = 0;

    [[nodiscard]] bool is_eof() const {
        return pos >= tokens.size() || tokens[pos].type == TokenType::EndOfFile;
    }

    [[nodiscard]] const Token& peek(size_t offset = 0) const {
        size_t p = pos + offset;
        if (p >= tokens.size()) {
            static Token eof_token{TokenType::EndOfFile, "", 0, 0};
            return eof_token;
        }
        return tokens[p];
    }

    Token consume() {
        if (is_eof()) {
            static Token eof_token{TokenType::EndOfFile, "", 0, 0};
            return eof_token;
        }
        return tokens[pos++];
    }

    void skipWhitespace() {
        while (!is_eof() && preprocessor_detail::isSkippable(peek())) {
            consume();
        }
    }

    Value parsePrimary() {
        skipWhitespace();

        const Token& tok = peek();

        if (tok.type == TokenType::Number) {
            consume();
            if (tok.has_numeric_value) {
                return Value(tok.numeric_value);
            }
            try {
                if (tok.text.find('.') != std::string::npos ||
                    tok.text.find('e') != std::string::npos ||
                    tok.text.find('E') != std::string::npos) {
                    return {std::stod(tok.text)};
                }
                return {static_cast<int64_t>(std::stoll(tok.text, nullptr, 0))};
            } catch (...) {
                throw std::runtime_error("Invalid number: " + tok.text);
            }
        }

        if (tok.type == TokenType::Identifier) {
            std::string name = tok.text;
            throw std::runtime_error(
                "Unexpanded identifier in constant expression: " + name);
        }

        if (tok.type == TokenType::Lparen) {
            consume();
            skipWhitespace();
            Value val = parseConditional();
            skipWhitespace();
            if (peek().type != TokenType::Rparen) {
                throw std::runtime_error("Expected ')'");
            }
            consume();
            return val;
        }

        throw std::runtime_error("Unexpected token in expression: " + tok.text);
    }

    Value parseUnary() {
        skipWhitespace();

        if (peek().type == TokenType::Minus) {
            consume();
            skipWhitespace();
            Value val = parseUnary();
            if (val.is_double()) {
                return {-val.to_double()};
            }
            return {-std::get<int64_t>(val.val)};
        }

        if (peek().type == TokenType::LogicalNot) {
            consume();
            skipWhitespace();
            return {static_cast<int64_t>(!parseUnary().is_truthy())};
        }

        if (peek().type == TokenType::Plus) {
            consume();
            skipWhitespace();
            return parseUnary();
        }

        if (peek().type == TokenType::BitNot) {
            consume();
            skipWhitespace();
            Value val = parseUnary();
            if (val.is_double()) {
                return {(~static_cast<int64_t>(std::round(val.to_double())))};
            }
            return {~std::get<int64_t>(val.val)};
        }

        return parsePrimary();
    }

    Value parsePower() {
        Value left = parseUnary();
        skipWhitespace();

        if (peek().type == TokenType::Power) {
            consume();
            skipWhitespace();
            Value right = parsePower();
            return {std::pow(left.to_double(), right.to_double())};
        }

        return left;
    }

    Value parseFactor() {
        Value left = parsePower();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::Multiply &&
                op.type != TokenType::Divide && op.type != TokenType::Modulo) {
                break;
            }

            consume();
            skipWhitespace();
            Value right = parsePower();

            if (left.is_double() || right.is_double()) {
                double l = left.to_double();
                double r = right.to_double();

                switch (op.type) {
                case TokenType::Multiply:
                    left = Value(l * r);
                    break;
                case TokenType::Divide:
                    left = Value(l / r);
                    break;
                case TokenType::Modulo:
                    throw std::runtime_error(
                        "Modulo requires integer operands");
                default:
                    std::unreachable();
                }
            } else {
                int64_t l = std::get<int64_t>(left.val);
                int64_t r = std::get<int64_t>(right.val);

                switch (op.type) {
                case TokenType::Multiply:
                    left = Value(l * r);
                    break;
                case TokenType::Divide:
                    left = Value(l / r);
                    break;
                case TokenType::Modulo:
                    left = Value(l % r);
                    break;
                default:
                    std::unreachable();
                }
            }
        }

        return left;
    }

    Value parseTerm() {
        Value left = parseFactor();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::Plus && op.type != TokenType::Minus) {
                break;
            }

            consume();
            skipWhitespace();
            Value right = parseFactor();

            if (left.is_double() || right.is_double()) {
                double l = left.to_double();
                double r = right.to_double();
                left = Value(op.type == TokenType::Plus ? (l + r) : (l - r));
            } else {
                int64_t l = std::get<int64_t>(left.val);
                int64_t r = std::get<int64_t>(right.val);
                left = Value(op.type == TokenType::Plus ? (l + r) : (l - r));
            }
        }

        return left;
    }

    Value parseBitwiseOr() {
        Value left = parseBitwiseXor();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::BitOr) {
                break;
            }

            consume();
            skipWhitespace();
            Value right = parseBitwiseXor();

            int64_t l = left.is_double()
                            ? static_cast<int64_t>(std::round(left.to_double()))
                            : std::get<int64_t>(left.val);
            int64_t r =
                right.is_double()
                    ? static_cast<int64_t>(std::round(right.to_double()))
                    : std::get<int64_t>(right.val);

            left = Value(l | r);
        }

        return left;
    }

    Value parseBitwiseXor() {
        Value left = parseBitwiseAnd();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::BitXor) {
                break;
            }

            consume();
            skipWhitespace();
            Value right = parseBitwiseAnd();

            int64_t l = left.is_double()
                            ? static_cast<int64_t>(std::round(left.to_double()))
                            : std::get<int64_t>(left.val);
            int64_t r =
                right.is_double()
                    ? static_cast<int64_t>(std::round(right.to_double()))
                    : std::get<int64_t>(right.val);

            left = Value(l ^ r);
        }

        return left;
    }

    Value parseBitwiseAnd() {
        Value left = parseEquality();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::BitAnd) {
                break;
            }

            consume();
            skipWhitespace();
            Value right = parseEquality();

            int64_t l = left.is_double()
                            ? static_cast<int64_t>(std::round(left.to_double()))
                            : std::get<int64_t>(left.val);
            int64_t r =
                right.is_double()
                    ? static_cast<int64_t>(std::round(right.to_double()))
                    : std::get<int64_t>(right.val);

            left = Value(l & r);
        }

        return left;
    }

    Value parseEquality() {
        Value left = parseComparison();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::Equal && op.type != TokenType::NotEqual) {
                break;
            }

            TokenType op_type = op.type;
            consume();
            skipWhitespace();
            Value right = parseComparison();

            double l = left.to_double();
            double r = right.to_double();
            bool result = (op_type == TokenType::Equal) ? (l == r) : (l != r);

            left = Value(static_cast<int64_t>(result));
        }

        return left;
    }

    Value parseComparison() {
        Value left = parseTerm();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::Greater &&
                op.type != TokenType::GreaterEqual &&
                op.type != TokenType::Less && op.type != TokenType::LessEqual) {
                break;
            }

            TokenType op_type = op.type;
            consume();
            skipWhitespace();
            Value right = parseTerm();

            double l = left.to_double();
            double r = right.to_double();
            bool result = false;

            switch (op_type) {
            case TokenType::Greater:
                result = l > r;
                break;
            case TokenType::GreaterEqual:
                result = l >= r;
                break;
            case TokenType::Less:
                result = l < r;
                break;
            case TokenType::LessEqual:
                result = l <= r;
                break;
            default:
                std::unreachable();
            }

            left = Value(static_cast<int64_t>(result));
        }

        return left;
    }

    Value parseLogicalAnd() {
        Value left = parseBitwiseOr();

        while (true) {
            skipWhitespace();
            if (peek().type != TokenType::LogicalAnd) {
                break;
            }
            consume();
            skipWhitespace();
            Value right = parseBitwiseOr();
            left = Value(
                static_cast<int64_t>(left.is_truthy() && right.is_truthy()));
        }

        return left;
    }

    Value parseLogicalOr() {
        Value left = parseLogicalAnd();

        while (true) {
            skipWhitespace();
            if (peek().type != TokenType::LogicalOr) {
                break;
            }
            consume();
            skipWhitespace();
            Value right = parseLogicalAnd();
            left = Value(
                static_cast<int64_t>(left.is_truthy() || right.is_truthy()));
        }

        return left;
    }

    Value parseConditional() {
        Value condition = parseLogicalOr();
        skipWhitespace();

        if (peek().type != TokenType::Question) {
            return condition;
        }

        consume();
        bool cond_truthy = condition.is_truthy();
        skipWhitespace();

        if (cond_truthy) {
            Value then_val = parseConditional();
            skipWhitespace();
            if (peek().type != TokenType::Colon) {
                throw std::runtime_error(
                    "Expected ':' in conditional expression");
            }
            consume();
            skipElseBranch();
            return then_val;
        }

        skipThenBranchToColon();
        skipWhitespace();
        if (peek().type != TokenType::Colon) {
            throw std::runtime_error("Expected ':' in conditional expression");
        }
        consume();
        skipWhitespace();
        Value else_val = parseConditional();
        return else_val;
    }

    void skipThenBranchToColon() {
        int nested = 0;
        while (!is_eof()) {
            const Token& tok = peek();

            if (tok.type == TokenType::Question) {
                nested++;
                consume();
            } else if (tok.type == TokenType::Colon) {
                if (nested == 0) {
                    break;
                }
                nested--;
                consume();
            } else {
                consume();
            }
        }
    }

    void skipElseBranch() {
        int nested = 0;
        int paren_depth = 0;

        while (!is_eof()) {
            const Token& tok = peek();

            if (tok.type == TokenType::Lparen) {
                paren_depth++;
                consume();
            } else if (tok.type == TokenType::Rparen) {
                if (paren_depth == 0) {
                    break;
                }
                paren_depth--;
                consume();
            } else if (tok.type == TokenType::Comma && paren_depth == 0) {
                break;
            } else if (tok.type == TokenType::Question) {
                nested++;
                consume();
            } else if (tok.type == TokenType::Colon) {
                if (nested == 0) {
                    break;
                }
                nested--;
                consume();
            } else {
                consume();
            }
        }
    }
};

class Expander {
  public:
    Expander(const MacroTable& macros, int recursion_depth = 0)
        : macros(macros), recursion_depth(recursion_depth) {}

    std::vector<Token> expand(const std::vector<Token>& input) {
        TokenStream stream(input);
        std::vector<Token> result;

        while (!stream.is_eof()) {
            const auto tok = stream.peek();

            if (tok.type == TokenType::Identifier) {
                if (tok.text == "defined") {
                    handleDefinedOperator(stream, result);
                    continue;
                }

                if (tok.text == "consteval" || tok.text == "is_consteval") {
                    handleConstevalIntrinsics(stream, result, tok);
                    continue;
                }

                if (tok.text == "static_assert") {
                    handleStaticAssertIntrinsic(stream, result, tok);
                    continue;
                }

                const Macro* macro = macros.find(tok.text);
                if (macro == nullptr) {
                    result.push_back(stream.consume());
                    continue;
                }

                Token macro_token = stream.consume();

                if (macro->is_function_like) {
                    expandFunctionLikeMacro(stream, result, macro_token,
                                            *macro);
                } else {
                    expandObjectLikeMacro(stream, result, macro_token, *macro);
                }
            } else {
                result.push_back(stream.consume());
            }
        }

        return result;
    }

    [[nodiscard]] std::vector<MacroExpansion> getExpansions() const {
        return expansions;
    }

  private:
    static constexpr int MAX_RECURSION = 1000;

    const MacroTable& macros;
    int recursion_depth;
    std::vector<MacroExpansion> expansions;

    void handleDefinedOperator(TokenStream& stream,
                               std::vector<Token>& result) {
        const auto tok = stream.peek();
        int def_line = tok.line;
        int def_col = tok.column;
        stream.consume();
        stream.skipWhitespace();

        bool has_paren = false;
        if (!stream.is_eof() && stream.peek().type == TokenType::Lparen) {
            has_paren = true;
            stream.consume();
            stream.skipWhitespace();
        }

        if (!stream.is_eof() && stream.peek().type == TokenType::Identifier) {
            Token macro_token = stream.consume();
            std::string macro_name = macro_token.text;

            if (has_paren) {
                stream.skipWhitespace();
                if (!stream.is_eof() &&
                    stream.peek().type == TokenType::Rparen) {
                    stream.consume();
                }
            }

            std::string value = macros.contains(macro_name) ? "1" : "0";
            result.emplace_back(
                TokenType::Number, value, def_line, def_col,
                static_cast<int64_t>(macros.contains(macro_name) ? 1 : 0));
        }
    }

    void handleConstevalIntrinsics(TokenStream& stream,
                                   std::vector<Token>& result,
                                   const Token& tok) {
        bool is_probe = (tok.text == "is_consteval");
        int start_line = tok.line;
        int start_col = tok.column;

        stream.consume();
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::Lparen) {
            result.emplace_back(TokenType::Identifier, tok.text, start_line,
                                start_col);
            return;
        }

        stream.consume();

        std::vector<Token> arg_tokens;
        int depth = 0;
        bool found = false;

        while (!stream.is_eof()) {
            const auto t = stream.peek();

            if (t.type == TokenType::Lparen) {
                depth++;
                arg_tokens.push_back(stream.consume());
            } else if (t.type == TokenType::Rparen) {
                if (depth == 0) {
                    found = true;
                    stream.consume();
                    break;
                }
                depth--;
                arg_tokens.push_back(stream.consume());
            } else {
                arg_tokens.push_back(stream.consume());
            }
        }

        if (!found) {
            result.emplace_back(TokenType::Identifier, tok.text, start_line,
                                start_col);
            return;
        }

        arg_tokens = expand(arg_tokens);

        std::optional<std::variant<int64_t, double>> maybe;
        try {
            Evaluator evaluator(arg_tokens);
            maybe = evaluator.tryEvaluate();
        } catch (const PreprocessorError&) {
            if (is_probe) {
                maybe = std::nullopt;
            } else {
                throw;
            }
        } catch (...) {
            maybe = std::nullopt;
        }

        if (is_probe) {
            std::string value = maybe ? "1" : "0";
            result.emplace_back(TokenType::Number, value, start_line, start_col,
                                static_cast<int64_t>(maybe ? 1 : 0));
        } else {
            if (!maybe) {
                throw PreprocessorError(
                    "consteval() requires a constant expression");
            }
            std::string value = Evaluator::toString(*maybe);
            result.emplace_back(TokenType::Number, value, start_line, start_col,
                                *maybe);
        }
    }

    void handleStaticAssertIntrinsic(TokenStream& stream,
                                     std::vector<Token>& result,
                                     const Token& tok) {
        int start_line = tok.line;
        int start_col = tok.column;

        stream.consume();
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::Lparen) {
            result.emplace_back(TokenType::Identifier, tok.text, start_line,
                                start_col);
            return;
        }

        stream.consume();

        std::vector<std::vector<Token>> args = parseMacroArguments(stream);
        if (args.size() != 2) {
            throw PreprocessorError(
                "static_assert() expects 2 arguments: condition, message");
        }

        Expander nested_expander(macros, recursion_depth + 1);
        std::vector<Token> cond_tokens = nested_expander.expand(args[0]);
        std::vector<Token> msg_tokens = nested_expander.expand(args[1]);

        std::optional<std::variant<int64_t, double>> maybe;
        try {
            Evaluator evaluator(cond_tokens);
            maybe = evaluator.tryEvaluate();
        } catch (const PreprocessorError&) {
            throw;
        } catch (...) {
            maybe = std::nullopt;
        }

        if (!maybe) {
            throw PreprocessorError(
                "static_assert() requires a constant expression condition");
        }

        if (!Evaluator::is_truthy(*maybe)) {
            std::string message =
                preprocessor_detail::tokensToString(msg_tokens, true);
            if (message.empty()) {
                message = "static_assert condition is false";
            }
            throw PreprocessorError(
                std::format("static_assert failed: {}", message));
        }

        result.emplace_back(TokenType::Number, "1", start_line, start_col,
                            static_cast<int64_t>(1));
    }

    std::vector<std::vector<Token>> parseMacroArguments(TokenStream& stream) {
        std::vector<std::vector<Token>> arguments;
        std::vector<Token> current_arg;
        int paren_depth = 0;

        while (!stream.is_eof()) {
            const auto arg_tok = stream.peek();

            if (arg_tok.type == TokenType::Lparen) {
                paren_depth++;
                current_arg.push_back(stream.consume());
            } else if (arg_tok.type == TokenType::Rparen) {
                if (paren_depth == 0) {
                    arguments.push_back(
                        preprocessor_detail::trimTokens(current_arg));
                    stream.consume();
                    break;
                }
                paren_depth--;
                current_arg.push_back(stream.consume());
            } else if (arg_tok.type == TokenType::Comma && paren_depth == 0) {
                arguments.push_back(
                    preprocessor_detail::trimTokens(current_arg));
                current_arg.clear();
                stream.consume();
            } else {
                current_arg.push_back(stream.consume());
            }
        }

        return arguments;
    }

    void expandFunctionLikeMacro(TokenStream& stream,
                                 std::vector<Token>& result, const Token& tok,
                                 const Macro& macro) {
        size_t pre_ws_start = result.size();
        while (!stream.is_eof() &&
               stream.peek().type == TokenType::Whitespace) {
            result.push_back(stream.consume());
        }

        if (stream.is_eof() || stream.peek().type != TokenType::Lparen) {
            result.emplace_back(TokenType::Identifier, tok.text, tok.line,
                                tok.column);
            return;
        }

        result.erase(result.begin() + static_cast<std::ptrdiff_t>(pre_ws_start),
                     result.end());

        stream.consume();

        std::vector<std::vector<Token>> arguments = parseMacroArguments(stream);

        if (arguments.size() != macro.params.size()) {
            if (!macro.params.empty() || arguments.size() != 1 ||
                !arguments[0].empty()) {
                throw PreprocessorError(std::format(
                    "Macro '{}' expects {} arguments, but {} were "
                    "provided",
                    macro.name, macro.params.size(), arguments.size()));
            }
            arguments.clear();
        }

        if (recursion_depth > MAX_RECURSION) {
            throw PreprocessorError("Macro expansion recursion limit reached");
        }

        std::set<std::string> params_next_to_concat;
        for (size_t i = 0; i < macro.body.size(); ++i) {
            if (macro.body[i].type != TokenType::Identifier) {
                continue;
            }

            bool is_param =
                std::ranges::contains(macro.params, macro.body[i].text);
            if (!is_param) {
                continue;
            }

            if (i > 0 && macro.body[i - 1].type == TokenType::Concat) {
                params_next_to_concat.insert(macro.body[i].text);
            }
            if (i + 1 < macro.body.size() &&
                macro.body[i + 1].type == TokenType::Concat) {
                params_next_to_concat.insert(macro.body[i].text);
            }
        }

        std::vector<std::vector<Token>> processed_args;
        Expander arg_expander(macros, recursion_depth + 1);
        for (size_t i = 0; i < arguments.size(); ++i) {
            const auto& param_name = macro.params[i];
            const auto& arg = arguments[i];

            if (params_next_to_concat.contains(param_name)) {
                processed_args.push_back(arg);
            } else {
                std::vector<Token> expanded_arg = arg_expander.expand(arg);
                processed_args.push_back(expanded_arg);
            }
        }

        std::vector<Token> substituted =
            substituteParams(macro.body, macro.params, processed_args);

        std::string initial_replacement =
            preprocessor_detail::tokensToString(substituted, false);

        substituted = foldTopLevelTernary(substituted);

        Expander nested_expander(macros, recursion_depth + 1);
        substituted = nested_expander.expand(substituted);

        auto nested_expansions = nested_expander.getExpansions();

        try {
            Evaluator evaluator(substituted);
            auto evaluated = evaluator.tryEvaluate();
            if (evaluated) {
                std::string value = Evaluator::toString(*evaluated);
                substituted.clear();
                substituted.emplace_back(TokenType::Number, value, tok.line,
                                         tok.column, *evaluated);
            }
        } catch (...) {
        }

        MacroExpansion expansion;
        expansion.macro_name = tok.text;
        expansion.original_line = tok.line;
        expansion.original_column = tok.column;
        expansion.replacement_text = initial_replacement;

        size_t expansion_idx = expansions.size();
        expansions.push_back(expansion);

        expansions.insert(expansions.end(), nested_expansions.begin(),
                          nested_expansions.end());

        if (!substituted.empty()) {
            Token begin_tok(TokenType::BeginMacroExpansion, "", tok.line,
                            tok.column);
            begin_tok.expansion_idx = expansion_idx;
            substituted.insert(substituted.begin(), begin_tok);

            Token end_tok(TokenType::EndMacroExpansion, "", tok.line,
                          tok.column);
            end_tok.expansion_idx = expansion_idx;
            substituted.push_back(end_tok);
        }

        stream.prepend(substituted);
    }

    void expandObjectLikeMacro(TokenStream& stream,
                               [[maybe_unused]] std::vector<Token>& result,
                               const Token& tok, const Macro& macro) {
        if (recursion_depth > MAX_RECURSION) {
            throw PreprocessorError("Macro expansion recursion limit reached");
        }

        std::string replacement_text =
            preprocessor_detail::tokensToString(macro.body, false);

        Expander nested_expander(macros, recursion_depth + 1);
        std::vector<Token> expanded = nested_expander.expand(macro.body);
        auto nested_expansions = nested_expander.getExpansions();

        MacroExpansion expansion;
        expansion.macro_name = tok.text;
        expansion.original_line = tok.line;
        expansion.original_column = tok.column;
        expansion.replacement_text = replacement_text;

        size_t expansion_idx = expansions.size();
        expansions.push_back(expansion);

        expansions.insert(expansions.end(), nested_expansions.begin(),
                          nested_expansions.end());

        if (!expanded.empty()) {
            Token begin_tok(TokenType::BeginMacroExpansion, "", tok.line,
                            tok.column);
            begin_tok.expansion_idx = expansion_idx;
            expanded.insert(expanded.begin(), begin_tok);

            Token end_tok(TokenType::EndMacroExpansion, "", tok.line,
                          tok.column);
            end_tok.expansion_idx = expansion_idx;
            expanded.push_back(end_tok);
        }

        stream.prepend(expanded);
    }

    std::vector<Token> foldTopLevelTernary(const std::vector<Token>& tokens) {
        if (tokens.empty()) {
            return tokens;
        }

        auto first = std::ranges::find_if(tokens, [](const Token& t) {
            return t.type != TokenType::Whitespace;
        });

        if (first == tokens.end() || first->type != TokenType::Lparen) {
            return tokens;
        }

        auto last =
            std::ranges::find_if(
                std::ranges::reverse_view(tokens),
                [](const Token& t) { return t.type != TokenType::Whitespace; })
                .base() -
            1;

        if (last == tokens.begin() || last->type != TokenType::Rparen) {
            return tokens;
        }

        std::vector<Token> core_tokens(first + 1, last);

        int paren_balance = 0;
        size_t question_pos = std::string::npos;
        size_t colon_pos = std::string::npos;

        for (size_t i = 0; i < core_tokens.size(); ++i) {
            const auto& token = core_tokens[i];
            if (token.type == TokenType::Lparen) {
                paren_balance++;
            } else if (token.type == TokenType::Rparen) {
                paren_balance--;
            } else if (paren_balance == 0 &&
                       token.type == TokenType::Question) {
                if (question_pos == std::string::npos) {
                    question_pos = i;
                }
            }
        }

        if (question_pos == std::string::npos) {
            return tokens;
        }

        int ternary_balance = 0;
        for (size_t i = question_pos + 1; i < core_tokens.size(); ++i) {
            const auto& token = core_tokens[i];
            if (token.type == TokenType::Lparen) {
                paren_balance++;
            } else if (token.type == TokenType::Rparen) {
                paren_balance--;
            } else if (paren_balance == 0 &&
                       token.type == TokenType::Question) {
                ternary_balance++;
            } else if (paren_balance == 0 && token.type == TokenType::Colon) {
                if (ternary_balance == 0) {
                    colon_pos = i;
                    break;
                }
                ternary_balance--;
            }
        }

        if (colon_pos == std::string::npos) {
            return tokens;
        }

        std::vector<Token> cond_tokens(
            core_tokens.begin(),
            core_tokens.begin() + static_cast<std::ptrdiff_t>(question_pos));

        try {
            Expander cond_expander(macros, recursion_depth + 1);
            auto expanded_cond = cond_expander.expand(cond_tokens);
            Evaluator evaluator(expanded_cond);
            auto result = evaluator.tryEvaluate();
            if (!result) {
                return tokens;
            }

            if (Evaluator::is_truthy(*result)) {
                return {core_tokens.begin() +
                            static_cast<std::ptrdiff_t>(question_pos + 1),
                        core_tokens.begin() +
                            static_cast<std::ptrdiff_t>(colon_pos)};
            }
            return {core_tokens.begin() +
                        static_cast<std::ptrdiff_t>(colon_pos + 1),
                    core_tokens.end()};
        } catch (...) {
            return tokens;
        }
    }

    std::vector<Token>
    substituteParams(const std::vector<Token>& body,
                     const std::vector<std::string>& params,
                     const std::vector<std::vector<Token>>& args) {

        std::vector<Token> result;

        for (const auto& tok : body) {
            if (tok.type == TokenType::Identifier) {
                auto it = std::ranges::find(params, tok.text);
                if (it != params.end()) {
                    size_t idx = std::ranges::distance(params.begin(), it);
                    if (!args[idx].empty()) {
                        result.insert(result.end(), args[idx].begin(),
                                      args[idx].end());
                    }
                    continue;
                }
            }
            result.push_back(tok);
        }

        while (true) {
            auto it = std::ranges::find_if(result, [](const Token& t) {
                return t.type == TokenType::Concat;
            });

            if (it == result.end()) {
                break;
            }

            auto lhs_it = it;
            bool left_has_value = false;

            if (lhs_it != result.begin()) {
                auto search_it = lhs_it - 1;
                while (true) {
                    if (!preprocessor_detail::isSkippable(*search_it)) {
                        lhs_it = search_it;
                        left_has_value = true;
                        break;
                    }
                    if (search_it == result.begin()) {
                        lhs_it = search_it;
                        left_has_value = false;
                        break;
                    }
                    search_it--;
                }
            } else {
                left_has_value = false;
            }

            auto rhs_it = it + 1;
            while (rhs_it != result.end() &&
                   preprocessor_detail::isSkippable(*rhs_it)) {
                rhs_it++;
            }

            std::string lhs_text = left_has_value ? lhs_it->text : "";
            std::string rhs_text = (rhs_it != result.end()) ? rhs_it->text : "";

            std::string new_text = lhs_text + rhs_text;

            PreprocessorTokenizer tokenizer(new_text);
            auto new_tokens = tokenizer.tokenize();
            new_tokens.erase(std::ranges::remove_if(
                                 new_tokens,
                                 [](const Token& t) {
                                     return t.type == TokenType::EndOfFile;
                                 })
                                 .begin(),
                             new_tokens.end());

            auto erase_end =
                (rhs_it == result.end()) ? result.end() : (rhs_it + 1);

            auto insert_pos = result.erase(lhs_it, erase_end);
            result.insert(insert_pos, new_tokens.begin(), new_tokens.end());
        }

        return result;
    }
};

} // namespace preprocessor

} // namespace infix2postfix

namespace infix2postfix {

class Preprocessor::Impl {
  public:
    explicit Impl(std::string source) : source(std::move(source)) {}

    void addPredefinedMacro(std::string name, const std::string& value) {
        PreprocessorTokenizer tokenizer(value);
        std::vector<Token> body_tokens = tokenizer.tokenize();

        std::erase_if(body_tokens, [](const Token& t) {
            return t.type == TokenType::EndOfFile;
        });

        preprocessor::Macro macro;
        macro.name = std::move(name);
        macro.is_function_like = false;
        macro.body = std::move(body_tokens);

        macros.define(std::move(macro));
    }

    PreprocessResult process() {
        output_lines.clear();
        line_mappings.clear();
        errors.clear();
        conditional_stack.clear();
        current_output_line = 1;
        included_libraries.clear();
        library_line_count = 0;

        PreprocessorTokenizer tokenizer(source);
        std::vector<Token> tokens = tokenizer.tokenize();

        processTokens(tokens);

        if (!conditional_stack.empty()) {
            addError(std::format(
                         "Unclosed @ifdef/@ifndef directive started at line {}",
                         conditional_stack.back().start_line),
                     tokens.empty() ? 0 : tokens.back().line);
        }

        PreprocessResult result;
        result.success = errors.empty();
        result.errors = errors;
        result.line_map = line_mappings;
        result.library_line_count = library_line_count;

        std::ostringstream oss;
        for (size_t i = 0; i < output_lines.size(); ++i) {
            oss << output_lines[i];
            if (i < output_lines.size() - 1) {
                oss << '\n';
            }
        }
        result.source = oss.str();

        return result;
    }

  private:
    struct ConditionalBlock {
        int start_line;
        bool is_active;
        bool had_true_branch;
    };

    std::string source;
    preprocessor::MacroTable macros;
    std::vector<std::string> output_lines;
    std::vector<LineMapping> line_mappings;
    std::vector<std::string> errors;
    std::vector<ConditionalBlock> conditional_stack;
    int current_output_line = 1;
    std::set<std::string_view, std::less<>> included_libraries;
    int library_line_count = 0;

    void processTokens(std::vector<Token>& tokens) {
        std::vector<Token> current_line_tokens;
        int current_line_number = 1;

        for (const Token& tok : tokens) {
            if (tok.type == TokenType::Newline) {
                processLineTokens(current_line_tokens, current_line_number);
                current_line_tokens.clear();
                current_line_number = tok.line + 1;
                continue;
            }

            if (tok.type != TokenType::EndOfFile) {
                current_line_tokens.push_back(tok);
            } else {
                break;
            }
        }

        if (!current_line_tokens.empty()) {
            processLineTokens(current_line_tokens, current_line_number);
        }
    }

    void processLineTokens(std::vector<Token>& line_tokens, int line_number) {
        if (line_tokens.empty()) {
            addOutputLine("", line_number);
            return;
        }

        size_t first_non_ws = 0;
        while (first_non_ws < line_tokens.size() &&
               (line_tokens[first_non_ws].type == TokenType::Whitespace ||
                line_tokens[first_non_ws].type == TokenType::Comment)) {
            first_non_ws++;
        }

        if (first_non_ws >= line_tokens.size()) {
            addOutputLine("", line_number);
            return;
        }

        const Token& first_tok = line_tokens[first_non_ws];
        if (first_tok.type >= TokenType::AtDefine &&
            first_tok.type <= TokenType::AtRequires) {
            handleDirective(line_tokens, line_number);
            addOutputLine("", line_number);
        } else if (!isCurrentBlockActive()) {
            addOutputLine("", line_number);
        } else {
            try {
                preprocessor::Expander expander(macros);
                std::vector<Token> expanded = expander.expand(line_tokens);
                auto expansions = expander.getExpansions();

                std::string line_text;
                int current_column = 1;
                for (const auto& tok : expanded) {
                    if (tok.type == TokenType::BeginMacroExpansion) {
                        if (tok.expansion_idx < expansions.size()) {
                            expansions[tok.expansion_idx]
                                .preprocessed_start_column = current_column;
                        }
                    } else if (tok.type == TokenType::EndMacroExpansion) {
                        if (tok.expansion_idx < expansions.size()) {
                            expansions[tok.expansion_idx]
                                .preprocessed_end_column = current_column;
                        }
                    } else {
                        line_text += tok.text;
                        current_column += static_cast<int>(tok.text.length());
                    }
                }

                addOutputLine(line_text, line_number);

                if (!line_mappings.empty()) {
                    line_mappings.back().expansions.insert(
                        line_mappings.back().expansions.end(),
                        expansions.begin(), expansions.end());
                }
            } catch (const PreprocessorError& e) {
                addError(e.what(), line_number);
                addOutputLine("", line_number);
            }
        }
    }

    void handleDirective(std::vector<Token>& line_tokens, int line_number) {
        preprocessor::TokenStream stream(line_tokens);

        stream.skipWhitespace();
        if (stream.is_eof()) {
            return;
        }

        const Token& directive_tok = stream.consume();

        switch (directive_tok.type) {
        case TokenType::AtDefine:
            if (isCurrentBlockActive()) {
                handleDefine(stream, line_number);
            }
            break;
        case TokenType::AtUndef:
            if (isCurrentBlockActive()) {
                handleUndef(stream, line_number);
            }
            break;
        case TokenType::AtIfdef:
            handleIfdef(stream, line_number, true);
            break;
        case TokenType::AtIfndef:
            handleIfdef(stream, line_number, false);
            break;
        case TokenType::AtIf:
            handleIf(stream, line_number);
            break;
        case TokenType::AtElse:
            handleElse(line_number);
            break;
        case TokenType::AtEndif:
            handleEndif(line_number);
            break;
        case TokenType::AtError:
            if (isCurrentBlockActive()) {
                handleError(stream, line_number);
            }
            break;
        case TokenType::AtRequires:
            if (isCurrentBlockActive()) {
                handleRequires(stream, line_number);
            }
            break;
        default:
            if (isCurrentBlockActive()) {
                addError(
                    std::format("Unknown directive '{}'", directive_tok.text),
                    line_number);
            }
            break;
        }
    }

    void handleDefine(preprocessor::TokenStream& stream, int line_number) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::Identifier) {
            addError("@define requires a macro name", line_number);
            return;
        }

        std::string name = stream.consume().text;

        preprocessor::Macro macro;
        macro.name = name;
        macro.is_function_like = false;

        if (!stream.is_eof() && stream.peek().type == TokenType::Lparen) {
            macro.is_function_like = true;
            stream.consume();

            stream.skipWhitespace();
            while (!stream.is_eof() &&
                   stream.peek().type != TokenType::Rparen) {
                stream.skipWhitespace();

                if (stream.peek().type != TokenType::Identifier) {
                    addError("Expected parameter name in macro definition",
                             line_number);
                    return;
                }

                std::string param = stream.consume().text;

                if (std::ranges::contains(macro.params, param)) {
                    addError(
                        std::format("Duplicate parameter name '{}' in macro "
                                    "definition",
                                    param),
                        line_number);
                    return;
                }

                macro.params.push_back(param);
                stream.skipWhitespace();

                if (stream.is_eof()) {
                    addError("Unterminated parameter list in macro definition",
                             line_number);
                    return;
                }

                if (stream.peek().type == TokenType::Comma) {
                    stream.consume();
                } else if (stream.peek().type != TokenType::Rparen) {
                    addError("Unterminated parameter list in macro definition",
                             line_number);
                    return;
                }
            }

            if (stream.is_eof() || stream.peek().type != TokenType::Rparen) {
                addError("Unterminated parameter list in macro definition",
                         line_number);
                return;
            }

            stream.consume();
        }

        stream.skipWhitespace();
        std::vector<Token> body_tokens;
        while (!stream.is_eof()) {
            body_tokens.push_back(stream.consume());
        }

        body_tokens = preprocessor_detail::trimTokens(body_tokens);

        if (!macro.is_function_like && !body_tokens.empty()) {
            try {
                preprocessor::Expander expander(macros);
                body_tokens = expander.expand(body_tokens);

                preprocessor::Evaluator evaluator(body_tokens);
                auto evaluated = evaluator.tryEvaluate();
                if (evaluated) {
                    std::string value =
                        preprocessor::Evaluator::toString(*evaluated);
                    body_tokens.clear();
                    body_tokens.emplace_back(TokenType::Number, value,
                                             line_number, 0, *evaluated);
                }
            } catch (...) {
            }
        }

        macro.body = std::move(body_tokens);
        macros.define(std::move(macro));
    }

    void handleUndef(preprocessor::TokenStream& stream, int line_number) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::Identifier) {
            addError("@undef requires a macro name", line_number);
            return;
        }

        std::string name = stream.consume().text;
        macros.undef(name);
    }

    void handleIfdef(preprocessor::TokenStream& stream, int line_number,
                     bool check_defined) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::Identifier) {
            const char* directive = check_defined ? "@ifdef" : "@ifndef";
            addError(std::format("{} requires a macro name", directive),
                     line_number);
            conditional_stack.push_back({line_number, false, true});
            return;
        }

        std::string name = stream.consume().text;

        bool parent_active = isCurrentBlockActive();
        bool macro_defined = macros.contains(name);
        bool condition_met = check_defined ? macro_defined : !macro_defined;
        bool is_active = parent_active && condition_met;

        conditional_stack.push_back({line_number, is_active, condition_met});
    }

    void handleIf(preprocessor::TokenStream& stream, int line_number) {
        stream.skipWhitespace();

        std::vector<Token> expr_tokens;
        while (!stream.is_eof()) {
            expr_tokens.push_back(stream.consume());
        }

        if (expr_tokens.empty()) {
            addError("@if requires an expression", line_number);
            conditional_stack.push_back({line_number, false, false});
            return;
        }

        bool parent_active = isCurrentBlockActive();
        bool condition_met = false;

        if (parent_active) {
            try {
                preprocessor::Expander expander(macros);
                expr_tokens = expander.expand(expr_tokens);

                preprocessor::Evaluator evaluator(expr_tokens);
                auto result = evaluator.evaluate();
                condition_met = preprocessor::Evaluator::is_truthy(result);
            } catch (const PreprocessorError& e) {
                addError(e.what(), line_number);
            } catch (const std::runtime_error& e) {
                addError(std::format("Failed to evaluate @if expression: {}",
                                     e.what()),
                         line_number);
            }
        }

        bool is_active = parent_active && condition_met;
        conditional_stack.push_back({line_number, is_active, condition_met});
    }

    void handleElse(int line_number) {
        if (conditional_stack.empty()) {
            addError("@else without matching @ifdef/@ifndef", line_number);
            return;
        }

        auto& block = conditional_stack.back();

        bool parent_active = true;
        if (conditional_stack.size() > 1) {
            parent_active =
                conditional_stack[conditional_stack.size() - 2].is_active;
        }

        block.is_active = parent_active && !block.had_true_branch;
    }

    void handleEndif(int line_number) {
        if (conditional_stack.empty()) {
            addError("@endif without matching @ifdef/@ifndef", line_number);
            return;
        }

        conditional_stack.pop_back();
    }

    void handleError(preprocessor::TokenStream& stream, int line_number) {
        stream.skipWhitespace();

        std::vector<Token> message_tokens;
        while (!stream.is_eof()) {
            message_tokens.push_back(stream.consume());
        }

        message_tokens = preprocessor_detail::trimTokens(message_tokens);
        std::string message =
            preprocessor_detail::tokensToString(message_tokens, true);

        addError(message.empty() ? "@error directive encountered"
                                 : std::format("@error: {}", message),
                 line_number);
    }

    void handleRequires(preprocessor::TokenStream& stream, int line_number) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::Identifier) {
            addError("@requires requires a library name", line_number);
            return;
        }

        std::string lib_name = stream.consume().text;

        std::vector<std::string_view> libraries_to_include;
        try {
            libraries_to_include =
                StandardLibraryManager::resolveDependencies(lib_name);
        } catch (const std::exception& e) {
            addError(std::format("Failed to resolve library '{}': {}", lib_name,
                                 e.what()),
                     line_number);
            return;
        }

        std::string_view explicitly_requested_lib = lib_name;

        for (const auto& lib : libraries_to_include) {
            if (included_libraries.contains(lib)) {
                continue;
            }

            auto lib_code_opt = StandardLibraryManager::getLibraryCode(lib);
            if (!lib_code_opt) {
                addError(
                    std::format("Library '{}' not found", std::string(lib)),
                    line_number);
                continue;
            }

            std::string lib_code = std::string(lib_code_opt.value());

            Preprocessor lib_preprocessor(lib_code);
            for (const auto& [name, macro] : macros) {
                lib_preprocessor.impl->macros.define(macro);
            }
            auto lib_result = lib_preprocessor.process();

            if (!lib_result.success) {
                addError(std::format("Failed to preprocess library '{}': {}",
                                     std::string(lib),
                                     lib_result.errors.empty()
                                         ? "unknown error"
                                         : lib_result.errors[0]),
                         line_number);
                continue;
            }

            for (const auto& [name, macro] : lib_preprocessor.impl->macros) {
                macros.define(macro);
            }

            std::vector<std::string> lib_lines;
            std::istringstream lib_stream(lib_result.source);
            std::string lib_line;
            while (std::getline(lib_stream, lib_line)) {
                lib_lines.push_back(lib_line);
            }

            if (lib == explicitly_requested_lib) {
                auto exports_opt = StandardLibraryManager::getExports(lib);
                if (exports_opt) {
                    std::string lib_name_upper;
                    for (char c : lib) {
                        lib_name_upper += static_cast<char>(std::toupper(c));
                    }

                    bool is_expr = macros.contains("__EXPR__");
                    bool is_single_expr = macros.contains("__SINGLEEXPR__");

                    for (const auto& exported : *exports_opt) {
                        if (exported.mode == stdlib::ExportMode::Expr &&
                            !is_expr) {
                            continue;
                        }
                        if (exported.mode == stdlib::ExportMode::SingleExpr &&
                            !is_single_expr) {
                            continue;
                        }

                        preprocessor::Macro alias_macro;
                        alias_macro.name = std::string(exported.name);

                        if (exported.param_count == 0) {
                            std::string body_str;
                            if (!exported.internal_name_override.empty()) {
                                body_str = std::string(
                                    exported.internal_name_override);
                            } else {
                                body_str = std::format(
                                    "___STDLIB_{}_{}", lib_name_upper,
                                    std::string(exported.name));
                            }
                            PreprocessorTokenizer tokenizer(body_str);
                            alias_macro.body = tokenizer.tokenize();
                            std::erase_if(alias_macro.body, [](const Token& t) {
                                return t.type == TokenType::EndOfFile;
                            });
                            alias_macro.is_function_like = false;
                        } else {
                            std::string internal_name;
                            if (!exported.internal_name_override.empty()) {
                                internal_name = std::string(
                                    exported.internal_name_override);
                            } else {
                                internal_name = std::format(
                                    "___stdlib_{}_{}", std::string(lib),
                                    std::string(exported.name));
                            }

                            alias_macro.is_function_like = true;
                            for (int i = 0; i < exported.param_count; ++i) {
                                alias_macro.params.push_back(
                                    std::format("__arg{}", i));
                            }

                            std::string body_str = internal_name + "(";
                            for (int i = 0; i < exported.param_count; ++i) {
                                if (i > 0) {
                                    body_str += ", ";
                                }
                                body_str += std::format("__arg{}", i);
                            }
                            body_str += ")";

                            PreprocessorTokenizer tokenizer(body_str);
                            alias_macro.body = tokenizer.tokenize();
                            std::erase_if(alias_macro.body, [](const Token& t) {
                                return t.type == TokenType::EndOfFile;
                            });
                        }

                        macros.define(std::move(alias_macro));
                    }
                }
            }

            for (const auto& lib_line : lib_lines | std::views::reverse) {
                output_lines.insert(output_lines.begin(), lib_line);

                LineMapping mapping;
                mapping.preprocessed_line = 1;
                mapping.original_line = -1;
                line_mappings.insert(line_mappings.begin(), mapping);
            }

            library_line_count += static_cast<int>(lib_lines.size());

            for (size_t i = lib_lines.size(); i < line_mappings.size(); ++i) {
                if (line_mappings[i].original_line > 0) {
                    line_mappings[i].preprocessed_line +=
                        static_cast<int>(lib_lines.size());
                }
            }

            for (size_t i = 0; i < lib_lines.size() && i < line_mappings.size();
                 ++i) {
                line_mappings[i].preprocessed_line = static_cast<int>(i + 1);
            }

            current_output_line += static_cast<int>(lib_lines.size());

            included_libraries.insert(lib);
        }
    }

    [[nodiscard]] bool isCurrentBlockActive() const {
        return std::ranges::all_of(conditional_stack, [](const auto& block) {
            return block.is_active;
        });
    }

    void addError(const std::string& message, int line) {
        errors.push_back(std::format("Line {}: {}", line, message));
    }

    void addOutputLine(const std::string& line, int original_line) {
        output_lines.push_back(line);

        LineMapping mapping;
        mapping.preprocessed_line = current_output_line;
        mapping.original_line = original_line;

        line_mappings.push_back(mapping);
        current_output_line++;
    }
};

Preprocessor::Preprocessor(std::string source)
    : impl(std::make_unique<Impl>(std::move(source))) {}

Preprocessor::~Preprocessor() = default;

void Preprocessor::addPredefinedMacro(std::string name,
                                      const std::string& value) {
    impl->addPredefinedMacro(std::move(name), value);
}

PreprocessResult Preprocessor::process() { return impl->process(); }

std::string Preprocessor::formatDiagnosticWithExpansion(
    const std::string& message, int line,
    const std::vector<LineMapping>& line_map) {

    const LineMapping* mapping = nullptr;
    for (const auto& m : line_map) {
        if (m.preprocessed_line == line) {
            mapping = &m;
            break;
        }
    }

    if (mapping == nullptr || mapping->expansions.empty()) {
        return message;
    }

    std::string result = message;
    result += "\n  Macro expansion trace:";

    for (const auto& expansion : mapping->expansions) {
        result += std::format("\n    {}:{}: in expansion of macro '{}'",
                              expansion.original_line,
                              expansion.original_column, expansion.macro_name);
        if (!expansion.replacement_text.empty()) {
            result += std::format(" -> '{}'", expansion.replacement_text);
        }
    }

    return result;
}

std::string
Preprocessor::formatMacroExpansions(const std::vector<LineMapping>& line_map) {
    std::string result;

    for (const auto& mapping : line_map) {
        if (!mapping.expansions.empty()) {
            for (const auto& expansion : mapping.expansions) {
                result += std::format(
                    "Line {} (original line {}:{}): macro '{}' expanded to "
                    "'{}'\n",
                    mapping.preprocessed_line, expansion.original_line,
                    expansion.original_column, expansion.macro_name,
                    expansion.replacement_text.empty()
                        ? "(empty)"
                        : expansion.replacement_text);
            }
        }
    }

    return result;
}

} // namespace infix2postfix
