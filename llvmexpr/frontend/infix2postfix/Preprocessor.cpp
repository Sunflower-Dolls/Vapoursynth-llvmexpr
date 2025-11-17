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
    IDENTIFIER,
    NUMBER,
    PLUS,
    MINUS,
    MULTIPLY,
    DIVIDE,
    MODULO,
    POWER,
    EQUAL,
    NOT_EQUAL,
    GREATER,
    GREATER_EQUAL,
    LESS,
    LESS_EQUAL,
    LOGICAL_AND,
    LOGICAL_OR,
    LOGICAL_NOT,
    BIT_AND,
    BIT_OR,
    BIT_XOR,
    BIT_NOT,
    LPAREN,
    RPAREN,
    LBRACKET,
    RBRACKET,
    LBRACE,
    RBRACE,
    COMMA,
    DOT,
    QUESTION,
    COLON,
    SEMICOLON,
    ASSIGN,
    AT_DEFINE,
    AT_UNDEF,
    AT_IFDEF,
    AT_IFNDEF,
    AT_IF,
    AT_ELSE,
    AT_ENDIF,
    AT_ERROR,
    AT_REQUIRES,
    AT,
    WHITESPACE,
    NEWLINE,
    COMMENT,
    END_OF_FILE,
    CONCAT,
    BEGIN_MACRO_EXPANSION,
    END_MACRO_EXPANSION,
};

struct Token {
    TokenType type;
    std::string text;
    int line;
    int column;
    std::variant<int64_t, double> numericValue;
    bool hasNumericValue = false;
    size_t expansion_idx = static_cast<size_t>(-1);

    Token() : type(TokenType::END_OF_FILE), line(0), column(0) {}

    Token(TokenType t, std::string txt, int ln, int col)
        : type(t), text(std::move(txt)), line(ln), column(col) {}

    Token(TokenType t, std::string txt, int ln, int col,
          const std::variant<int64_t, double>& val)
        : type(t), text(std::move(txt)), line(ln), column(col),
          numericValue(val), hasNumericValue(true) {}
};

class PreprocessorTokenizer {
  public:
    explicit PreprocessorTokenizer(std::string_view source) : Source(source) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (!eof()) {
            Token tok = nextToken();
            tokens.push_back(tok);
            if (tok.type == TokenType::END_OF_FILE) {
                break;
            }
        }
        return tokens;
    }

  private:
    std::string_view Source;
    size_t Pos = 0;
    int Line = 1;
    int Column = 1;

    [[nodiscard]] bool eof() const { return Pos >= Source.length(); }

    [[nodiscard]] char peek(size_t offset = 0) const {
        size_t p = Pos + offset;
        return p < Source.length() ? Source[p] : '\0';
    }

    [[nodiscard]] char peekSigned(int offset) const {
        if (offset < 0) {
            auto absOffset = static_cast<size_t>(-offset);
            if (absOffset > Pos) {
                return '\0';
            }
            return Source[Pos - absOffset];
        }
        return peek(static_cast<size_t>(offset));
    }

    char consume() {
        if (eof()) {
            return '\0';
        }
        char c = Source[Pos++];
        if (c == '\n') {
            Line++;
            Column = 1;
        } else {
            Column++;
        }
        return c;
    }

    Token nextToken() {
        if (eof()) {
            return {TokenType::END_OF_FILE, "", Line, Column};
        }

        int startLine = Line;
        int startColumn = Column;
        char c = peek();

        if (c == ' ' || c == '\t' || c == '\r') {
            return consumeWhitespace(startLine, startColumn);
        }
        if (c == '\n') {
            consume();
            return {TokenType::NEWLINE, "\n", startLine, startColumn};
        }
        if (c == '#') {
            return consumeComment(startLine, startColumn);
        }
        if (std::isdigit(c) != 0 || (c == '.' && std::isdigit(peek(1)) != 0)) {
            return consumeNumber(startLine, startColumn);
        }
        if (c == '@') {
            if (peek(1) == '@') {
                consume();
                consume();
                return {TokenType::CONCAT, "@@", startLine, startColumn};
            }
            return consumeDirective(startLine, startColumn);
        }
        if (std::isalpha(c) != 0 || c == '_' || c == '$') {
            return consumeIdentifier(startLine, startColumn);
        }
        return consumeOperator(startLine, startColumn);
    }

    Token consumeWhitespace(int startLine, int startColumn) {
        size_t start = Pos;
        while (!eof() && (peek() == ' ' || peek() == '\t' || peek() == '\r')) {
            consume();
        }
        std::string text(Source.substr(start, Pos - start));
        return {TokenType::WHITESPACE, text, startLine, startColumn};
    }

    Token consumeComment(int startLine, int startColumn) {
        size_t start = Pos;
        consume();
        while (!eof() && peek() != '\n') {
            consume();
        }
        std::string text(Source.substr(start, Pos - start));
        return {TokenType::COMMENT, text, startLine, startColumn};
    }

    Token consumeNumber(int startLine, int startColumn) {
        size_t start = Pos;

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

        std::string text(Source.substr(start, Pos - start));

        try {
            if (text.find('.') != std::string::npos ||
                text.find('e') != std::string::npos ||
                text.find('E') != std::string::npos) {
                double val = std::stod(text);
                return {TokenType::NUMBER, text, startLine, startColumn, val};
            }
            int64_t val = std::stoll(text, nullptr, 0);
            return {TokenType::NUMBER, text, startLine, startColumn, val};
        } catch (...) {
            return {TokenType::NUMBER, text, startLine, startColumn};
        }
    }

    Token consumeDirective(int startLine, int startColumn) {
        consume();

        if (eof() || (std::isalpha(peek()) == 0 && peek() != '_')) {
            return {TokenType::AT, "@", startLine, startColumn};
        }

        size_t start = Pos;
        while (!eof() && (std::isalnum(peek()) != 0 || peek() == '_')) {
            consume();
        }

        std::string directive(Source.substr(start, Pos - start));
        std::string fullText = "@" + directive;

        TokenType type = TokenType::AT;
        if (directive == "define") {
            type = TokenType::AT_DEFINE;
        } else if (directive == "undef") {
            type = TokenType::AT_UNDEF;
        } else if (directive == "ifdef") {
            type = TokenType::AT_IFDEF;
        } else if (directive == "ifndef") {
            type = TokenType::AT_IFNDEF;
        } else if (directive == "if") {
            type = TokenType::AT_IF;
        } else if (directive == "else") {
            type = TokenType::AT_ELSE;
        } else if (directive == "endif") {
            type = TokenType::AT_ENDIF;
        } else if (directive == "error") {
            type = TokenType::AT_ERROR;
        } else if (directive == "requires") {
            type = TokenType::AT_REQUIRES;
        }

        return {type, fullText, startLine, startColumn};
    }

    Token consumeIdentifier(int startLine, int startColumn) {
        size_t start = Pos;
        while (!eof() &&
               (std::isalnum(peek()) != 0 || peek() == '_' || peek() == '$')) {
            consume();
        }
        std::string text(Source.substr(start, Pos - start));
        return {TokenType::IDENTIFIER, text, startLine, startColumn};
    }

    Token consumeOperator(int startLine, int startColumn) {
        char c = peek();
        char next = peek(1);

        if (c == '*' && next == '*') {
            consume();
            consume();
            return {TokenType::POWER, "**", startLine, startColumn};
        }
        if (c == '=' && next == '=') {
            consume();
            consume();
            return {TokenType::EQUAL, "==", startLine, startColumn};
        }
        if (c == '!' && next == '=') {
            consume();
            consume();
            return {TokenType::NOT_EQUAL, "!=", startLine, startColumn};
        }
        if (c == '>' && next == '=') {
            consume();
            consume();
            return {TokenType::GREATER_EQUAL, ">=", startLine, startColumn};
        }
        if (c == '<' && next == '=') {
            consume();
            consume();
            return {TokenType::LESS_EQUAL, "<=", startLine, startColumn};
        }
        if (c == '&' && next == '&') {
            consume();
            consume();
            return {TokenType::LOGICAL_AND, "&&", startLine, startColumn};
        }
        if (c == '|' && next == '|') {
            consume();
            consume();
            return {TokenType::LOGICAL_OR, "||", startLine, startColumn};
        }

        consume();
        TokenType type = TokenType::END_OF_FILE;
        std::string text(1, c);

        switch (c) {
        case '+':
            type = TokenType::PLUS;
            break;
        case '-':
            type = TokenType::MINUS;
            break;
        case '*':
            type = TokenType::MULTIPLY;
            break;
        case '/':
            type = TokenType::DIVIDE;
            break;
        case '%':
            type = TokenType::MODULO;
            break;
        case '>':
            type = TokenType::GREATER;
            break;
        case '<':
            type = TokenType::LESS;
            break;
        case '!':
            type = TokenType::LOGICAL_NOT;
            break;
        case '&':
            type = TokenType::BIT_AND;
            break;
        case '|':
            type = TokenType::BIT_OR;
            break;
        case '^':
            type = TokenType::BIT_XOR;
            break;
        case '~':
            type = TokenType::BIT_NOT;
            break;
        case '(':
            type = TokenType::LPAREN;
            break;
        case ')':
            type = TokenType::RPAREN;
            break;
        case '[':
            type = TokenType::LBRACKET;
            break;
        case ']':
            type = TokenType::RBRACKET;
            break;
        case '{':
            type = TokenType::LBRACE;
            break;
        case '}':
            type = TokenType::RBRACE;
            break;
        case ',':
            type = TokenType::COMMA;
            break;
        case '.':
            type = TokenType::DOT;
            break;
        case '?':
            type = TokenType::QUESTION;
            break;
        case ':':
            type = TokenType::COLON;
            break;
        case ';':
            type = TokenType::SEMICOLON;
            break;
        case '=':
            type = TokenType::ASSIGN;
            break;
        default:
            throw PreprocessorError(
                std::format("Unexpected character '{}' at line {}, column {}",
                            c, startLine, startColumn));
        }

        return {type, text, startLine, startColumn};
    }
};

std::string tokensToString(const std::vector<Token>& tokens,
                           bool preserveWhitespace = false) {
    std::string result;
    for (const auto& tok : tokens) {
        if (!preserveWhitespace && (tok.type == TokenType::WHITESPACE ||
                                    tok.type == TokenType::COMMENT)) {
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
           (tokens[start].type == TokenType::WHITESPACE ||
            tokens[start].type == TokenType::COMMENT)) {
        start++;
    }

    size_t end = tokens.size();
    while (end > start && (tokens[end - 1].type == TokenType::WHITESPACE ||
                           tokens[end - 1].type == TokenType::COMMENT)) {
        end--;
    }

    return {tokens.begin() + static_cast<std::ptrdiff_t>(start),
            tokens.begin() + static_cast<std::ptrdiff_t>(end)};
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
        return tokens.empty() || tokens.front().type == TokenType::END_OF_FILE;
    }

    [[nodiscard]] Token peek(size_t offset = 0) const {
        if (offset >= tokens.size()) {
            static Token eofToken{TokenType::END_OF_FILE, "", 0, 0};
            return eofToken;
        }
        return tokens[offset];
    }

    Token consume() {
        if (is_eof()) {
            static Token eofToken{TokenType::END_OF_FILE, "", 0, 0};
            return eofToken;
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
        while (!is_eof() && (peek().type == TokenType::WHITESPACE ||
                             peek().type == TokenType::COMMENT ||
                             peek().type == TokenType::BEGIN_MACRO_EXPANSION ||
                             peek().type == TokenType::END_MACRO_EXPANSION)) {
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
        return pos >= tokens.size() ||
               tokens[pos].type == TokenType::END_OF_FILE;
    }

    [[nodiscard]] const Token& peek(size_t offset = 0) const {
        size_t p = pos + offset;
        if (p >= tokens.size()) {
            static Token eofToken{TokenType::END_OF_FILE, "", 0, 0};
            return eofToken;
        }
        return tokens[p];
    }

    Token consume() {
        if (is_eof()) {
            static Token eofToken{TokenType::END_OF_FILE, "", 0, 0};
            return eofToken;
        }
        return tokens[pos++];
    }

    void skipWhitespace() {
        while (!is_eof() && (peek().type == TokenType::WHITESPACE ||
                             peek().type == TokenType::COMMENT ||
                             peek().type == TokenType::BEGIN_MACRO_EXPANSION ||
                             peek().type == TokenType::END_MACRO_EXPANSION)) {
            consume();
        }
    }

    Value parsePrimary() {
        skipWhitespace();

        const Token& tok = peek();

        if (tok.type == TokenType::NUMBER) {
            consume();
            if (tok.hasNumericValue) {
                return Value(tok.numericValue);
            }
            try {
                if (tok.text.find('.') != std::string::npos ||
                    tok.text.find('e') != std::string::npos ||
                    tok.text.find('E') != std::string::npos) {
                    return {std::stod(tok.text)};
                }
                return {std::stoll(tok.text, nullptr, 0)};
            } catch (...) {
                throw std::runtime_error("Invalid number: " + tok.text);
            }
        }

        if (tok.type == TokenType::IDENTIFIER) {
            std::string name = tok.text;
            throw std::runtime_error(
                "Unexpanded identifier in constant expression: " + name);
        }

        if (tok.type == TokenType::LPAREN) {
            consume();
            skipWhitespace();
            Value val = parseConditional();
            skipWhitespace();
            if (peek().type != TokenType::RPAREN) {
                throw std::runtime_error("Expected ')'");
            }
            consume();
            return val;
        }

        throw std::runtime_error("Unexpected token in expression: " + tok.text);
    }

    Value parseUnary() {
        skipWhitespace();

        if (peek().type == TokenType::MINUS) {
            consume();
            skipWhitespace();
            Value val = parseUnary();
            if (val.is_double()) {
                return {-val.to_double()};
            }
            return {-std::get<int64_t>(val.val)};
        }

        if (peek().type == TokenType::LOGICAL_NOT) {
            consume();
            skipWhitespace();
            return {static_cast<int64_t>(!parseUnary().is_truthy())};
        }

        if (peek().type == TokenType::PLUS) {
            consume();
            skipWhitespace();
            return parseUnary();
        }

        if (peek().type == TokenType::BIT_NOT) {
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

        if (peek().type == TokenType::POWER) {
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

            if (op.type != TokenType::MULTIPLY &&
                op.type != TokenType::DIVIDE && op.type != TokenType::MODULO) {
                break;
            }

            consume();
            skipWhitespace();
            Value right = parsePower();

            if (left.is_double() || right.is_double()) {
                double l = left.to_double();
                double r = right.to_double();

                switch (op.type) {
                case TokenType::MULTIPLY:
                    left = Value(l * r);
                    break;
                case TokenType::DIVIDE:
                    left = Value(l / r);
                    break;
                case TokenType::MODULO:
                    throw std::runtime_error(
                        "Modulo requires integer operands");
                default:
                    std::unreachable();
                }
            } else {
                int64_t l = std::get<int64_t>(left.val);
                int64_t r = std::get<int64_t>(right.val);

                switch (op.type) {
                case TokenType::MULTIPLY:
                    left = Value(l * r);
                    break;
                case TokenType::DIVIDE:
                    left = Value(l / r);
                    break;
                case TokenType::MODULO:
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

            if (op.type != TokenType::PLUS && op.type != TokenType::MINUS) {
                break;
            }

            consume();
            skipWhitespace();
            Value right = parseFactor();

            if (left.is_double() || right.is_double()) {
                double l = left.to_double();
                double r = right.to_double();
                left = Value(op.type == TokenType::PLUS ? (l + r) : (l - r));
            } else {
                int64_t l = std::get<int64_t>(left.val);
                int64_t r = std::get<int64_t>(right.val);
                left = Value(op.type == TokenType::PLUS ? (l + r) : (l - r));
            }
        }

        return left;
    }

    Value parseBitwiseOr() {
        Value left = parseBitwiseXor();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::BIT_OR) {
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

            if (op.type != TokenType::BIT_XOR) {
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

            if (op.type != TokenType::BIT_AND) {
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

            if (op.type != TokenType::EQUAL &&
                op.type != TokenType::NOT_EQUAL) {
                break;
            }

            TokenType opType = op.type;
            consume();
            skipWhitespace();
            Value right = parseComparison();

            double l = left.to_double();
            double r = right.to_double();
            bool result = (opType == TokenType::EQUAL) ? (l == r) : (l != r);

            left = Value(static_cast<int64_t>(result));
        }

        return left;
    }

    Value parseComparison() {
        Value left = parseTerm();

        while (true) {
            skipWhitespace();
            const Token& op = peek();

            if (op.type != TokenType::GREATER &&
                op.type != TokenType::GREATER_EQUAL &&
                op.type != TokenType::LESS &&
                op.type != TokenType::LESS_EQUAL) {
                break;
            }

            TokenType opType = op.type;
            consume();
            skipWhitespace();
            Value right = parseTerm();

            double l = left.to_double();
            double r = right.to_double();
            bool result = false;

            switch (opType) {
            case TokenType::GREATER:
                result = l > r;
                break;
            case TokenType::GREATER_EQUAL:
                result = l >= r;
                break;
            case TokenType::LESS:
                result = l < r;
                break;
            case TokenType::LESS_EQUAL:
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
            if (peek().type != TokenType::LOGICAL_AND) {
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
            if (peek().type != TokenType::LOGICAL_OR) {
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

        if (peek().type != TokenType::QUESTION) {
            return condition;
        }

        consume();
        bool condTruthy = condition.is_truthy();
        skipWhitespace();

        if (condTruthy) {
            Value thenVal = parseConditional();
            skipWhitespace();
            if (peek().type != TokenType::COLON) {
                throw std::runtime_error(
                    "Expected ':' in conditional expression");
            }
            consume();
            skipElseBranch();
            return thenVal;
        }

        skipThenBranchToColon();
        skipWhitespace();
        if (peek().type != TokenType::COLON) {
            throw std::runtime_error("Expected ':' in conditional expression");
        }
        consume();
        skipWhitespace();
        Value elseVal = parseConditional();
        return elseVal;
    }

    void skipThenBranchToColon() {
        int nested = 0;
        while (!is_eof()) {
            const Token& tok = peek();

            if (tok.type == TokenType::QUESTION) {
                nested++;
                consume();
            } else if (tok.type == TokenType::COLON) {
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
        int parenDepth = 0;

        while (!is_eof()) {
            const Token& tok = peek();

            if (tok.type == TokenType::LPAREN) {
                parenDepth++;
                consume();
            } else if (tok.type == TokenType::RPAREN) {
                if (parenDepth == 0) {
                    break;
                }
                parenDepth--;
                consume();
            } else if (tok.type == TokenType::COMMA && parenDepth == 0) {
                break;
            } else if (tok.type == TokenType::QUESTION) {
                nested++;
                consume();
            } else if (tok.type == TokenType::COLON) {
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
    Expander(const MacroTable& macros, int recursionDepth = 0)
        : macros(macros), recursion_depth(recursionDepth) {}

    std::vector<Token> expand(const std::vector<Token>& input) {
        TokenStream stream(input);
        std::vector<Token> result;

        while (!stream.is_eof()) {
            const auto tok = stream.peek();

            if (tok.type == TokenType::IDENTIFIER) {
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

                Token macroToken = stream.consume();

                if (macro->is_function_like) {
                    expandFunctionLikeMacro(stream, result, macroToken, *macro);
                } else {
                    expandObjectLikeMacro(stream, result, macroToken, *macro);
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
        int defLine = tok.line;
        int defCol = tok.column;
        stream.consume();
        stream.skipWhitespace();

        bool hasParen = false;
        if (!stream.is_eof() && stream.peek().type == TokenType::LPAREN) {
            hasParen = true;
            stream.consume();
            stream.skipWhitespace();
        }

        if (!stream.is_eof() && stream.peek().type == TokenType::IDENTIFIER) {
            Token macroToken = stream.consume();
            std::string macroName = macroToken.text;

            if (hasParen) {
                stream.skipWhitespace();
                if (!stream.is_eof() &&
                    stream.peek().type == TokenType::RPAREN) {
                    stream.consume();
                }
            }

            std::string value = macros.contains(macroName) ? "1" : "0";
            result.emplace_back(
                TokenType::NUMBER, value, defLine, defCol,
                static_cast<int64_t>(macros.contains(macroName) ? 1 : 0));
        }
    }

    void handleConstevalIntrinsics(TokenStream& stream,
                                   std::vector<Token>& result,
                                   const Token& tok) {
        bool isProbe = (tok.text == "is_consteval");
        int startLine = tok.line;
        int startCol = tok.column;

        stream.consume();
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::LPAREN) {
            result.emplace_back(TokenType::IDENTIFIER, tok.text, startLine,
                                startCol);
            return;
        }

        stream.consume();

        std::vector<Token> argTokens;
        int depth = 0;
        bool found = false;

        while (!stream.is_eof()) {
            const auto t = stream.peek();

            if (t.type == TokenType::LPAREN) {
                depth++;
                argTokens.push_back(stream.consume());
            } else if (t.type == TokenType::RPAREN) {
                if (depth == 0) {
                    found = true;
                    stream.consume();
                    break;
                }
                depth--;
                argTokens.push_back(stream.consume());
            } else {
                argTokens.push_back(stream.consume());
            }
        }

        if (!found) {
            result.emplace_back(TokenType::IDENTIFIER, tok.text, startLine,
                                startCol);
            return;
        }

        argTokens = expand(argTokens);

        std::optional<std::variant<int64_t, double>> maybe;
        try {
            Evaluator evaluator(argTokens);
            maybe = evaluator.tryEvaluate();
        } catch (const PreprocessorError&) {
            if (isProbe) {
                maybe = std::nullopt;
            } else {
                throw;
            }
        } catch (...) {
            maybe = std::nullopt;
        }

        if (isProbe) {
            std::string value = maybe ? "1" : "0";
            result.emplace_back(TokenType::NUMBER, value, startLine, startCol,
                                static_cast<int64_t>(maybe ? 1 : 0));
        } else {
            if (!maybe) {
                throw PreprocessorError(
                    "consteval() requires a constant expression");
            }
            std::string value = Evaluator::toString(*maybe);
            result.emplace_back(TokenType::NUMBER, value, startLine, startCol,
                                *maybe);
        }
    }

    void handleStaticAssertIntrinsic(TokenStream& stream,
                                     std::vector<Token>& result,
                                     const Token& tok) {
        int startLine = tok.line;
        int startCol = tok.column;

        stream.consume();
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::LPAREN) {
            result.emplace_back(TokenType::IDENTIFIER, tok.text, startLine,
                                startCol);
            return;
        }

        stream.consume();

        std::vector<std::vector<Token>> args = parseMacroArguments(stream);
        if (args.size() != 2) {
            throw PreprocessorError(
                "static_assert() expects 2 arguments: condition, message");
        }

        Expander nestedExpander(macros, recursion_depth + 1);
        std::vector<Token> condTokens = nestedExpander.expand(args[0]);
        std::vector<Token> msgTokens = nestedExpander.expand(args[1]);

        std::optional<std::variant<int64_t, double>> maybe;
        try {
            Evaluator evaluator(condTokens);
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
                preprocessor_detail::tokensToString(msgTokens, true);
            if (message.empty()) {
                message = "static_assert condition is false";
            }
            throw PreprocessorError(
                std::format("static_assert failed: {}", message));
        }

        result.emplace_back(TokenType::NUMBER, "1", startLine, startCol,
                            static_cast<int64_t>(1));
    }

    std::vector<std::vector<Token>> parseMacroArguments(TokenStream& stream) {
        std::vector<std::vector<Token>> arguments;
        std::vector<Token> currentArg;
        int parenDepth = 0;

        while (!stream.is_eof()) {
            const auto argTok = stream.peek();

            if (argTok.type == TokenType::LPAREN) {
                parenDepth++;
                currentArg.push_back(stream.consume());
            } else if (argTok.type == TokenType::RPAREN) {
                if (parenDepth == 0) {
                    arguments.push_back(
                        preprocessor_detail::trimTokens(currentArg));
                    stream.consume();
                    break;
                }
                parenDepth--;
                currentArg.push_back(stream.consume());
            } else if (argTok.type == TokenType::COMMA && parenDepth == 0) {
                arguments.push_back(
                    preprocessor_detail::trimTokens(currentArg));
                currentArg.clear();
                stream.consume();
            } else {
                currentArg.push_back(stream.consume());
            }
        }

        return arguments;
    }

    void expandFunctionLikeMacro(TokenStream& stream,
                                 std::vector<Token>& result, const Token& tok,
                                 const Macro& macro) {
        size_t preWsStart = result.size();
        while (!stream.is_eof() &&
               stream.peek().type == TokenType::WHITESPACE) {
            result.push_back(stream.consume());
        }

        if (stream.is_eof() || stream.peek().type != TokenType::LPAREN) {
            result.emplace_back(TokenType::IDENTIFIER, tok.text, tok.line,
                                tok.column);
            return;
        }

        result.erase(result.begin() + static_cast<std::ptrdiff_t>(preWsStart),
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

        std::set<std::string> paramsNextToConcat;
        for (size_t i = 0; i < macro.body.size(); ++i) {
            if (macro.body[i].type != TokenType::IDENTIFIER) {
                continue;
            }

            bool isParam =
                std::ranges::contains(macro.params, macro.body[i].text);
            if (!isParam) {
                continue;
            }

            if (i > 0 && macro.body[i - 1].type == TokenType::CONCAT) {
                paramsNextToConcat.insert(macro.body[i].text);
            }
            if (i + 1 < macro.body.size() &&
                macro.body[i + 1].type == TokenType::CONCAT) {
                paramsNextToConcat.insert(macro.body[i].text);
            }
        }

        std::vector<std::vector<Token>> processedArgs;
        Expander argExpander(macros, recursion_depth + 1);
        for (size_t i = 0; i < arguments.size(); ++i) {
            const auto& paramName = macro.params[i];
            const auto& arg = arguments[i];

            if (paramsNextToConcat.contains(paramName)) {
                processedArgs.push_back(arg);
            } else {
                std::vector<Token> expandedArg = argExpander.expand(arg);
                processedArgs.push_back(expandedArg);
            }
        }

        std::vector<Token> substituted =
            substituteParams(macro.body, macro.params, processedArgs);

        std::string initialReplacement =
            preprocessor_detail::tokensToString(substituted, false);

        substituted = foldTopLevelTernary(substituted);

        Expander nestedExpander(macros, recursion_depth + 1);
        substituted = nestedExpander.expand(substituted);

        auto nestedExpansions = nestedExpander.getExpansions();

        try {
            Evaluator evaluator(substituted);
            auto evaluated = evaluator.tryEvaluate();
            if (evaluated) {
                std::string value = Evaluator::toString(*evaluated);
                substituted.clear();
                substituted.emplace_back(TokenType::NUMBER, value, tok.line,
                                         tok.column, *evaluated);
            }
        } catch (...) {
        }

        MacroExpansion expansion;
        expansion.macro_name = tok.text;
        expansion.original_line = tok.line;
        expansion.original_column = tok.column;
        expansion.replacement_text = initialReplacement;

        size_t expansion_idx = expansions.size();
        expansions.push_back(expansion);

        expansions.insert(expansions.end(), nestedExpansions.begin(),
                          nestedExpansions.end());

        if (!substituted.empty()) {
            Token begin_tok(TokenType::BEGIN_MACRO_EXPANSION, "", tok.line,
                            tok.column);
            begin_tok.expansion_idx = expansion_idx;
            substituted.insert(substituted.begin(), begin_tok);

            Token end_tok(TokenType::END_MACRO_EXPANSION, "", tok.line,
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

        std::string replacementText =
            preprocessor_detail::tokensToString(macro.body, false);

        Expander nestedExpander(macros, recursion_depth + 1);
        std::vector<Token> expanded = nestedExpander.expand(macro.body);
        auto nestedExpansions = nestedExpander.getExpansions();

        MacroExpansion expansion;
        expansion.macro_name = tok.text;
        expansion.original_line = tok.line;
        expansion.original_column = tok.column;
        expansion.replacement_text = replacementText;

        size_t expansion_idx = expansions.size();
        expansions.push_back(expansion);

        expansions.insert(expansions.end(), nestedExpansions.begin(),
                          nestedExpansions.end());

        if (!expanded.empty()) {
            Token begin_tok(TokenType::BEGIN_MACRO_EXPANSION, "", tok.line,
                            tok.column);
            begin_tok.expansion_idx = expansion_idx;
            expanded.insert(expanded.begin(), begin_tok);

            Token end_tok(TokenType::END_MACRO_EXPANSION, "", tok.line,
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
            return t.type != TokenType::WHITESPACE;
        });

        if (first == tokens.end() || first->type != TokenType::LPAREN) {
            return tokens;
        }

        auto last =
            std::ranges::find_if(
                std::ranges::reverse_view(tokens),
                [](const Token& t) { return t.type != TokenType::WHITESPACE; })
                .base() -
            1;

        if (last == tokens.begin() || last->type != TokenType::RPAREN) {
            return tokens;
        }

        std::vector<Token> core_tokens(first + 1, last);

        int paren_balance = 0;
        size_t question_pos = std::string::npos;
        size_t colon_pos = std::string::npos;

        for (size_t i = 0; i < core_tokens.size(); ++i) {
            const auto& token = core_tokens[i];
            if (token.type == TokenType::LPAREN) {
                paren_balance++;
            } else if (token.type == TokenType::RPAREN) {
                paren_balance--;
            } else if (paren_balance == 0 &&
                       token.type == TokenType::QUESTION) {
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
            if (token.type == TokenType::LPAREN) {
                paren_balance++;
            } else if (token.type == TokenType::RPAREN) {
                paren_balance--;
            } else if (paren_balance == 0 &&
                       token.type == TokenType::QUESTION) {
                ternary_balance++;
            } else if (paren_balance == 0 && token.type == TokenType::COLON) {
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
            Expander condExpander(macros, recursion_depth + 1);
            auto expanded_cond = condExpander.expand(cond_tokens);
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
            if (tok.type == TokenType::IDENTIFIER) {
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
                return t.type == TokenType::CONCAT;
            });

            if (it == result.end()) {
                break;
            }

            if (it == result.begin() || (it + 1) == result.end()) {
                result.erase(it);
                continue;
            }

            auto lhs_it = it - 1;
            auto rhs_it = it + 1;

            std::string newText = lhs_it->text + rhs_it->text;

            PreprocessorTokenizer tokenizer(newText);
            auto newTokens = tokenizer.tokenize();
            newTokens.erase(std::ranges::remove_if(
                                newTokens,
                                [](const Token& t) {
                                    return t.type == TokenType::END_OF_FILE;
                                })
                                .begin(),
                            newTokens.end());

            auto insert_pos = result.erase(lhs_it, rhs_it + 1);
            result.insert(insert_pos, newTokens.begin(), newTokens.end());
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
        std::vector<Token> bodyTokens = tokenizer.tokenize();

        std::erase_if(bodyTokens, [](const Token& t) {
            return t.type == TokenType::END_OF_FILE;
        });

        preprocessor::Macro macro;
        macro.name = std::move(name);
        macro.is_function_like = false;
        macro.body = std::move(bodyTokens);

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
        std::vector<Token> currentLineTokens;
        int currentLineNumber = 1;

        for (const Token& tok : tokens) {
            if (tok.type == TokenType::NEWLINE) {
                processLineTokens(currentLineTokens, currentLineNumber);
                currentLineTokens.clear();
                currentLineNumber = tok.line + 1;
                continue;
            }

            if (tok.type != TokenType::END_OF_FILE) {
                currentLineTokens.push_back(tok);
            } else {
                break;
            }
        }

        if (!currentLineTokens.empty()) {
            processLineTokens(currentLineTokens, currentLineNumber);
        }
    }

    void processLineTokens(std::vector<Token>& lineTokens, int lineNumber) {
        if (lineTokens.empty()) {
            addOutputLine("", lineNumber);
            return;
        }

        size_t firstNonWs = 0;
        while (firstNonWs < lineTokens.size() &&
               (lineTokens[firstNonWs].type == TokenType::WHITESPACE ||
                lineTokens[firstNonWs].type == TokenType::COMMENT)) {
            firstNonWs++;
        }

        if (firstNonWs >= lineTokens.size()) {
            addOutputLine("", lineNumber);
            return;
        }

        const Token& firstTok = lineTokens[firstNonWs];
        if (firstTok.type >= TokenType::AT_DEFINE &&
            firstTok.type <= TokenType::AT_REQUIRES) {
            handleDirective(lineTokens, lineNumber);
            addOutputLine("", lineNumber);
        } else if (!isCurrentBlockActive()) {
            addOutputLine("", lineNumber);
        } else {
            try {
                preprocessor::Expander expander(macros);
                std::vector<Token> expanded = expander.expand(lineTokens);
                auto expansions = expander.getExpansions();

                std::string lineText;
                int current_column = 1;
                for (const auto& tok : expanded) {
                    if (tok.type == TokenType::BEGIN_MACRO_EXPANSION) {
                        if (tok.expansion_idx < expansions.size()) {
                            expansions[tok.expansion_idx]
                                .preprocessed_start_column = current_column;
                        }
                    } else if (tok.type == TokenType::END_MACRO_EXPANSION) {
                        if (tok.expansion_idx < expansions.size()) {
                            expansions[tok.expansion_idx]
                                .preprocessed_end_column = current_column;
                        }
                    } else {
                        lineText += tok.text;
                        current_column += static_cast<int>(tok.text.length());
                    }
                }

                addOutputLine(lineText, lineNumber);

                if (!line_mappings.empty()) {
                    line_mappings.back().expansions.insert(
                        line_mappings.back().expansions.end(),
                        expansions.begin(), expansions.end());
                }
            } catch (const PreprocessorError& e) {
                addError(e.what(), lineNumber);
                addOutputLine("", lineNumber);
            }
        }
    }

    void handleDirective(std::vector<Token>& lineTokens, int lineNumber) {
        preprocessor::TokenStream stream(lineTokens);

        stream.skipWhitespace();
        if (stream.is_eof()) {
            return;
        }

        const Token& directiveTok = stream.consume();

        switch (directiveTok.type) {
        case TokenType::AT_DEFINE:
            if (isCurrentBlockActive()) {
                handleDefine(stream, lineNumber);
            }
            break;
        case TokenType::AT_UNDEF:
            if (isCurrentBlockActive()) {
                handleUndef(stream, lineNumber);
            }
            break;
        case TokenType::AT_IFDEF:
            handleIfdef(stream, lineNumber, true);
            break;
        case TokenType::AT_IFNDEF:
            handleIfdef(stream, lineNumber, false);
            break;
        case TokenType::AT_IF:
            handleIf(stream, lineNumber);
            break;
        case TokenType::AT_ELSE:
            handleElse(lineNumber);
            break;
        case TokenType::AT_ENDIF:
            handleEndif(lineNumber);
            break;
        case TokenType::AT_ERROR:
            if (isCurrentBlockActive()) {
                handleError(stream, lineNumber);
            }
            break;
        case TokenType::AT_REQUIRES:
            if (isCurrentBlockActive()) {
                handleRequires(stream, lineNumber);
            }
            break;
        default:
            if (isCurrentBlockActive()) {
                addError(
                    std::format("Unknown directive '{}'", directiveTok.text),
                    lineNumber);
            }
            break;
        }
    }

    void handleDefine(preprocessor::TokenStream& stream, int lineNumber) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::IDENTIFIER) {
            addError("@define requires a macro name", lineNumber);
            return;
        }

        std::string name = stream.consume().text;

        preprocessor::Macro macro;
        macro.name = name;
        macro.is_function_like = false;

        if (!stream.is_eof() && stream.peek().type == TokenType::LPAREN) {
            macro.is_function_like = true;
            stream.consume();

            stream.skipWhitespace();
            while (!stream.is_eof() &&
                   stream.peek().type != TokenType::RPAREN) {
                stream.skipWhitespace();

                if (stream.peek().type != TokenType::IDENTIFIER) {
                    addError("Expected parameter name in macro definition",
                             lineNumber);
                    return;
                }

                std::string param = stream.consume().text;

                if (std::ranges::contains(macro.params, param)) {
                    addError(
                        std::format("Duplicate parameter name '{}' in macro "
                                    "definition",
                                    param),
                        lineNumber);
                    return;
                }

                macro.params.push_back(param);
                stream.skipWhitespace();

                if (stream.is_eof()) {
                    addError("Unterminated parameter list in macro definition",
                             lineNumber);
                    return;
                }

                if (stream.peek().type == TokenType::COMMA) {
                    stream.consume();
                } else if (stream.peek().type != TokenType::RPAREN) {
                    addError("Unterminated parameter list in macro definition",
                             lineNumber);
                    return;
                }
            }

            if (stream.is_eof() || stream.peek().type != TokenType::RPAREN) {
                addError("Unterminated parameter list in macro definition",
                         lineNumber);
                return;
            }

            stream.consume();
        }

        stream.skipWhitespace();
        std::vector<Token> bodyTokens;
        while (!stream.is_eof()) {
            bodyTokens.push_back(stream.consume());
        }

        bodyTokens = preprocessor_detail::trimTokens(bodyTokens);

        if (!macro.is_function_like && !bodyTokens.empty()) {
            try {
                preprocessor::Expander expander(macros);
                bodyTokens = expander.expand(bodyTokens);

                preprocessor::Evaluator evaluator(bodyTokens);
                auto evaluated = evaluator.tryEvaluate();
                if (evaluated) {
                    std::string value =
                        preprocessor::Evaluator::toString(*evaluated);
                    bodyTokens.clear();
                    bodyTokens.emplace_back(TokenType::NUMBER, value,
                                            lineNumber, 0, *evaluated);
                }
            } catch (...) {
            }
        }

        macro.body = std::move(bodyTokens);
        macros.define(std::move(macro));
    }

    void handleUndef(preprocessor::TokenStream& stream, int lineNumber) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::IDENTIFIER) {
            addError("@undef requires a macro name", lineNumber);
            return;
        }

        std::string name = stream.consume().text;
        macros.undef(name);
    }

    void handleIfdef(preprocessor::TokenStream& stream, int lineNumber,
                     bool checkDefined) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::IDENTIFIER) {
            const char* directive = checkDefined ? "@ifdef" : "@ifndef";
            addError(std::format("{} requires a macro name", directive),
                     lineNumber);
            conditional_stack.push_back({lineNumber, false, true});
            return;
        }

        std::string name = stream.consume().text;

        bool parentActive = isCurrentBlockActive();
        bool macroDefined = macros.contains(name);
        bool conditionMet = checkDefined ? macroDefined : !macroDefined;
        bool isActive = parentActive && conditionMet;

        conditional_stack.push_back({lineNumber, isActive, conditionMet});
    }

    void handleIf(preprocessor::TokenStream& stream, int lineNumber) {
        stream.skipWhitespace();

        std::vector<Token> exprTokens;
        while (!stream.is_eof()) {
            exprTokens.push_back(stream.consume());
        }

        if (exprTokens.empty()) {
            addError("@if requires an expression", lineNumber);
            conditional_stack.push_back({lineNumber, false, false});
            return;
        }

        bool parentActive = isCurrentBlockActive();
        bool conditionMet = false;

        if (parentActive) {
            try {
                preprocessor::Expander expander(macros);
                exprTokens = expander.expand(exprTokens);

                preprocessor::Evaluator evaluator(exprTokens);
                auto result = evaluator.evaluate();
                conditionMet = preprocessor::Evaluator::is_truthy(result);
            } catch (const PreprocessorError& e) {
                addError(e.what(), lineNumber);
            } catch (const std::runtime_error& e) {
                addError(std::format("Failed to evaluate @if expression: {}",
                                     e.what()),
                         lineNumber);
            }
        }

        bool isActive = parentActive && conditionMet;
        conditional_stack.push_back({lineNumber, isActive, conditionMet});
    }

    void handleElse(int lineNumber) {
        if (conditional_stack.empty()) {
            addError("@else without matching @ifdef/@ifndef", lineNumber);
            return;
        }

        auto& block = conditional_stack.back();

        bool parentActive = true;
        if (conditional_stack.size() > 1) {
            parentActive =
                conditional_stack[conditional_stack.size() - 2].is_active;
        }

        block.is_active = parentActive && !block.had_true_branch;
    }

    void handleEndif(int lineNumber) {
        if (conditional_stack.empty()) {
            addError("@endif without matching @ifdef/@ifndef", lineNumber);
            return;
        }

        conditional_stack.pop_back();
    }

    void handleError(preprocessor::TokenStream& stream, int lineNumber) {
        stream.skipWhitespace();

        std::vector<Token> messageTokens;
        while (!stream.is_eof()) {
            messageTokens.push_back(stream.consume());
        }

        messageTokens = preprocessor_detail::trimTokens(messageTokens);
        std::string message =
            preprocessor_detail::tokensToString(messageTokens, true);

        addError(message.empty() ? "@error directive encountered"
                                 : std::format("@error: {}", message),
                 lineNumber);
    }

    void handleRequires(preprocessor::TokenStream& stream, int lineNumber) {
        stream.skipWhitespace();

        if (stream.is_eof() || stream.peek().type != TokenType::IDENTIFIER) {
            addError("@requires requires a library name", lineNumber);
            return;
        }

        std::string libName = stream.consume().text;

        std::vector<std::string_view> librariesToInclude;
        try {
            librariesToInclude =
                StandardLibraryManager::resolveDependencies(libName);
        } catch (const std::exception& e) {
            addError(std::format("Failed to resolve library '{}': {}", libName,
                                 e.what()),
                     lineNumber);
            return;
        }

        std::string_view explicitlyRequestedLib = libName;

        for (const auto& lib : librariesToInclude) {
            if (included_libraries.contains(lib)) {
                continue;
            }

            auto libCodeOpt = StandardLibraryManager::getLibraryCode(lib);
            if (!libCodeOpt) {
                addError(
                    std::format("Library '{}' not found", std::string(lib)),
                    lineNumber);
                continue;
            }

            std::string libCode = std::string(libCodeOpt.value());

            Preprocessor libPreprocessor(libCode);
            for (const auto& [name, macro] : macros) {
                libPreprocessor.impl->macros.define(macro);
            }
            auto libResult = libPreprocessor.process();

            if (!libResult.success) {
                addError(std::format("Failed to preprocess library '{}': {}",
                                     std::string(lib),
                                     libResult.errors.empty()
                                         ? "unknown error"
                                         : libResult.errors[0]),
                         lineNumber);
                continue;
            }

            for (const auto& [name, macro] : libPreprocessor.impl->macros) {
                macros.define(macro);
            }

            std::vector<std::string> libLines;
            std::istringstream libStream(libResult.source);
            std::string libLine;
            while (std::getline(libStream, libLine)) {
                libLines.push_back(libLine);
            }

            if (lib == explicitlyRequestedLib) {
                auto exportsOpt = StandardLibraryManager::getExports(lib);
                if (exportsOpt) {
                    std::string libNameUpper;
                    for (char c : lib) {
                        libNameUpper += static_cast<char>(std::toupper(c));
                    }

                    for (const auto& exported : *exportsOpt) {
                        preprocessor::Macro aliasMacro;
                        aliasMacro.name = std::string(exported.name);

                        if (exported.param_count == 0) {
                            std::string bodyStr =
                                std::format("___STDLIB_{}_{}", libNameUpper,
                                            std::string(exported.name));
                            PreprocessorTokenizer tokenizer(bodyStr);
                            aliasMacro.body = tokenizer.tokenize();
                            std::erase_if(aliasMacro.body, [](const Token& t) {
                                return t.type == TokenType::END_OF_FILE;
                            });
                            aliasMacro.is_function_like = false;
                        } else {
                            std::string internalName =
                                std::format("___stdlib_{}_{}", std::string(lib),
                                            std::string(exported.name));

                            aliasMacro.is_function_like = true;
                            for (int i = 0; i < exported.param_count; ++i) {
                                aliasMacro.params.push_back(
                                    std::format("__arg{}", i));
                            }

                            std::string bodyStr = internalName + "(";
                            for (int i = 0; i < exported.param_count; ++i) {
                                if (i > 0) {
                                    bodyStr += ", ";
                                }
                                bodyStr += std::format("__arg{}", i);
                            }
                            bodyStr += ")";

                            PreprocessorTokenizer tokenizer(bodyStr);
                            aliasMacro.body = tokenizer.tokenize();
                            std::erase_if(aliasMacro.body, [](const Token& t) {
                                return t.type == TokenType::END_OF_FILE;
                            });
                        }

                        macros.define(std::move(aliasMacro));
                    }
                }
            }

            for (const auto& libLine : libLines | std::views::reverse) {
                output_lines.insert(output_lines.begin(), libLine);

                LineMapping mapping;
                mapping.preprocessed_line = 1;
                mapping.original_line = -1;
                line_mappings.insert(line_mappings.begin(), mapping);
            }

            library_line_count += static_cast<int>(libLines.size());

            for (size_t i = libLines.size(); i < line_mappings.size(); ++i) {
                if (line_mappings[i].original_line > 0) {
                    line_mappings[i].preprocessed_line +=
                        static_cast<int>(libLines.size());
                }
            }

            for (size_t i = 0; i < libLines.size() && i < line_mappings.size();
                 ++i) {
                line_mappings[i].preprocessed_line = static_cast<int>(i + 1);
            }

            current_output_line += static_cast<int>(libLines.size());

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

    void addOutputLine(const std::string& line, int originalLine) {
        output_lines.push_back(line);

        LineMapping mapping;
        mapping.preprocessed_line = current_output_line;
        mapping.original_line = originalLine;

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
