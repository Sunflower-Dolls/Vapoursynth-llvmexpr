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

#include "InfixConverter.hpp"
#include <format>
#include <stdexcept>

#include "infix2postfix/AnalysisEngine.hpp"
#include "infix2postfix/Preprocessor.hpp"
#include "infix2postfix/Tokenizer.hpp"

std::string convertInfixToPostfix(
    const std::string& infix_expr, int num_inputs, infix2postfix::Mode mode,
    const std::map<std::string, std::string>* predefined_macros) {
    try {
        std::string preprocessed_source = infix_expr;
        std::vector<infix2postfix::LineMapping> line_map;
        int library_line_count = 0;

        if (predefined_macros != nullptr) {
            infix2postfix::Preprocessor preprocessor(infix_expr);

            for (const auto& [name, value] : *predefined_macros) {
                preprocessor.addPredefinedMacro(name, value);
            }

            auto preprocess_result = preprocessor.process();

            if (!preprocess_result.success) {
                std::string error_msg = "Preprocessing errors:\n";
                for (const auto& error : preprocess_result.errors) {
                    error_msg += error + "\n";
                }
                throw std::runtime_error(error_msg);
            }

            preprocessed_source = preprocess_result.source;
            line_map = preprocess_result.line_map;
            library_line_count = preprocess_result.library_line_count;
        }

        infix2postfix::Tokenizer tokenizer(preprocessed_source);
        auto tokens = tokenizer.tokenize();

        infix2postfix::AnalysisEngine engine(tokens, mode, num_inputs, line_map,
                                             library_line_count);
        bool success = engine.runAnalysis();

        if (!success) {
            std::string diagnostics = engine.formatDiagnostics();
            throw std::runtime_error(diagnostics);
        }

        return engine.generateCode();
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::format("Infix to postfix conversion error: {}", e.what()));
    }
}
