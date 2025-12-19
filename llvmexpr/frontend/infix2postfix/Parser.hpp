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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_PARSER_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_PARSER_HPP

#include "AST.hpp"

#include <set>
#include <vector>

namespace infix2postfix {

struct ErrorInfo {
    std::string message;
    Range range;
};

struct ParseResult {
    std::unique_ptr<Program> ast;
    std::vector<ErrorInfo> errors;
};

class Parser {
  public:
    explicit Parser(const std::vector<Token>& tokens);
    ParseResult parse();

  private:
    std::unique_ptr<Stmt> parseDeclaration();
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<Stmt> parseIfStatement();
    std::unique_ptr<Stmt> parseWhileStatement();
    std::unique_ptr<Stmt> parseGotoStatement();
    std::unique_ptr<Stmt> parseLabelStatement();
    std::unique_ptr<Stmt> parseReturnStatement();
    std::unique_ptr<BlockStmt> parseBlock();
    std::unique_ptr<Stmt> parseExprStatement();
    std::unique_ptr<FunctionDef> parseFunctionDef();
    std::unique_ptr<GlobalDecl> parseGlobalDecl();

    std::unique_ptr<Expr> parseTernary();

    template <typename NextLevel, typename... TokenTypes>
    std::unique_ptr<Expr> parseBinary(NextLevel next_level,
                                      TokenTypes... token_types);

    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseBitwiseOr();
    std::unique_ptr<Expr> parseBitwiseXor();
    std::unique_ptr<Expr> parseBitwiseAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseComparison();
    std::unique_ptr<Expr> parseTerm();
    std::unique_ptr<Expr> parseFactor();
    std::unique_ptr<Expr> parseExponent();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePrimary();
    std::unique_ptr<Expr> finishCall(std::unique_ptr<Expr> callee);

    bool match(const std::vector<TokenType>& types);
    Token consume(TokenType type, const std::string& message);
    Token advance();
    [[nodiscard]] Token peek() const;
    [[nodiscard]] Token peek(int offset) const;
    [[nodiscard]] Token previous() const;
    [[nodiscard]] bool isAtEnd() const;
    void error(const Token& token, const std::string& message);
    void synchronize();
    void reportError(const Token& token, const std::string& message);

    std::vector<Token> tokens;
    int current = 0;
    std::set<std::string> defined_functions;
    std::vector<ErrorInfo> errors;
    bool panic_mode = false;
};

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_PARSER_HPP