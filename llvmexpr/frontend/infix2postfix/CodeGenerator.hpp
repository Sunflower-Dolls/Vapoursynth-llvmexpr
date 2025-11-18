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

#ifndef LLVMEXPR_INFIX2POSTFIX_CODEGENERATOR_HPP
#define LLVMEXPR_INFIX2POSTFIX_CODEGENERATOR_HPP

#include "AST.hpp"
#include "PostfixBuilder.hpp"
#include "SemanticAnalyzer.hpp"
#include "types.hpp"
#include <format>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace infix2postfix {

struct BuiltinFunction; // Forward declaration

class CodeGenError : public std::runtime_error {
  public:
    Range range;
    CodeGenError(const std::string& message, const Range& r)
        : std::runtime_error(std::format(": {}", r.to_string(), message)),
          range(r) {}
};

class CodeGenerator {
  public:
    struct ExprResult {
        PostfixBuilder postfix;
        Type type;
    };

    CodeGenerator(Mode mode, int num_inputs);

    std::string generate(const Program* program);

    [[nodiscard]] Mode get_mode() const { return mode; }
    ExprResult generate_expr(Expr* expr);

  private:
    Type generate(Expr* expr, PostfixBuilder& builder);
    void generate(Stmt* stmt, PostfixBuilder& builder);
    std::string generate_expr_to_string(Expr* expr);

    // Expression handlers
    Type handle(const NumberExpr& expr, PostfixBuilder& builder);
    Type handle(const VariableExpr& expr, PostfixBuilder& builder);
    Type handle(const UnaryExpr& expr, PostfixBuilder& builder);
    Type handle(const BinaryExpr& expr, PostfixBuilder& builder);
    Type handle(const TernaryExpr& expr, PostfixBuilder& builder);
    Type handle(const CallExpr& expr, PostfixBuilder& builder);
    Type handle(const PropAccessExpr& expr, PostfixBuilder& builder);
    Type handle(const StaticRelPixelAccessExpr& expr, PostfixBuilder& builder);
    Type handle(const FrameDimensionExpr& expr, PostfixBuilder& builder);
    Type handle(const ArrayAccessExpr& expr, PostfixBuilder& builder);

    // Statement handlers
    void handle(const ExprStmt& stmt, PostfixBuilder& builder);
    void handle(const AssignStmt& stmt, PostfixBuilder& builder);
    void handle(const BlockStmt& stmt, PostfixBuilder& builder);
    void handle(const IfStmt& stmt, PostfixBuilder& builder);
    void handle(const WhileStmt& stmt, PostfixBuilder& builder);
    void handle(const ReturnStmt& stmt, PostfixBuilder& builder);
    void handle(const LabelStmt& stmt, PostfixBuilder& builder);
    void handle(const GotoStmt& stmt, PostfixBuilder& builder);
    void handle(const FunctionDef& stmt, PostfixBuilder& builder);
    void handle(const GlobalDecl& stmt, PostfixBuilder& builder);
    void handle(const ArrayAssignStmt& stmt, PostfixBuilder& builder);

    void check_stack_effect(const std::string& s, int expected,
                            const Range& range);
    int compute_stack_effect(const std::string& s, const Range& range);

    void inline_function_call(const FunctionSignature& sig,
                              FunctionDef* func_def,
                              const std::vector<std::unique_ptr<Expr>>& args,
                              const Range& call_range, PostfixBuilder& builder);

    Mode mode;
    int num_inputs;
    int label_counter = 0;
    int call_site_counter = 0;

    std::map<std::string, Expr*> param_substitutions;
    std::map<std::string, std::string> var_rename_map;
    const FunctionSignature* current_function = nullptr;
    std::vector<int> call_site_id_stack;

    std::set<std::string> current_function_labels;
};

} // namespace infix2postfix

#endif // LLVMEXPR_INFIX2POSTFIX_CODEGENERATOR_HPP