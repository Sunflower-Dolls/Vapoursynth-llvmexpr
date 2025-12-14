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

#include "Parser.hpp"
#include "Builtins.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <utility>

namespace infix2postfix {

Parser::Parser(const std::vector<Token>& tokens) : tokens(tokens) {}

ParseResult Parser::parse() {
    auto program = std::make_unique<Program>();
    while (!isAtEnd()) {
        // Skip any leading empty statements
        while (match({TokenType::Newline, TokenType::Semicolon})) {
        }
        if (isAtEnd()) {
            break;
        }

        int pos_before = current;
        auto stmt = parseDeclaration();
        if (stmt) {
            program->statements.push_back(std::move(stmt));
        }
        panic_mode = false;

        if (current == pos_before && !isAtEnd() && !errors.empty()) {
            advance();
        }
    }

    return ParseResult{.ast = std::move(program), .errors = std::move(errors)};
}

std::unique_ptr<Stmt> Parser::parseDeclaration() {
    std::unique_ptr<Stmt> stmt;
    if (peek().type == TokenType::Global) {
        auto global_decl = parseGlobalDecl();

        while (match({TokenType::Newline})) {
        }

        // Global declaration must be followed by a function definition
        if (peek().type != TokenType::Function) {
            error(peek(), "Global declaration must be followed by a function "
                          "definition.");
        }
        auto func_def = parseFunctionDef();
        // Attach the global declaration to the function
        func_def->global_decl = std::move(global_decl);
        stmt = make_node<FunctionDef>(std::move(*func_def));
    } else if (peek().type == TokenType::Function) {
        auto func_def = parseFunctionDef();
        stmt = std::make_unique<Stmt>(FunctionDef(std::move(*func_def)));
    } else {
        stmt = parseStatement();
    }

    // Block statements are not followed by a terminator here.
    if ((get_if<FunctionDef>(stmt.get()) != nullptr) ||
        (get_if<IfStmt>(stmt.get()) != nullptr) ||
        (get_if<WhileStmt>(stmt.get()) != nullptr) ||
        (get_if<BlockStmt>(stmt.get()) != nullptr)) {
        return stmt;
    }

    // Last statement in a block or file.
    if (isAtEnd() || peek().type == TokenType::RBrace) {
        return stmt;
    }

    // A simple statement must be followed by a terminator.
    if (peek().type == TokenType::Newline ||
        peek().type == TokenType::Semicolon) {
        // The terminator is consumed by the main loop.
        return stmt;
    }

    error(peek(), "Expected newline or semicolon after statement.");
    synchronize();
    return stmt;
}

std::unique_ptr<Stmt> Parser::parseStatement() {
    if (peek().type == TokenType::If) {
        return parseIfStatement();
    }
    if (peek().type == TokenType::While) {
        return parseWhileStatement();
    }
    if (peek().type == TokenType::LBrace) {
        error(peek(), "Standalone blocks are not allowed. Braces can only be "
                      "used for function, if, else, or while bodies.");
        synchronize();
        return nullptr;
    }
    if (peek().type == TokenType::Goto) {
        return parseGotoStatement();
    }
    if (peek().type == TokenType::Return) {
        return parseReturnStatement();
    }
    if (peek().type == TokenType::Identifier &&
        peek(1).type == TokenType::Colon) {
        return parseLabelStatement();
    }
    return parseExprStatement();
}

std::unique_ptr<Stmt> Parser::parseIfStatement() {
    consume(TokenType::If, "Expect 'if'.");
    consume(TokenType::LParen, "Expect '(' after 'if'.");
    auto condition = parseTernary();
    consume(TokenType::RParen, "Expect ')' after if condition.");

    if (peek().type != TokenType::LBrace) {
        error(peek(), "The body of an if statement must be a block statement "
                      "enclosed in {}.");
    }
    auto then_branch = std::make_unique<Stmt>(std::move(*parseBlock()));

    std::unique_ptr<Stmt> else_branch = nullptr;
    if (match({TokenType::Else})) {
        if (peek().type == TokenType::If) {
            else_branch = parseIfStatement();
        } else {
            if (peek().type != TokenType::LBrace) {
                error(peek(), "The body of an else statement must be a block "
                              "statement enclosed in {}.");
            }
            else_branch = std::make_unique<Stmt>(std::move(*parseBlock()));
        }
    }
    return make_node<IfStmt>(std::move(condition), std::move(then_branch),
                             std::move(else_branch));
}

std::unique_ptr<Stmt> Parser::parseWhileStatement() {
    consume(TokenType::While, "Expect 'while'.");
    consume(TokenType::LParen, "Expect '(' after 'while'.");
    auto condition = parseTernary();
    consume(TokenType::RParen, "Expect ')' after while condition.");

    if (peek().type != TokenType::LBrace) {
        error(peek(), "The body of a while statement must be a block "
                      "statement enclosed in {}.");
    }
    auto body = std::make_unique<Stmt>(std::move(*parseBlock()));

    return make_node<WhileStmt>(std::move(condition), std::move(body));
}

std::unique_ptr<Stmt> Parser::parseGotoStatement() {
    Token keyword = consume(TokenType::Goto, "Expect 'goto'.");
    Token label =
        consume(TokenType::Identifier, "Expect label name after 'goto'.");
    if (label.value.starts_with("__internal_")) {
        error(label, "goto target cannot start with '__internal_'.");
    }
    return make_node<GotoStmt>(keyword, label, nullptr);
}

std::unique_ptr<Stmt> Parser::parseLabelStatement() {
    Token name = consume(TokenType::Identifier, "Expect label name.");
    if (name.value.starts_with("__internal_")) {
        error(name, "Label name cannot start with '__internal_'.");
    }
    consume(TokenType::Colon, "Expect ':' after label name.");
    return make_node<LabelStmt>(name);
}

std::unique_ptr<Stmt> Parser::parseReturnStatement() {
    Token keyword = consume(TokenType::Return, "Expect 'return'.");
    std::unique_ptr<Expr> value = nullptr;
    // Check if there is a value to return.
    if (peek().type != TokenType::Newline &&
        peek().type != TokenType::Semicolon &&
        peek().type != TokenType::EndOfFile &&
        peek().type != TokenType::RBrace) {
        value = parseTernary();
    }
    return make_node<ReturnStmt>(keyword, std::move(value));
}

std::unique_ptr<BlockStmt> Parser::parseBlock() {
    consume(TokenType::LBrace, "Expect '{' to start a block.");
    std::vector<std::unique_ptr<Stmt>> statements;
    while (peek().type != TokenType::RBrace && !isAtEnd()) {
        // Skip any empty statements
        while (match({TokenType::Newline, TokenType::Semicolon})) {
        }
        if (peek().type == TokenType::RBrace || isAtEnd()) {
            break;
        }

        int pos_before = current;
        auto stmt = parseDeclaration();
        if (stmt) {
            statements.push_back(std::move(stmt));
        }
        panic_mode = false;

        if (peek().type == TokenType::RBrace || isAtEnd()) {
            break;
        }

        if (current == pos_before && !isAtEnd() &&
            peek().type != TokenType::RBrace && !errors.empty()) {
            advance();
        }
    }
    consume(TokenType::RBrace, "Expect '}' to end a block.");
    return std::make_unique<BlockStmt>(BlockStmt(std::move(statements)));
}

std::unique_ptr<Stmt> Parser::parseExprStatement() {
    if (peek().type == TokenType::Identifier &&
        peek(1).type == TokenType::Assign) {
        Token name = advance(); // identifier
        if (name.value.starts_with("__internal_")) {
            error(name, "Variable name cannot start with '__internal_'.");
        }
        advance(); // '='
        auto value = parseTernary();
        return make_node<AssignStmt>(name, std::move(value));
    }

    if (peek().type == TokenType::Identifier &&
        peek(1).type == TokenType::LBracket) {
        auto left_expr = parsePostfix();

        if (match({TokenType::Assign})) {
            auto right_expr = parseTernary();

            if (get_if<ArrayAccessExpr>(left_expr.get()) != nullptr) {
                return make_node<ArrayAssignStmt>(std::move(left_expr),
                                                  std::move(right_expr));
            }
            error(peek(), "Invalid assignment target.");
        }

        return make_node<ExprStmt>(std::move(left_expr));
    }

    auto expr = parseTernary();
    return make_node<ExprStmt>(std::move(expr));
}

std::unique_ptr<FunctionDef> Parser::parseFunctionDef() {
    consume(TokenType::Function, "Expect 'function'.");
    Token name = consume(TokenType::Identifier, "Expect function name.");
    if (name.value.starts_with("__internal_")) {
        error(name, "Function name cannot start with '__internal_'.");
    }

    const auto& builtins = get_builtin_functions();
    // TODO: resize is not a built-in function in Expr mode.
    if ((builtins.contains(name.value)) || name.value.starts_with("nth_") ||
        name.value == "new" || name.value == "resize") {
        error(name,
              std::format(
                  "Function name '{}' conflicts with a built-in function.",
                  name.value));
    }

    defined_functions.insert(name.value);

    consume(TokenType::LParen, "Expect '(' after function name.");
    std::vector<Parameter> params;
    if (peek().type != TokenType::RParen) {
        do {
            Token type_token;
            Token name_token;
            Type param_type = Type::Value;

            if (peek().type == TokenType::Identifier &&
                peek(1).type == TokenType::Identifier) {
                type_token = advance(); // Consume potential type
                if (type_token.value == "Value") {
                    param_type = Type::Value;
                } else if (type_token.value == "Clip") {
                    param_type = Type::Clip;
                } else if (type_token.value == "Literal") {
                    param_type = Type::Literal;
                } else if (type_token.value == "Array") {
                    param_type = Type::Array;
                } else {
                    error(type_token,
                          std::format("Unknown type '{}' for parameter.",
                                      type_token.value));
                }
                name_token =
                    consume(TokenType::Identifier, "Expect parameter name.");
            } else if (peek().type == TokenType::Identifier) {
                // Untyped parameter, default to Value
                name_token =
                    consume(TokenType::Identifier, "Expect parameter name.");
                type_token = {.type = TokenType::Identifier,
                              .value = "Value",
                              .range = name_token.range};
                param_type = Type::Value;
            } else {
                error(peek(), "Expect a parameter declaration.");
                break; // Avoid infinite loop on error
            }
            if (name_token.type == TokenType::Identifier &&
                name_token.value.starts_with("__internal_")) {
                error(name_token,
                      "Parameter name cannot start with '__internal_'.");
            }
            params.push_back({type_token, name_token, param_type});
        } while (match({TokenType::Comma}));
    }
    consume(TokenType::RParen, "Expect ')' after parameters.");
    auto body = parseBlock();
    return std::make_unique<FunctionDef>(
        FunctionDef(name, std::move(params), std::move(body), nullptr));
}

std::unique_ptr<GlobalDecl> Parser::parseGlobalDecl() {
    Token keyword = consume(TokenType::Global, "Expect '<global...>'.");
    std::string content = keyword.value.substr(1, keyword.value.length() - 2);
    if (content == "global.all") {
        return std::make_unique<GlobalDecl>(
            GlobalDecl(keyword, GlobalMode::All));
    }
    if (content == "global.none") {
        return std::make_unique<GlobalDecl>(
            GlobalDecl(keyword, GlobalMode::None));
    }

    // <global<var1><var2>...>
    std::vector<Token> globals;

    size_t pos = 0;
    if (content.starts_with("global")) {
        pos = std::string("global").length();
    } else {
        error(keyword, "Invalid global declaration format.");
    }

    // Parse each <varname>
    while (pos < content.length()) {
        if (content[pos] != '<') {
            error(keyword, "Expected '<' in global variable list.");
        }
        pos++; // '<'

        size_t start = pos;
        while (pos < content.length() && content[pos] != '>') {
            pos++;
        }

        if (pos >= content.length()) {
            error(keyword, "Unclosed '<' in global variable list.");
        }

        std::string var_name = content.substr(start, pos - start);

        if (var_name.empty()) {
            error(keyword, "Empty variable name in global declaration.");
        }

        if (var_name.starts_with("__internal_")) {
            error(keyword, std::format("Invalid identifier '{}': cannot "
                                       "start with '__internal_'.",
                                       var_name));
        }

        if ((std::isalpha(var_name[0]) == 0) && var_name[0] != '_') {
            error(keyword, std::format("Invalid identifier '{}': must "
                                       "start with letter or underscore.",
                                       var_name));
        }

        for (size_t i = 1; i < var_name.length(); i++) {
            if ((std::isalnum(var_name[i]) == 0) && var_name[i] != '_') {
                error(keyword, std::format("Invalid identifier '{}': "
                                           "contains invalid character.",
                                           var_name));
            }
        }

        globals.push_back({TokenType::Identifier, var_name, keyword.range});
        pos++; // '>'
    }

    if (globals.empty()) {
        error(keyword,
              "Global declaration must specify at least one variable.");
    }

    return std::make_unique<GlobalDecl>(
        GlobalDecl(keyword, GlobalMode::Specific, globals));
}

std::unique_ptr<Expr> Parser::parseTernary() {
    auto expr = parseLogicalOr();
    if (match({TokenType::Question})) {
        auto then_branch = parseTernary();
        consume(TokenType::Colon, "Expect ':' for ternary operator.");
        auto else_branch = parseTernary();
        expr = make_node<TernaryExpr>(std::move(expr), std::move(then_branch),
                                      std::move(else_branch));
    }
    return expr;
}

template <typename NextLevel, typename... TokenTypes>
std::unique_ptr<Expr> Parser::parseBinary(NextLevel next_level,
                                          TokenTypes... token_types) {
    auto expr = (this->*next_level)();
    while (match({token_types...})) {
        Token op = previous();
        auto right = (this->*next_level)();
        expr = make_node<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseLogicalOr() {
    return parseBinary(&Parser::parseLogicalAnd, TokenType::LogicalOr);
}

std::unique_ptr<Expr> Parser::parseLogicalAnd() {
    return parseBinary(&Parser::parseBitwiseOr, TokenType::LogicalAnd);
}

std::unique_ptr<Expr> Parser::parseBitwiseOr() {
    return parseBinary(&Parser::parseBitwiseXor, TokenType::BitOr);
}

std::unique_ptr<Expr> Parser::parseBitwiseXor() {
    return parseBinary(&Parser::parseBitwiseAnd, TokenType::BitXor);
}

std::unique_ptr<Expr> Parser::parseBitwiseAnd() {
    return parseBinary(&Parser::parseEquality, TokenType::BitAnd);
}

std::unique_ptr<Expr> Parser::parseEquality() {
    return parseBinary(&Parser::parseComparison, TokenType::Eq, TokenType::Ne);
}

std::unique_ptr<Expr> Parser::parseComparison() {
    return parseBinary(&Parser::parseTerm, TokenType::Gt, TokenType::Ge,
                       TokenType::Lt, TokenType::Le);
}

std::unique_ptr<Expr> Parser::parseTerm() {
    return parseBinary(&Parser::parseFactor, TokenType::Plus, TokenType::Minus);
}

std::unique_ptr<Expr> Parser::parseFactor() {
    return parseBinary(&Parser::parseExponent, TokenType::Star,
                       TokenType::Slash, TokenType::Percent);
}

std::unique_ptr<Expr> Parser::parseExponent() {
    // '**' is right-associative: a ** b ** c = a ** (b ** c)
    auto expr = parseUnary();
    if (match({TokenType::StarStar})) {
        Token op = previous();
        auto right = parseExponent();
        expr = make_node<BinaryExpr>(std::move(expr), op, std::move(right));
    }
    return expr;
}

std::unique_ptr<Expr> Parser::parseUnary() {
    if (match({TokenType::Not, TokenType::Minus, TokenType::BitNot})) {
        Token op = previous();
        if (op.type == TokenType::Minus && peek().type == TokenType::Number) {
            Token number = advance();
            number.value = std::format("-{}", number.value);
            number.range.start = op.range.start;
            return make_node<NumberExpr>(number);
        }
        auto right = parseUnary();
        return make_node<UnaryExpr>(op, std::move(right));
    }
    return parsePostfix();
}

std::unique_ptr<Expr> Parser::parsePostfix() {
    auto expr = parsePrimary();
    while (true) {
        if (match({TokenType::LParen})) {
            expr = finishCall(std::move(expr));
        } else if (match({TokenType::LBracket})) {
            auto index1 = parseTernary();

            if (match({TokenType::Comma})) {
                // Pixel access: array[offsetX, offsetY]
                auto index2 = parseTernary();
                consume(TokenType::RBracket, "Expect ']' after indices");
                std::string suffix;
                if (match({TokenType::Colon})) {
                    Token s = consume(TokenType::Identifier,
                                      "Expect boundary suffix");
                    suffix = std::format(":{}", s.value);
                }
                if (auto* var = get_if<VariableExpr>(expr.get())) {
                    auto get_constant_token = [](Expr* e) -> Token* {
                        if (auto* num = get_if<NumberExpr>(e)) {
                            return &num->value;
                        }
                        if (auto* unary = get_if<UnaryExpr>(e)) {
                            if (unary->op.type == TokenType::Minus) {
                                if (auto* num = get_if<NumberExpr>(
                                        unary->right.get())) {
                                    static Token neg_token;
                                    neg_token = num->value;
                                    neg_token.value =
                                        std::format("-{}", neg_token.value);
                                    return &neg_token;
                                }
                            }
                        }
                        return nullptr;
                    };

                    Token* x_tok = get_constant_token(index1.get());
                    Token* y_tok = get_constant_token(index2.get());

                    if ((x_tok != nullptr) && (y_tok != nullptr)) {
                        return make_node<StaticRelPixelAccessExpr>(
                            var->name, *x_tok, *y_tok, suffix);
                    }
                }
                error(peek(), "Dynamic pixel access should use dyn().");
            } else {
                // Array access: array[index]
                consume(TokenType::RBracket, "Expect ']' after array index.");
                expr = make_node<ArrayAccessExpr>(std::move(expr),
                                                  std::move(index1));
            }
        } else if (match({TokenType::Dot})) {
            Token prop = consume(TokenType::Identifier,
                                 "Expect property name after '.'");
            if (auto* var = get_if<VariableExpr>(expr.get())) {
                // Check if this is frame.width[N] or frame.height[N]
                if (var->name.value == "frame" &&
                    (prop.value == "width" || prop.value == "height")) {
                    if (match({TokenType::LBracket})) {
                        auto plane_index_expr = parseTernary();
                        consume(TokenType::RBracket,
                                "Expect ']' after plane index");
                        return make_node<FrameDimensionExpr>(
                            prop, std::move(plane_index_expr));
                    }
                }
                return make_node<PropAccessExpr>(var->name, prop);
            }
            error(prop, "Invalid property access target.");
        } else {
            break;
        }
    }
    return expr;
}

std::unique_ptr<Expr> Parser::finishCall(std::unique_ptr<Expr> callee) {
    if (auto* var = get_if<VariableExpr>(callee.get())) {
        std::vector<std::unique_ptr<Expr>> args;
        if (peek().type != TokenType::RParen) {
            do {
                args.push_back(parseTernary());
            } while (match({TokenType::Comma}));
        }
        consume(TokenType::RParen, "Expect ')' after arguments.");
        return make_node<CallExpr>(var->name, std::move(args));
    }
    error(peek(), "Invalid call target.");
    Token placeholder{
        .type = TokenType::Identifier, .value = "error", .range = peek().range};
    return make_node<CallExpr>(placeholder,
                               std::vector<std::unique_ptr<Expr>>{});
}

std::unique_ptr<Expr> Parser::parsePrimary() {
    if (match({TokenType::Number})) {
        return make_node<NumberExpr>(previous());
    }
    if (match({TokenType::Identifier})) {
        return make_node<VariableExpr>(previous());
    }
    if (match({TokenType::LParen})) {
        auto expr = parseTernary();
        consume(TokenType::RParen, "Expect ')' after expression.");
        return expr;
    }
    error(peek(), "Expect expression.");
    Token placeholder{
        .type = TokenType::Number, .value = "0", .range = peek().range};
    return make_node<NumberExpr>(placeholder);
}

bool Parser::match(const std::vector<TokenType>& types) {
    if (std::ranges::any_of(
            types, [this](TokenType type) { return peek().type == type; })) {
        advance();
        return true;
    }
    return false;
}

Token Parser::consume(TokenType type, const std::string& message) {
    if (peek().type == type) {
        return advance();
    }
    error(peek(), message);
    return peek();
}

Token Parser::advance() {
    if (!isAtEnd()) {
        current++;
    }
    return previous();
}

Token Parser::peek() const { return tokens[current]; }
Token Parser::peek(int offset) const {
    if (current + offset >= static_cast<int>(tokens.size())) {
        return tokens.back(); // Return EOF token
    }
    return tokens[current + offset];
}
Token Parser::previous() const { return tokens[current - 1]; }
bool Parser::isAtEnd() const { return peek().type == TokenType::EndOfFile; }

void Parser::reportError(const Token& token, const std::string& message) {
    if (panic_mode) {
        return;
    }

    std::string error_message;
    if (token.type == TokenType::EndOfFile) {
        error_message = std::format("at end: {}", message);
    } else {
        error_message = std::format("at '{}': {}", token.value, message);
    }

    errors.push_back({error_message, token.range});
}

void Parser::error(const Token& token, const std::string& message) {
    reportError(token, message);
    if (!panic_mode) {
        panic_mode = true;
    }
}

void Parser::synchronize() {
    panic_mode = false;

    while (!isAtEnd()) {
        if (current > 0) {
            TokenType prev = tokens[current - 1].type;
            if (prev == TokenType::Semicolon || prev == TokenType::Newline) {
                while (peek().type == TokenType::Semicolon ||
                       peek().type == TokenType::Newline) {
                    advance();
                }
                return;
            }
        }

        switch (peek().type) {
        case TokenType::Function:
        case TokenType::Global:
        case TokenType::If:
        case TokenType::While:
        case TokenType::Return:
        case TokenType::Goto:
        case TokenType::RBrace:
        case TokenType::Semicolon:
        case TokenType::Newline:
            return;
        default:
            break;
        }

        advance();
    }
}

} // namespace infix2postfix
