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

#ifndef LLVMEXPR_FRONTEND_INFIX2POSTFIX_PREPROCESSOR_HPP
#define LLVMEXPR_FRONTEND_INFIX2POSTFIX_PREPROCESSOR_HPP

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace infix2postfix {

class PreprocessorError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct MacroExpansion {
    std::string macro_name;
    int original_line;
    int original_column;
    std::string replacement_text;
    int preprocessed_start_column;
    int preprocessed_end_column;
};

struct LineMapping {
    int preprocessed_line;
    int original_line;
    std::vector<MacroExpansion> expansions;
};

struct MacroDefinition {
    std::string name;
    std::string body;
    bool is_function_like = false;
    std::vector<std::string> parameters;
};

struct PreprocessResult {
    std::string source;
    std::vector<LineMapping> line_map;
    std::vector<std::string> errors;
    bool success;
    int library_line_count = 0;
};

class Preprocessor {
  public:
    explicit Preprocessor(std::string source);
    ~Preprocessor();

    Preprocessor(const Preprocessor&) = delete;
    Preprocessor& operator=(const Preprocessor&) = delete;
    Preprocessor(Preprocessor&&) = delete;
    Preprocessor& operator=(Preprocessor&&) = delete;

    void addPredefinedMacro(std::string name, const std::string& value = "");

    PreprocessResult process();

    static std::string
    formatDiagnosticWithExpansion(const std::string& message, int line,
                                  const std::vector<LineMapping>& line_map);

    static std::string
    formatMacroExpansions(const std::vector<LineMapping>& line_map);

  private:
    class Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace infix2postfix

#endif // LLVMEXPR_FRONTEND_INFIX2POSTFIX_PREPROCESSOR_HPP
