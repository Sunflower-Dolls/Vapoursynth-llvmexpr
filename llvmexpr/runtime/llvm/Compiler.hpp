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

#ifndef LLVMEXPR_RUNTIME_LLVM_COMPILER_HPP
#define LLVMEXPR_RUNTIME_LLVM_COMPILER_HPP

#include <map>
#include <string>
#include <vector>

#include "VapourSynth4.h"

#include "../../analysis/AnalysisResults.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "Jit.hpp"

class Compiler {
  public:
    Compiler(std::vector<Token> tokens_in, const VSVideoInfo* out_vi,
             const std::vector<const VSVideoInfo*>& in_vi, int width_in,
             int height_in, bool mirror, std::string dump_path,
             const std::map<std::pair<int, std::string>, int>& p_map,
             std::string function_name, int opt_level_in, int approx_math_in,
             const analysis::ExpressionAnalysisResults& analysis_results_in,
             ExprMode mode = ExprMode::Expr,
             const std::vector<std::string>& output_props = {});

    CompiledFunction compile();

  private:
    std::vector<Token> tokens;
    const VSVideoInfo* vo;
    const std::vector<const VSVideoInfo*>& vi;
    int num_inputs;
    int width;
    int height;
    bool mirror_boundary;
    std::string dump_ir_path;
    const std::map<std::pair<int, std::string>, int>& prop_map;
    std::string func_name;
    int opt_level;
    int approx_math;
    ExprMode expr_mode;
    const std::vector<std::string>& output_props;

    // Analysis results
    const analysis::ExpressionAnalysisResults& analysis_results;

    CompiledFunction compileWithApproxMath(int actual_approx_math);
};

#endif // LLVMEXPR_RUNTIME_LLVM_COMPILER_HPP
