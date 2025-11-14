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

std::string convertInfixToPostfix(const std::string& infix_expr, int num_inputs,
                                  infix2postfix::Mode mode,
                                  const InfixConversionContext* context) {
    try {
        std::string preprocessed_source = infix_expr;
        std::vector<infix2postfix::LineMapping> line_map;
        int library_line_count = 0;

        if (context != nullptr) {
            infix2postfix::Preprocessor preprocessor(infix_expr);

            if (mode == infix2postfix::Mode::Expr) {
                preprocessor.addPredefinedMacro("__EXPR__", "");
            } else {
                preprocessor.addPredefinedMacro("__SINGLEEXPR__", "");
            }

            preprocessor.addPredefinedMacro("__WIDTH__",
                                            std::to_string(context->width));
            preprocessor.addPredefinedMacro("__HEIGHT__",
                                            std::to_string(context->height));
            preprocessor.addPredefinedMacro(
                "__INPUT_NUM__", std::to_string(context->num_inputs));
            preprocessor.addPredefinedMacro(
                "__OUTPUT_BITDEPTH__",
                std::to_string(context->output_bitdepth));

            for (size_t i = 0; i < context->input_bitdepths.size(); ++i) {
                std::string macro_name =
                    std::format("__INPUT_BITDEPTH_{}__", i);
                preprocessor.addPredefinedMacro(
                    macro_name, std::to_string(context->input_bitdepths[i]));
            }

            for (size_t i = 0; i < context->input_formats.size(); ++i) {
                std::string macro_name = std::format("__INPUT_FMT_{}__", i);
                preprocessor.addPredefinedMacro(
                    macro_name, std::to_string(context->input_formats[i]));
            }

            for (size_t i = 0; i < context->input_widths.size(); ++i) {
                preprocessor.addPredefinedMacro(
                    std::format("__INPUT_WIDTH_{}__", i),
                    std::to_string(context->input_widths[i]));
                preprocessor.addPredefinedMacro(
                    std::format("__INPUT_HEIGHT_{}__", i),
                    std::to_string(context->input_heights[i]));
                preprocessor.addPredefinedMacro(
                    std::format("__INPUT_SUBSAMPLE_W_{}__", i),
                    std::to_string(context->input_subsample_ws[i]));
                preprocessor.addPredefinedMacro(
                    std::format("__INPUT_SUBSAMPLE_H_{}__", i),
                    std::to_string(context->input_subsample_hs[i]));
            }

            preprocessor.addPredefinedMacro(
                "__OUTPUT_FMT__", std::to_string(context->output_format));

            preprocessor.addPredefinedMacro(
                "__SUBSAMPLE_W__", std::to_string(context->subsample_w));
            preprocessor.addPredefinedMacro(
                "__SUBSAMPLE_H__", std::to_string(context->subsample_h));

            if (mode == infix2postfix::Mode::Expr && context->plane_no >= 0) {
                preprocessor.addPredefinedMacro(
                    "__PLANE_NO__", std::to_string(context->plane_no));
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
