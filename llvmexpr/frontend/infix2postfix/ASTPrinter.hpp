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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_ASTPRINTER_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_ASTPRINTER_HPP

#include "AST.hpp"

#include <format>
#include <sstream>
#include <string>
#include <string_view>

namespace infix2postfix {

class ASTPrinter {
  public:
    std::string print(const Program* program);

  private:
    void print(const Stmt* stmt);
    void print(const Expr* expr);

    // Stmt visitors
    void visit(const ExprStmt& stmt);
    void visit(const AssignStmt& stmt);
    void visit(const ArrayAssignStmt& stmt);
    void visit(const BlockStmt& stmt);
    void visit(const IfStmt& stmt);
    void visit(const WhileStmt& stmt);
    void visit(const ReturnStmt& stmt);
    void visit(const LabelStmt& stmt);
    void visit(const GotoStmt& stmt);
    void visit(const GlobalDecl& stmt);
    void visit(const FunctionDef& stmt);

    // Expr visitors
    void visit(const NumberExpr& expr);
    void visit(const VariableExpr& expr);
    void visit(const UnaryExpr& expr);
    void visit(const BinaryExpr& expr);
    void visit(const TernaryExpr& expr);
    void visit(const CallExpr& expr);
    void visit(const PropAccessExpr& expr);
    void visit(const StaticRelPixelAccessExpr& expr);
    void visit(const FrameDimensionExpr& expr);
    void visit(const ArrayAccessExpr& expr);

    void indent();
    void unindent();

    template <typename... Args>
    void line(const std::string_view format_str, Args&&... args) {
        for (int i = 0; i < indent_level; ++i) {
            ss << "  ";
        }
        ss << std::vformat(format_str,
                           std::make_format_args(std::forward<Args>(args)...))
           << "\n";
    }

    std::stringstream ss;
    int indent_level = 0;
};

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_ASTPRINTER_HPP
