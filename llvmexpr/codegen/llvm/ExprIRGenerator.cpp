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

#include "ExprIRGenerator.hpp"

#include <bit>
#include <format>

#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"

constexpr uint32_t EXIT_NAN_PAYLOAD = 0x7FC0E71F; // qNaN with payload 0xE71F

ExprIRGenerator::ExprIRGenerator(
    const std::vector<Token>& tokens_in, const VSVideoInfo* out_vi,
    const std::vector<const VSVideoInfo*>& in_vi, int width_in, int height_in,
    bool mirror, const std::map<std::pair<int, std::string>, int>& p_map,
    const analysis::ExpressionAnalysisResults& analysis_results_in,
    llvm::LLVMContext& context_ref, llvm::Module& module_ref,
    llvm::IRBuilder<>& builder_ref, MathLibraryManager& math_mgr,
    std::string func_name_in, int approx_math_in)
    : IRGeneratorBase(tokens_in, out_vi, in_vi, width_in, height_in, mirror,
                      p_map, analysis_results_in, context_ref, module_ref,
                      builder_ref, math_mgr, std::move(func_name_in),
                      approx_math_in) {}

void ExprIRGenerator::define_function_signature() {
    llvm::Type* void_ty = llvm::Type::getVoidTy(context);
    llvm::Type* ptr_ty = llvm::PointerType::get(context, 0);
    llvm::Type* context_ptr_ty = ptr_ty; // opaque pointer (void*)
    llvm::Type* i8_ptr_ptr_ty = ptr_ty; // opaque pointer (represents uint8_t**)
    llvm::Type* i32_ptr_ty = ptr_ty;    // opaque pointer (represents int32_t*)
    llvm::Type* float_ptr_ty = ptr_ty;  // opaque pointer (represents float*)

    llvm::FunctionType* func_ty = llvm::FunctionType::get(
        void_ty, {context_ptr_ty, i8_ptr_ptr_ty, i32_ptr_ty, float_ptr_ty},
        false);

    func = llvm::Function::Create(func_ty, llvm::Function::ExternalLinkage,
                                  func_name, &module);
    func->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);

    // Context argument (index 0) not used in Expr mode
    rwptrs_arg = func->getArg(1);
    rwptrs_arg->setName("rwptrs");
    strides_arg = func->getArg(2);
    strides_arg->setName("strides");
    props_arg = func->getArg(3);
    props_arg->setName("props");

    func->addParamAttr(2, llvm::Attribute::ReadOnly); // strides (int32_t*)
    func->addParamAttr(3, llvm::Attribute::ReadOnly); // props (float*)
}

void ExprIRGenerator::generate_loops() {
    llvm::BasicBlock* entry_bb =
        llvm::BasicBlock::Create(context, "entry", func);
    builder.SetInsertPoint(entry_bb);

    llvm::Function* parent_func = builder.GetInsertBlock()->getParent();
    llvm::BasicBlock* loop_y_header =
        llvm::BasicBlock::Create(context, "loop_y_header", parent_func);
    llvm::BasicBlock* loop_y_body =
        llvm::BasicBlock::Create(context, "loop_y_body", parent_func);
    llvm::BasicBlock* loop_y_exit =
        llvm::BasicBlock::Create(context, "loop_y_exit", parent_func);

    llvm::Value* y_var =
        builder.CreateAlloca(builder.getInt32Ty(), nullptr, "y.var");
    llvm::Value* x_var =
        builder.CreateAlloca(builder.getInt32Ty(), nullptr, "x.var");
    builder.CreateStore(builder.getInt32(0), y_var);

    const auto& coord_usage = analysis_results.getCoordinateUsageResult();

    llvm::Value* x_fp_var = nullptr;
    if (coord_usage.uses_x) {
        x_fp_var = createAllocaInEntry(builder.getFloatTy(), "x_fp.var");
    }
    llvm::Value* y_fp_var = nullptr;
    if (coord_usage.uses_y) {
        y_fp_var = createAllocaInEntry(builder.getFloatTy(), "y_fp.var");
        builder.CreateStore(llvm::ConstantFP::get(builder.getFloatTy(), 0.0),
                            y_fp_var);
    }

    // Index 0 = dst, 1..num_inputs = sources
    preloaded_base_ptrs.resize(num_inputs + 1);
    preloaded_strides.resize(num_inputs + 1);
    for (int i = 0; i <= num_inputs; ++i) {
        llvm::Value* base_ptr_i = builder.CreateLoad(
            llvm::PointerType::get(context, 0),
            builder.CreateGEP(llvm::PointerType::get(context, 0), rwptrs_arg,
                              builder.getInt32(i)));
        llvm::Value* stride_i = builder.CreateLoad(
            builder.getInt32Ty(),
            builder.CreateGEP(builder.getInt32Ty(), strides_arg,
                              builder.getInt32(i)));
        preloaded_base_ptrs[i] = base_ptr_i;
        preloaded_strides[i] = stride_i;

        assumeAligned(base_ptr_i,
                      32); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
    }

    alias_scope_domain = llvm::MDNode::getDistinct(context, {});
    alias_scopes.resize(num_inputs + 1);
    for (int i = 0; i <= num_inputs; ++i) {
        llvm::SmallVector<llvm::Metadata*, 2> elems;
        elems.push_back(nullptr);
        llvm::Metadata* name_node = llvm::MDNode::get(
            context, {llvm::MDString::get(
                         context, std::format("rwptrs_{}", i).c_str())});
        elems.push_back(name_node);
        alias_scopes[i] = llvm::MDNode::getDistinct(context, elems);
        alias_scopes[i]->replaceOperandWith(0, alias_scopes[i]);
    }
    alias_scope_lists.resize(num_inputs + 1);
    noalias_scope_lists.resize(num_inputs + 1);
    for (int i = 0; i <= num_inputs; ++i) {
        std::vector<llvm::Metadata*> self_list = {alias_scopes[i]};
        alias_scope_lists[i] = llvm::MDNode::get(context, self_list);
        std::vector<llvm::Metadata*> others;
        for (int j = 0; j <= num_inputs; ++j) {
            if (j == i) {
                continue;
            }
            others.push_back(alias_scopes[j]);
        }
        noalias_scope_lists[i] = llvm::MDNode::get(context, others);
    }

    builder.CreateBr(loop_y_header);

    builder.SetInsertPoint(loop_y_header);
    llvm::Value* y_val = builder.CreateLoad(builder.getInt32Ty(), y_var, "y");
    llvm::Value* y_cond =
        builder.CreateICmpSLT(y_val, builder.getInt32(height), "y.cond");
    builder.CreateCondBr(y_cond, loop_y_body, loop_y_exit);

    builder.SetInsertPoint(loop_y_body);

    // Pre-calculate and cache row pointers
    row_ptr_cache.clear();
    const auto& clip_access_result =
        analysis_results.getRelAccessAnalysisResult();
    for (const auto& access : clip_access_result.unique_rel_y_accesses) {
        int clip_idx = access.clip_idx;
        int vs_clip_idx = clip_idx + 1;
        int rel_y = access.rel_y;

        llvm::Value* coord_y =
            builder.CreateAdd(y_val, builder.getInt32(rel_y));
        llvm::Value* final_y = get_final_coord(
            coord_y, builder.getInt32(height), access.use_mirror);

        llvm::Value* base_ptr = preloaded_base_ptrs[vs_clip_idx];
        llvm::Value* stride = preloaded_strides[vs_clip_idx];

        llvm::Value* y_offset = builder.CreateMul(final_y, stride);
        llvm::Value* row_ptr = builder.CreateGEP(builder.getInt8Ty(), base_ptr,
                                                 y_offset, "row_ptr");
        row_ptr_cache[access] = row_ptr;
    }

    llvm::Value* width_val = builder.getInt32(width);
    llvm::Value* start_main_x = builder.getInt32(-clip_access_result.min_rel_x);
    llvm::Value* end_main_x =
        builder.getInt32(width - clip_access_result.max_rel_x);

    bool has_left_peel = // NOLINT(cppcoreguidelines-init-variables)
        clip_access_result.min_rel_x < 0;
    bool has_right_peel = // NOLINT(cppcoreguidelines-init-variables)
        clip_access_result.max_rel_x > 0;

    llvm::BasicBlock* loop_x_start_bb =
        llvm::BasicBlock::Create(context, "loop_x_start", parent_func);
    llvm::BasicBlock* loop_x_exit_bb =
        llvm::BasicBlock::Create(context, "loop_x_exit", parent_func);

    builder.CreateBr(loop_x_start_bb);
    builder.SetInsertPoint(loop_x_start_bb);

    builder.CreateStore(builder.getInt32(0), x_var);
    if (coord_usage.uses_x) {
        builder.CreateStore(llvm::ConstantFP::get(builder.getFloatTy(), 0.0),
                            x_fp_var);
    }

    if (has_left_peel) {
        llvm::BasicBlock* left_peel_header =
            llvm::BasicBlock::Create(context, "left_peel_header", parent_func);
        llvm::BasicBlock* left_peel_body =
            llvm::BasicBlock::Create(context, "left_peel_body", parent_func);
        llvm::BasicBlock* after_left_peel =
            llvm::BasicBlock::Create(context, "after_left_peel", parent_func);

        builder.CreateBr(left_peel_header);
        builder.SetInsertPoint(left_peel_header);
        llvm::Value* x_val =
            builder.CreateLoad(builder.getInt32Ty(), x_var, "x_peel_l");
        llvm::Value* cond = builder.CreateICmpSLT(x_val, start_main_x);
        llvm::BranchInst* left_peel_br =
            builder.CreateCondBr(cond, left_peel_body, after_left_peel);
        add_loop_metadata(left_peel_br);

        builder.SetInsertPoint(left_peel_body);
        generate_x_loop_body(x_var, x_fp_var, y_var, y_fp_var, false);
        builder.CreateBr(left_peel_header);

        builder.SetInsertPoint(after_left_peel);
    }

    llvm::BasicBlock* main_loop_header =
        llvm::BasicBlock::Create(context, "main_loop_header", parent_func);
    llvm::BasicBlock* main_loop_body =
        llvm::BasicBlock::Create(context, "main_loop_body", parent_func);
    llvm::BasicBlock* after_main_loop =
        llvm::BasicBlock::Create(context, "after_main_loop", parent_func);

    builder.CreateBr(main_loop_header);
    builder.SetInsertPoint(main_loop_header);
    llvm::Value* x_val_main =
        builder.CreateLoad(builder.getInt32Ty(), x_var, "x_main");
    llvm::Value* main_cond = builder.CreateICmpSLT(x_val_main, end_main_x);

    llvm::BranchInst* loop_br =
        builder.CreateCondBr(main_cond, main_loop_body, after_main_loop);
    add_loop_metadata(loop_br);

    builder.SetInsertPoint(main_loop_body);
    generate_x_loop_body(x_var, x_fp_var, y_var, y_fp_var, true);
    builder.CreateBr(main_loop_header);

    builder.SetInsertPoint(after_main_loop);

    if (has_right_peel) {
        llvm::BasicBlock* right_peel_header =
            llvm::BasicBlock::Create(context, "right_peel_header", parent_func);
        llvm::BasicBlock* right_peel_body =
            llvm::BasicBlock::Create(context, "right_peel_body", parent_func);

        builder.CreateBr(right_peel_header);
        builder.SetInsertPoint(right_peel_header);
        llvm::Value* x_val =
            builder.CreateLoad(builder.getInt32Ty(), x_var, "x_peel_r");
        llvm::Value* cond = builder.CreateICmpSLT(x_val, width_val);
        llvm::BranchInst* right_peel_br =
            builder.CreateCondBr(cond, right_peel_body, loop_x_exit_bb);
        add_loop_metadata(right_peel_br);

        builder.SetInsertPoint(right_peel_body);
        generate_x_loop_body(x_var, x_fp_var, y_var, y_fp_var, false);
        builder.CreateBr(right_peel_header);
    } else {
        builder.CreateBr(loop_x_exit_bb);
    }

    builder.SetInsertPoint(loop_x_exit_bb);
    llvm::Value* y_next = builder.CreateAdd(y_val, builder.getInt32(1));
    builder.CreateStore(y_next, y_var);
    if (coord_usage.uses_y) {
        llvm::Value* y_fp_val =
            builder.CreateLoad(builder.getFloatTy(), y_fp_var);
        llvm::Value* y_fp_next = builder.CreateFAdd(
            y_fp_val, llvm::ConstantFP::get(builder.getFloatTy(), 1.0));
        builder.CreateStore(y_fp_next, y_fp_var);
    }
    builder.CreateBr(loop_y_header);

    builder.SetInsertPoint(loop_y_exit);
    builder.CreateRetVoid();
}

void ExprIRGenerator::generate_x_loop_body(llvm::Value* x_var,
                                           llvm::Value* x_fp_var,
                                           llvm::Value* y_var,
                                           llvm::Value* y_fp_var,
                                           bool no_x_bounds_check) {
    const auto& coord_usage = analysis_results.getCoordinateUsageResult();
    llvm::Value* x_val = builder.CreateLoad(builder.getInt32Ty(), x_var, "x");
    llvm::Value* y_val =
        builder.CreateLoad(builder.getInt32Ty(), y_var, "y_in_x_loop");

    llvm::Value* x_fp = nullptr;
    if (coord_usage.uses_x) {
        x_fp = builder.CreateLoad(builder.getFloatTy(), x_fp_var, "x_fp");
    }
    llvm::Value* y_fp = nullptr;
    if (coord_usage.uses_y) {
        y_fp = builder.CreateLoad(builder.getFloatTy(), y_fp_var, "y_fp");
    }

    generate_ir_from_tokens(x_val, y_val, x_fp, y_fp, no_x_bounds_check);

    llvm::Value* x_next = builder.CreateAdd(x_val, builder.getInt32(1));
    builder.CreateStore(x_next, x_var);
    if (coord_usage.uses_x) {
        llvm::Value* x_fp_next = builder.CreateFAdd(
            x_fp, llvm::ConstantFP::get(builder.getFloatTy(), 1.0));
        builder.CreateStore(x_fp_next, x_fp_var);
    }
}

bool ExprIRGenerator::process_mode_specific_token(
    const Token& token, std::vector<llvm::Value*>& rpn_stack, llvm::Value* x,
    [[maybe_unused]] llvm::Value* y, llvm::Value* x_fp, llvm::Value* y_fp,
    bool no_x_bounds_check) {
    llvm::Type* float_ty = builder.getFloatTy();
    llvm::Type* i32_ty = builder.getInt32Ty();

    switch (token.type) {
    case TokenType::CONSTANT_X:
        rpn_stack.push_back(x_fp);
        return true;
    case TokenType::CONSTANT_Y:
        rpn_stack.push_back(y_fp);
        return true;

    case TokenType::CLIP_REL: {
        const auto& payload = std::get<TokenPayload_ClipAccess>(token.payload);
        bool use_mirror = // NOLINT(cppcoreguidelines-init-variables)
            payload.has_mode ? payload.use_mirror : mirror_boundary;
        analysis::RelYAccess access{.clip_idx = payload.clip_idx,
                                    .rel_y = payload.rel_y,
                                    .use_mirror = use_mirror};
        llvm::Value* row_ptr = row_ptr_cache.at(access);
        rpn_stack.push_back(generate_load_from_row_ptr(
            row_ptr, payload.clip_idx, x, payload.rel_x, use_mirror,
            no_x_bounds_check));
        return true;
    }
    case TokenType::CLIP_ABS: {
        const auto& payload = std::get<TokenPayload_ClipAccess>(token.payload);
        llvm::Value* coord_y_f = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* coord_x_f = rpn_stack.back();
        rpn_stack.pop_back();

        llvm::Value* coord_y =
            builder.CreateCall(llvm::Intrinsic::getOrInsertDeclaration(
                                   &module, llvm::Intrinsic::rint, {float_ty}),
                               {coord_y_f});
        coord_y = builder.CreateFPToSI(coord_y, i32_ty);

        llvm::Value* coord_x =
            builder.CreateCall(llvm::Intrinsic::getOrInsertDeclaration(
                                   &module, llvm::Intrinsic::rint, {float_ty}),
                               {coord_x_f});
        coord_x = builder.CreateFPToSI(coord_x, i32_ty);

        bool use_mirror_final = false;
        if (payload.has_mode) {
            use_mirror_final = payload.use_mirror;
        } else {
            use_mirror_final = mirror_boundary;
        }

        rpn_stack.push_back(generate_pixel_load(payload.clip_idx, coord_x,
                                                coord_y, use_mirror_final));
        return true;
    }
    case TokenType::CLIP_CUR: {
        const auto& payload = std::get<TokenPayload_ClipAccess>(token.payload);
        analysis::RelYAccess access{.clip_idx = payload.clip_idx,
                                    .rel_y = 0,
                                    .use_mirror = mirror_boundary};
        llvm::Value* row_ptr = row_ptr_cache.at(access);
        rpn_stack.push_back(
            generate_load_from_row_ptr(row_ptr, payload.clip_idx, x, 0,
                                       mirror_boundary, no_x_bounds_check));
        return true;
    }

    case TokenType::EXIT_NO_WRITE: {
        rpn_stack.push_back(llvm::ConstantFP::get(
            float_ty, std::bit_cast<float>(EXIT_NAN_PAYLOAD)));
        return true;
    }

    case TokenType::PROP_ACCESS: {
        const auto& payload = std::get<TokenPayload_PropAccess>(token.payload);
        auto key = std::make_pair(payload.clip_idx, payload.prop_name);
        int prop_idx = // NOLINT(cppcoreguidelines-init-variables)
            prop_map.at(key);
        llvm::Value* prop_val = builder.CreateLoad(
            float_ty,
            builder.CreateGEP(float_ty, props_arg, builder.getInt32(prop_idx)));
        rpn_stack.push_back(prop_val);
        return true;
    }

    case TokenType::PROP_EXISTS: {
        const auto& payload = std::get<TokenPayload_PropAccess>(token.payload);
        auto key = std::make_pair(payload.clip_idx, payload.prop_name);
        llvm::Value* exists_val = nullptr;
        if (prop_map.contains(key)) {
            int prop_idx = prop_map.at(key);
            llvm::Value* prop_val = builder.CreateLoad(
                float_ty, builder.CreateGEP(float_ty, props_arg,
                                            builder.getInt32(prop_idx)));

            llvm::Value* prop_val_int = builder.CreateBitCast(prop_val, i32_ty);

            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            llvm::Value* nan_payload_int = builder.getInt32(0x7FC0BEEF);
            llvm::Value* is_prop_read_nan =
                builder.CreateICmpEQ(prop_val_int, nan_payload_int);

            exists_val = builder.CreateSelect(
                is_prop_read_nan, llvm::ConstantFP::get(float_ty, 0.0),
                llvm::ConstantFP::get(float_ty, 1.0));
        } else {
            exists_val = llvm::ConstantFP::get(float_ty, 0.0);
        }
        rpn_stack.push_back(exists_val);
        return true;
    }

    case TokenType::STORE_ABS: {
        llvm::Value* coord_y_f = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* coord_x_f = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* val_to_store = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* coord_y = builder.CreateFPToSI(coord_y_f, i32_ty);
        llvm::Value* coord_x = builder.CreateFPToSI(coord_x_f, i32_ty);
        generate_pixel_store(val_to_store, coord_x, coord_y);
        return true;
    }

    // Array
    case TokenType::ARRAY_ALLOC_STATIC: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        if (!named_arrays.contains(payload.name)) {
            llvm::ArrayType* array_ty =
                llvm::ArrayType::get(float_ty, payload.static_size);
            llvm::Value* array_ptr =
                createAllocaInEntry(array_ty, payload.name + "_array");
            named_arrays[payload.name] = array_ptr;
        }
        return true;
    }

    case TokenType::ARRAY_LOAD: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        llvm::Value* idx_f = rpn_stack.back();
        rpn_stack.pop_back();

        llvm::Value* idx = builder.CreateFPToSI(idx_f, i32_ty);

        llvm::Value* array_ptr = named_arrays.at(payload.name);

        llvm::Value* elem_ptr = builder.CreateInBoundsGEP(
            llvm::cast<llvm::AllocaInst>(array_ptr)->getAllocatedType(),
            array_ptr, {builder.getInt32(0), idx});

        llvm::Value* value = builder.CreateLoad(float_ty, elem_ptr);
        rpn_stack.push_back(value);
        return true;
    }

    case TokenType::ARRAY_STORE: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        llvm::Value* idx_f = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* value = rpn_stack.back();
        rpn_stack.pop_back();

        llvm::Value* idx = builder.CreateFPToSI(idx_f, i32_ty);

        llvm::Value* array_ptr = named_arrays.at(payload.name);

        llvm::Value* elem_ptr = builder.CreateInBoundsGEP(
            llvm::cast<llvm::AllocaInst>(array_ptr)->getAllocatedType(),
            array_ptr, {builder.getInt32(0), idx});

        builder.CreateStore(value, elem_ptr);
        return true;
    }

    default:
        // Token not handled by this mode
        return false;
    }
}

void ExprIRGenerator::finalize_and_store_result(llvm::Value* result_val,
                                                llvm::Value* x,
                                                llvm::Value* y) {
    bool has_exit = false;
    has_exit = std::ranges::any_of(tokens, [](const auto& token) {
        return token.type == TokenType::EXIT_NO_WRITE;
    });

    if (has_exit) {
        llvm::Function* parent_func = builder.GetInsertBlock()->getParent();
        llvm::Value* result_int =
            builder.CreateBitCast(result_val, builder.getInt32Ty());
        llvm::Value* exit_nan_int = builder.getInt32(EXIT_NAN_PAYLOAD);
        llvm::Value* is_exit_val =
            builder.CreateICmpEQ(result_int, exit_nan_int);

        llvm::BasicBlock* store_block =
            llvm::BasicBlock::Create(context, "do_default_store", parent_func);
        llvm::BasicBlock* after_store_block = llvm::BasicBlock::Create(
            context, "after_default_store", parent_func);

        builder.CreateCondBr(is_exit_val, after_store_block, store_block);

        builder.SetInsertPoint(store_block);
        generate_pixel_store(result_val, x, y);
        builder.CreateBr(after_store_block);

        builder.SetInsertPoint(after_store_block);
    } else {
        generate_pixel_store(result_val, x, y);
    }
}
