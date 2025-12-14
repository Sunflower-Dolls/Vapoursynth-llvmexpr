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

#ifndef LLVMEXPR_INFIX2POSTFIX_ANALYSISENGINE_HPP
#define LLVMEXPR_INFIX2POSTFIX_ANALYSISENGINE_HPP

#include "AST.hpp"
#include "Preprocessor.hpp"
#include "types.hpp"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace infix2postfix {

enum class DiagnosticSeverity : std::uint8_t {
    Error,
    Warning,
};

struct Diagnostic {
    DiagnosticSeverity severity;
    std::string message;
    Range range;

    Diagnostic(DiagnosticSeverity sev, std::string msg, Range r)
        : severity(sev), message(std::move(msg)), range(r) {}
};

class SemanticAnalyzer; // Forward declaration

class AnalysisEngine {
  public:
    AnalysisEngine(const std::vector<Token>& tokens, Mode mode, int num_inputs,
                   const std::vector<LineMapping>& line_map,
                   int library_line_count = 0);
    ~AnalysisEngine();

    AnalysisEngine(const AnalysisEngine&) = delete;
    AnalysisEngine& operator=(const AnalysisEngine&) = delete;
    AnalysisEngine(AnalysisEngine&&) = delete;
    AnalysisEngine& operator=(AnalysisEngine&&) = delete;

    bool runAnalysis();

    std::string generateCode();

    [[nodiscard]] const Program* getAST() const { return ast.get(); }
    Program* getAST() { return ast.get(); }

    [[nodiscard]] const std::vector<Diagnostic>& getDiagnostics() const {
        return diagnostics;
    }

    [[nodiscard]] bool hasErrors() const;

    [[nodiscard]] std::string formatDiagnostics() const;

  private:
    std::vector<Token> tokens;
    Mode mode;
    int num_inputs;
    std::vector<LineMapping> line_map;
    int library_line_count;

    std::unique_ptr<Program> ast;
    std::unique_ptr<SemanticAnalyzer> semantic_analyzer;
    std::vector<Diagnostic> diagnostics;
};

} // namespace infix2postfix

#endif
