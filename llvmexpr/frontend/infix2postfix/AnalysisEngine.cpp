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

#include "AnalysisEngine.hpp"
#include "CodeGenerator.hpp"
#include "Parser.hpp"
#include "SemanticAnalyzer.hpp"
#include "llvmexpr/utils/EnumName.hpp"
#include <algorithm>
#include <format>

namespace infix2postfix {

AnalysisEngine::AnalysisEngine(const std::vector<Token>& tokens, Mode mode,
                               int num_inputs,
                               const std::vector<LineMapping>& line_map,
                               int library_line_count)
    : tokens(tokens), mode(mode), num_inputs(num_inputs), line_map(line_map),
      library_line_count(library_line_count) {}

AnalysisEngine::~AnalysisEngine() = default;

bool AnalysisEngine::runAnalysis() {
    diagnostics.clear();

    Parser parser(tokens);
    auto parse_result = parser.parse();
    ast = std::move(parse_result.ast);

    std::ranges::transform(parse_result.errors, std::back_inserter(diagnostics),
                           [](const auto& error) {
                               return Diagnostic(DiagnosticSeverity::ERROR,
                                                 error.message, error.range);
                           });

    if (!ast || hasErrors()) {
        return false;
    }

    semantic_analyzer = std::make_unique<SemanticAnalyzer>(mode, num_inputs,
                                                           library_line_count);
    semantic_analyzer->analyze(ast.get());

    const auto& semantic_diagnostics = semantic_analyzer->getDiagnostics();
    diagnostics.insert(diagnostics.end(), semantic_diagnostics.begin(),
                       semantic_diagnostics.end());

    return !hasErrors();
}

std::string AnalysisEngine::generateCode() {
    if (!ast || !semantic_analyzer) {
        throw std::runtime_error(
            "Cannot generate code: analysis not run or failed");
    }

    if (hasErrors()) {
        throw std::runtime_error(
            "Cannot generate code: semantic analysis had errors");
    }

    CodeGenerator code_generator(mode, num_inputs);

    return code_generator.generate(ast.get());
}

bool AnalysisEngine::hasErrors() const {
    return std::ranges::any_of(diagnostics, [](const auto& diag) {
        return diag.severity == DiagnosticSeverity::ERROR;
    });
}

std::string AnalysisEngine::formatDiagnostics() const {
    if (diagnostics.empty()) {
        return "";
    }

    std::string result;
    int error_count = 0;
    int warning_count = 0;

    for (const auto& diag : diagnostics) {
        if (diag.severity == DiagnosticSeverity::ERROR) {
            error_count++;
        } else {
            warning_count++;
        }

        if (!result.empty()) {
            result += "\n";
        }
        std::string severity_name = std::string(enum_name(diag.severity));

        const LineMapping* mapping = nullptr;
        auto it = std::ranges::find_if(line_map, [&](const auto& m) {
            return m.preprocessed_line == diag.range.start.line;
        });
        if (it != line_map.end()) {
            mapping = &(*it);
        }

        Range range = diag.range;
        std::string message = diag.message;

        if (mapping != nullptr) {
            range.start.line = mapping->original_line;
            range.end.line = mapping->original_line;

            if (!mapping->expansions.empty()) {
                std::string expansion_trace;
                std::vector<const MacroExpansion*> containing_expansions;

                for (const auto& expansion : mapping->expansions) {
                    if (diag.range.start.column + 1 >=
                            expansion.preprocessed_start_column &&
                        diag.range.end.column <
                            expansion.preprocessed_end_column) {
                        containing_expansions.push_back(&expansion);
                    }
                }

                std::ranges::sort(containing_expansions,
                                  [](const auto* a, const auto* b) {
                                      return (a->preprocessed_end_column -
                                              a->preprocessed_start_column) <
                                             (b->preprocessed_end_column -
                                              b->preprocessed_start_column);
                                  });

                for (const auto* expansion : containing_expansions) {
                    expansion_trace += std::format(
                        "\n  note: in expansion of macro '{}' from {}:{}",
                        expansion->macro_name, mapping->original_line,
                        expansion->original_column);
                }
                message += expansion_trace;
            }
        }

        result += std::format("{} - {}: {}", range.to_string(), severity_name,
                              message);
    }

    std::string summary;
    if (error_count > 0 && warning_count > 0) {
        summary = std::format("\nFound {} error(s) and {} warning(s).",
                              error_count, warning_count);
    } else if (error_count > 0) {
        summary = std::format("\nFound {} error(s).", error_count);
    } else if (warning_count > 0) {
        summary = std::format("\nFound {} warning(s).", warning_count);
    }

    result += summary;
    return result;
}

} // namespace infix2postfix
