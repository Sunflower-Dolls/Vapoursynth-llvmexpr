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

#ifndef LLVMEXPR_LLVMGENERATOR_HPP
#define LLVMEXPR_LLVMGENERATOR_HPP

#include "IRGeneratorBase.hpp"

class LLVMGenerator : public IRGeneratorBase {
  public:
    LLVMGenerator(
        const std::vector<Token>& tokens_in, const VSVideoInfo* out_vi,
        const std::vector<const VSVideoInfo*>& in_vi, int width_in,
        int height_in, bool mirror,
        const std::map<std::pair<int, std::string>, int>& p_map,
        const analysis::ExpressionAnalysisResults& analysis_results_in,
        llvm::LLVMContext& context_ref, llvm::Module& module_ref,
        llvm::IRBuilder<>& builder_ref, MathLibraryManager& math_mgr,
        std::string func_name_in, int approx_math_in);

  protected:
    void define_function_signature() override;
    void generate_loops() override;

    bool process_mode_specific_token(const Token& token,
                                     std::vector<llvm::Value*>& rpn_stack,
                                     llvm::Value* x, llvm::Value* y,
                                     llvm::Value* x_fp, llvm::Value* y_fp,
                                     bool no_x_bounds_check) override;

    void finalize_and_store_result(llvm::Value* result_val, llvm::Value* x,
                                   llvm::Value* y) override;

  private:
    void generate_x_loop_body(llvm::Value* x_var, llvm::Value* x_fp_var,
                              llvm::Value* y_var, llvm::Value* y_fp_var,
                              bool no_x_bounds_check);

    // Arrays
    std::map<std::string, llvm::Value*> named_arrays;
};

#endif // LLVMEXPR_LLVMGENERATOR_HPP
