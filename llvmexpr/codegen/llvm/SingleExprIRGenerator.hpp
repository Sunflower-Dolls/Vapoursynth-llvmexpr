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

#ifndef LLVMEXPR_SINGLEEXPRIRGENERATOR_HPP
#define LLVMEXPR_SINGLEEXPRIRGENERATOR_HPP

#include "IRGeneratorBase.hpp"

class SingleExprIRGenerator : public IRGeneratorBase {
  public:
    SingleExprIRGenerator(
        const std::vector<Token>& tokens_in, const VSVideoInfo* out_vi,
        const std::vector<const VSVideoInfo*>& in_vi, bool mirror,
        const std::map<std::pair<int, std::string>, int>& p_map,
        const std::vector<std::string>& output_props,
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
    llvm::Value* generate_pixel_load_plane(int clip_idx, int plane_idx,
                                           llvm::Value* x, llvm::Value* y);
    void generate_pixel_store_plane(llvm::Value* value_to_store, int plane_idx,
                                    llvm::Value* x, llvm::Value* y);

    std::vector<std::vector<llvm::Value*>> plane_base_ptrs;
    std::vector<std::vector<llvm::Value*>> plane_strides;
    std::map<std::string, llvm::Value*> prop_allocas;
    const std::vector<std::string>& output_props_list;
    std::map<std::string, int> output_prop_map;

    // Array
    llvm::Value* context_arg = nullptr;
    llvm::Function* llvmexpr_ensure_buffer_func = nullptr;
    llvm::Function* llvmexpr_get_buffer_size_func = nullptr;
    std::map<std::string, llvm::Value*> array_ptr_cache;

    std::map<std::string, llvm::Value*> named_arrays;
};

#endif // LLVMEXPR_SINGLEEXPRIRGENERATOR_HPP
