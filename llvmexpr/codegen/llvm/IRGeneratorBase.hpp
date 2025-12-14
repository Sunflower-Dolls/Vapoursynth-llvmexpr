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

#ifndef LLVMEXPR_IRGENERATORBASE_HPP
#define LLVMEXPR_IRGENERATORBASE_HPP

#include "../../analysis/AnalysisResults.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "Math.hpp"

#include <map>
#include <string>
#include <vector>

#include "VapourSynth4.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/IR/Module.h"

class IRGeneratorBase {
  public:
    IRGeneratorBase(
        const std::vector<Token>& tokens_in, const VSVideoInfo* out_vi,
        const std::vector<const VSVideoInfo*>& in_vi, int width_in,
        int height_in, bool mirror,
        const std::map<std::pair<int, std::string>, int>& p_map,
        const analysis::ExpressionAnalysisResults& analysis_results_in,
        llvm::LLVMContext& context_ref, llvm::Module& module_ref,
        llvm::IRBuilder<>& builder_ref, MathLibraryManager& math_mgr,
        std::string func_name_in, int approx_math_in);

    virtual ~IRGeneratorBase() = default;

    IRGeneratorBase(const IRGeneratorBase&) = delete;
    IRGeneratorBase& operator=(const IRGeneratorBase&) = delete;
    IRGeneratorBase(IRGeneratorBase&&) = delete;
    IRGeneratorBase& operator=(IRGeneratorBase&&) = delete;

    void generate();

  protected:
    // NOLINTBEGIN(cppcoreguidelines-non-private-member-variables-in-classes)
    // Input parameters
    const std::vector<Token>& tokens;
    const VSVideoInfo* vo;
    const std::vector<const VSVideoInfo*>& vi;
    int num_inputs;
    int width;
    int height;
    bool mirror_boundary;
    const std::map<std::pair<int, std::string>, int>& prop_map;
    const analysis::ExpressionAnalysisResults& analysis_results;
    std::string func_name;
    int approx_math;

    llvm::LLVMContext& context;
    llvm::Module& module;
    llvm::IRBuilder<>& builder;
    MathLibraryManager& math_manager;

    llvm::Function* func;
    llvm::Value* rwptrs_arg;
    llvm::Value* strides_arg;
    llvm::Value* props_arg;

    std::vector<llvm::Value*> preloaded_base_ptrs;
    std::vector<llvm::Value*> preloaded_strides;

    llvm::MDNode* alias_scope_domain;
    std::vector<llvm::MDNode*> alias_scopes;
    std::vector<llvm::MDNode*> alias_scope_lists;
    std::vector<llvm::MDNode*> noalias_scope_lists;

    std::map<analysis::RelYAccess, llvm::Value*> row_ptr_cache;

    // NOLINTEND(cppcoreguidelines-non-private-member-variables-in-classes)

    virtual void defineFunctionSignature() = 0;

    virtual void generateLoops() = 0;

    llvm::AllocaInst* createAllocaInEntry(llvm::Type* type,
                                          const std::string& name);

    template <typename... Args>
    llvm::Value* createIntrinsicCall(llvm::Intrinsic::ID intrinsic_id,
                                     Args... args);

    void assumeAligned(llvm::Value* ptr_value, unsigned alignment);

    template <typename MemInstT>
    void setMemoryInstAttrs(MemInstT* inst, unsigned alignment,
                            int rwptr_index);

    llvm::Value* getFinalCoord(llvm::Value* coord, llvm::Value* max_dim,
                               bool use_mirror);

    llvm::Value* generateLoadFromRowPtr(llvm::Value* row_ptr, int clip_idx,
                                        llvm::Value* x, int rel_x,
                                        bool use_mirror,
                                        bool no_x_bounds_check);

    void addLoopMetadata(llvm::BranchInst* loop_br);

    llvm::Value* generatePixelLoad(int clip_idx, llvm::Value* x, llvm::Value* y,
                                   bool mirror);

    void generatePixelStore(llvm::Value* value_to_store, llvm::Value* x,
                            llvm::Value* y);

    void generateIRFromTokens(llvm::Value* x, llvm::Value* y, llvm::Value* x_fp,
                              llvm::Value* y_fp, bool no_x_bounds_check);

    bool processCommonToken(const Token& token,
                            std::vector<llvm::Value*>& rpn_stack,
                            llvm::Type* float_ty, llvm::Type* i32_ty,
                            bool use_approx_math);

    virtual bool processModeSpecificToken(const Token& token,
                                          std::vector<llvm::Value*>& rpn_stack,
                                          llvm::Value* x, llvm::Value* y,
                                          llvm::Value* x_fp, llvm::Value* y_fp,
                                          bool no_x_bounds_check) = 0;

    virtual void finalizeAndStoreResult(llvm::Value* result_val, llvm::Value* x,
                                        llvm::Value* y) = 0;
};

template <typename... Args>
llvm::Value*
IRGeneratorBase::createIntrinsicCall(llvm::Intrinsic::ID intrinsic_id,
                                     Args... args) {
    static_assert(sizeof...(Args) >= 1, "At least one argument required");
    llvm::SmallVector<llvm::Value*, 4> arg_vec{args...};
    auto* callee = llvm::Intrinsic::getOrInsertDeclaration(
        &module, intrinsic_id, {arg_vec[0]->getType()});
    auto* call = builder.CreateCall(callee, arg_vec);
    call->setFastMathFlags(builder.getFastMathFlags());
    return call;
}

template <typename MemInstT>
void IRGeneratorBase::setMemoryInstAttrs(MemInstT* inst, unsigned alignment,
                                         int rwptr_index) {
    inst->setAlignment(llvm::Align(alignment));
    inst->setMetadata(llvm::LLVMContext::MD_alias_scope,
                      alias_scope_lists[rwptr_index]);
    inst->setMetadata(llvm::LLVMContext::MD_noalias,
                      noalias_scope_lists[rwptr_index]);
}

#endif // LLVMEXPR_IRGENERATORBASE_HPP
