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

#ifndef LLVMEXPR_INFIX2POSTFIX_SEMANTICANALYZER_HPP
#define LLVMEXPR_INFIX2POSTFIX_SEMANTICANALYZER_HPP

#include "AST.hpp"
#include "AnalysisEngine.hpp"
#include "SymbolTable.hpp"
#include "types.hpp"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace infix2postfix {

class SemanticAnalyzer {
  public:
    SemanticAnalyzer(Mode mode, int num_inputs, int library_line_count = 0);

    class ScopeGuard {
      public:
        explicit ScopeGuard(SemanticAnalyzer* analyzer) : analyzer(analyzer) {
            analyzer->enterScope();
        }
        ~ScopeGuard() { analyzer->exitScope(); }
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;
        ScopeGuard(ScopeGuard&&) = delete;
        ScopeGuard& operator=(ScopeGuard&&) = delete;

      private:
        SemanticAnalyzer* analyzer;
    };

    bool analyze(const Program* program);

    [[nodiscard]] const std::vector<Diagnostic>& getDiagnostics() const {
        return diagnostics;
    }

    [[nodiscard]] bool hasErrors() const;

    [[nodiscard]] const std::map<std::string, std::vector<FunctionSignature>>&
    getFunctionSignatures() const {
        return function_signatures;
    }

    [[nodiscard]] const std::map<std::string, std::vector<FunctionDef*>>&
    getFunctionDefs() const {
        return function_defs;
    }

    [[nodiscard]] const std::map<std::string, std::pair<std::string, Range>>&
    get_written_properties() const {
        return written_properties;
    }

  private:
    Type analyzeExpr(Expr* expr);
    Type analyze(VariableExpr& expr);
    Type analyze(const NumberExpr& expr);
    Type analyze(UnaryExpr& expr);
    Type analyze(BinaryExpr& expr);
    Type analyze(TernaryExpr& expr);
    Type analyze(CallExpr& expr);
    Type analyze(const PropAccessExpr& expr);
    Type analyze(const StaticRelPixelAccessExpr& expr);
    Type analyze(FrameDimensionExpr& expr);
    Type analyze(ArrayAccessExpr& expr);

    // Statement analysis
    void analyzeStmt(Stmt* stmt);
    void analyze(const ExprStmt& stmt);
    void analyze(AssignStmt& stmt);
    void analyze(const BlockStmt& stmt);
    void analyze(IfStmt& stmt);
    void analyze(WhileStmt& stmt);
    void analyze(const ReturnStmt& stmt);
    void analyze(LabelStmt& stmt);
    void analyze(GotoStmt& stmt);
    void analyze(FunctionDef& stmt);
    void analyze(const GlobalDecl& stmt);
    void analyze(ArrayAssignStmt& stmt);

    void getAllSymbols(
        const SymbolTable* scope,
        std::map<std::string, std::shared_ptr<Symbol>>& symbols) const;

    struct ForwardGotoInfo {
        GotoStmt* stmt;
        std::map<std::string, std::shared_ptr<Symbol>> symbols_at_goto;
    };

    void enterScope();
    void exitScope();
    std::shared_ptr<Symbol> defineSymbol(SymbolKind kind,
                                         const std::string& name, Type type,
                                         const Range& range);
    std::shared_ptr<Symbol> resolveSymbol(const std::string& name,
                                          const Range& range);

    void reportError(const std::string& message, const Range& range);
    void reportWarning(const std::string& message, const Range& range);
    bool builtinParamTypeIsEvaluatable(
        const std::vector<struct BuiltinFunction>& overloads, size_t param_idx);

    const FunctionSignature*
    resolveOverload(const std::string& name,
                    const std::vector<std::unique_ptr<Expr>>& args,
                    const Range& range, CallExpr* call_expr = nullptr);

    void collectLabels(Stmt* stmt, std::set<std::string>& labels,
                       const std::string& context, const Range& context_range);

    void validateGlobalDependencies(Stmt* stmt);
    void validateFunctionCall(const CallExpr& expr);

    void collectUsedGlobals(Expr* expr, std::set<std::string>& used_globals);
    void collectUsedGlobalsInStmt(Stmt* stmt,
                                  std::set<std::string>& used_globals);

    bool path_always_returns(Stmt* stmt);

    void validateClipReference(const std::string& clip_name,
                               const Range& range);

    Mode mode;
    int num_inputs;
    int library_line_count;
    bool has_result = false;
    bool result_defined_in_global_scope = false;

    std::unique_ptr<SymbolTable> global_scope;
    SymbolTable* current_scope;

    std::vector<std::unique_ptr<SymbolTable>> scope_stack;

    std::set<std::string> defined_global_vars;

    std::map<std::string, std::vector<FunctionSignature>> function_signatures;
    std::map<std::string, std::vector<FunctionDef*>> function_defs;

    std::set<std::string> global_labels;
    std::set<std::string> current_function_labels;

    std::vector<Diagnostic> diagnostics;

    std::map<std::string, std::vector<ForwardGotoInfo>> current_pending_gotos;
    std::set<std::string> current_labels_seen;

    std::map<std::string, std::pair<std::string, Range>> written_properties;

    const FunctionSignature* current_function = nullptr;
};

} // namespace infix2postfix

#endif
