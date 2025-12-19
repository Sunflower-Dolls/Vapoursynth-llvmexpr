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

#include "ASTPrinter.hpp"
#include "AnalysisEngine.hpp"
#include "Preprocessor.hpp"
#include "Tokenizer.hpp"

#include <format>
#include <fstream>
#include <iostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>

using namespace infix2postfix;

void print_usage() {
    std::cerr << "Usage: infix2postfix <in.expr> -m [expr/single] -o "
                 "<out.expr> [-D MACRO[=value]] [--dump-ast] [-E]"
              << '\n';
    std::cerr << "Options:\n";
    std::cerr << "  -m MODE         Set mode (expr or single)\n";
    std::cerr << "  -o FILE         Output file\n";
    std::cerr << "  -D MACRO[=VAL]  Define preprocessor macro\n";
    std::cerr << "  --dump-ast      Dump the AST\n";
    std::cerr << "  -E              Output preprocessed code and macro "
                 "expansion trace\n";
}

int main(int argc, char* argv[]) {
    std::string input_file;
    std::string output_file;
    Mode mode = Mode::Expr;
    bool dump_ast = false;
    bool preprocess_only = false;
    std::vector<std::pair<std::string, std::string>> predefined_macros;

    auto args = std::span(argv, argc);

    if (argc < 2) {
        print_usage();
        return 1;
    }

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = args[i];

        if (!arg.starts_with('-')) {
            if (!input_file.empty()) {
                std::cerr << "Error: Only one input file can be specified.\n";
                return 1;
            }
            input_file = arg;
            continue;
        }

        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::cerr << "Error: -o requires an argument\n";
                return 1;
            }
            output_file = args[++i];
        } else if (arg == "-m") {
            if (i + 1 >= argc) {
                std::cerr << "Error: -m requires an argument\n";
                return 1;
            }
            std::string_view mode_str = args[++i];
            if (mode_str == "single") {
                mode = Mode::Single;
            } else if (mode_str != "expr") {
                std::cerr << std::format(
                    "Error: Invalid mode '{}'. Use 'expr' or 'single'.\n",
                    std::string(mode_str));
                return 1;
            }
        } else if (arg == "-D") {
            if (i + 1 >= argc) {
                std::cerr << "Error: -D requires an argument\n";
                return 1;
            }
            std::string_view macro_def = args[++i];
            size_t eq_pos = macro_def.find('=');
            if (eq_pos != std::string_view::npos) {
                std::string name(macro_def.substr(0, eq_pos));
                std::string value(macro_def.substr(eq_pos + 1));
                predefined_macros.emplace_back(std::move(name),
                                               std::move(value));
            } else {
                predefined_macros.emplace_back(std::string(macro_def), "");
            }
        } else if (arg == "--dump-ast") {
            dump_ast = true;
        } else if (arg == "-E") {
            preprocess_only = true;
        } else {
            std::cerr << std::format("Error: Unknown option '{}'\n",
                                     std::string(arg));
            print_usage();
            return 1;
        }
    }

    if (input_file.empty()) {
        std::cerr << "Error: No input file specified.\n";
        print_usage();
        return 1;
    }

    if (preprocess_only) {
        if (dump_ast) {
            std::cerr << "Error: --dump-ast and -E cannot be used together\n";
            return 1;
        }
        if (!output_file.empty()) {
            std::cerr << "Error: -o and -E cannot be used together\n";
            return 1;
        }
    } else {
        if (output_file.empty()) {
            std::cerr << "Error: Output file must be specified with -o\n";
            print_usage();
            return 1;
        }
    }

    std::ifstream in_stream(input_file);
    if (!in_stream) {
        std::cerr << std::format("Error: Cannot open input file '{}'\n",
                                 input_file);
        return 1;
    }
    std::stringstream buffer;
    buffer << in_stream.rdbuf();
    std::string source = buffer.str();

    try {
        Preprocessor preprocessor(source);

        if (mode == Mode::Expr) {
            preprocessor.addPredefinedMacro("__EXPR__", "");
        } else if (mode == Mode::VkExpr) {
            preprocessor.addPredefinedMacro("__EXPR__", "");
            preprocessor.addPredefinedMacro("__GPU__", "");
        } else {
            preprocessor.addPredefinedMacro("__SINGLEEXPR__", "");
        }

        for (const auto& [name, value] : predefined_macros) {
            preprocessor.addPredefinedMacro(name, value);
        }

        auto preprocess_result = preprocessor.process();

        if (!preprocess_result.success) {
            std::cerr << "Preprocessing errors:\n";
            for (const auto& error : preprocess_result.errors) {
                std::cerr << error << "\n";
            }
            return 1;
        }

        if (preprocess_only) {
            std::cout << "=== Preprocessed Code ===\n";
            std::cout << preprocess_result.source << "\n";
            std::cout << "\n=== Macro Expansion Trace ===\n";

            std::string expansion_info =
                Preprocessor::formatMacroExpansions(preprocess_result.line_map);
            if (expansion_info.empty()) {
                std::cout << "(No macro expansions)\n";
            } else {
                std::cout << expansion_info;
            }

            return 0;
        }

        Tokenizer tokenizer(preprocess_result.source);
        auto tokens = tokenizer.tokenize();

        AnalysisEngine engine(
            tokens, mode,
            114514, // NOLINT(cppcoreguidelines-avoid-magic-numbers)
            0, preprocess_result.line_map,
            preprocess_result.library_line_count);

        bool success = engine.runAnalysis();

        if (dump_ast) {
            ASTPrinter printer;
            std::cout << "--- AST Dump ---\n"
                      << printer.print(engine.getAST())
                      << "--- End AST Dump ---\n";
        }

        std::string diagnostics = engine.formatDiagnostics();

        if (!success) {
            std::cerr << std::format("Analysis errors:\n{}\n", diagnostics);
            return 1;
        }
        if (!diagnostics.empty()) {
            std::cerr << std::format("Analysis warnings:\n{}\n", diagnostics);
        }

        std::string postfix_code = engine.generateCode();

        std::ofstream out_stream(output_file);
        if (!out_stream) {
            std::cerr << std::format("Error: Cannot open output file '{}'\n",
                                     output_file);
            return 1;
        }
        out_stream << postfix_code << '\n';

        std::cout << std::format("Successfully converted '{}' to '{}'\n",
                                 input_file, output_file);

    } catch (const std::exception& e) {
        std::cerr << std::format("An error occurred: {}\n", e.what());
        return 1;
    }

    return 0;
}
