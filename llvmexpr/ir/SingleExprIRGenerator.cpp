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

#include "SingleExprIRGenerator.hpp"

#include <format>

#include "llvm/IR/Constants.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/Instructions.h"

SingleExprIRGenerator::SingleExprIRGenerator(
    const std::vector<Token>& tokens_in, const VSVideoInfo* out_vi,
    const std::vector<const VSVideoInfo*>& in_vi, bool mirror,
    const std::map<std::pair<int, std::string>, int>& p_map,
    const std::vector<std::string>& output_props,
    const analysis::ExpressionAnalysisResults& analysis_results_in,
    llvm::LLVMContext& context_ref, llvm::Module& module_ref,
    llvm::IRBuilder<>& builder_ref, MathLibraryManager& math_mgr,
    std::string func_name_in, int approx_math_in)
    : IRGeneratorBase(tokens_in, out_vi, in_vi, out_vi->width, out_vi->height,
                      mirror, p_map, analysis_results_in, context_ref,
                      module_ref, builder_ref, math_mgr,
                      std::move(func_name_in), approx_math_in),
      output_props_list(output_props) {

    for (size_t i = 0; i < output_props_list.size(); ++i) {
        output_prop_map[output_props_list[i]] = static_cast<int>(i);
    }
}

void SingleExprIRGenerator::define_function_signature() {
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

    context_arg = func->getArg(0);
    context_arg->setName("context");
    rwptrs_arg = func->getArg(1);
    rwptrs_arg->setName("rwptrs");
    strides_arg = func->getArg(2);
    strides_arg->setName("strides");
    props_arg = func->getArg(3);
    props_arg->setName("props");

    func->addParamAttr(2, llvm::Attribute::ReadOnly); // strides (int32_t*)

    // Declare Host API functions for dynamic array management
    llvm::Type* i64_ty = builder.getInt64Ty();
    llvm::Type* i8_ptr_ty = ptr_ty;

    // float* llvmexpr_ensure_buffer(const char* name, int64_t size)
    llvm::FunctionType* ensure_buffer_ty =
        llvm::FunctionType::get(float_ptr_ty, {i8_ptr_ty, i64_ty}, false);
    llvmexpr_ensure_buffer_func = llvm::Function::Create(
        ensure_buffer_ty, llvm::Function::ExternalLinkage,
        "llvmexpr_ensure_buffer", &module);

    // int64_t llvmexpr_get_buffer_size(const char* name)
    llvm::FunctionType* get_buffer_size_ty =
        llvm::FunctionType::get(i64_ty, {i8_ptr_ty}, false);
    llvmexpr_get_buffer_size_func = llvm::Function::Create(
        get_buffer_size_ty, llvm::Function::ExternalLinkage,
        "llvmexpr_get_buffer_size", &module);
}

void SingleExprIRGenerator::
    generate_loops() { // TODO: rename this. Nothing to do with loops here.
    llvm::BasicBlock* entry_bb =
        llvm::BasicBlock::Create(context, "entry", func);
    builder.SetInsertPoint(entry_bb);

    // Pre-load all plane pointers and strides
    plane_base_ptrs.resize(num_inputs + 1);
    plane_strides.resize(num_inputs + 1);
    const int num_planes = // NOLINT(cppcoreguidelines-init-variables)
        vo->format.numPlanes;
    for (int i = 0; i <= num_inputs; ++i) {
        plane_base_ptrs[i].resize(num_planes);
        plane_strides[i].resize(num_planes);
        for (int p = 0; p < num_planes; ++p) {
            int flat_idx = (i * num_planes) + p;
            llvm::Value* base_ptr_i = builder.CreateLoad(
                llvm::PointerType::get(context, 0),
                builder.CreateGEP(llvm::PointerType::get(context, 0),
                                  rwptrs_arg, builder.getInt32(flat_idx)));
            llvm::Value* stride_i = builder.CreateLoad(
                builder.getInt32Ty(),
                builder.CreateGEP(builder.getInt32Ty(), strides_arg,
                                  builder.getInt32(flat_idx)));
            plane_base_ptrs[i][p] = base_ptr_i;
            plane_strides[i][p] = stride_i;
        }
    }

    // Aligned loads for properties
    for (const auto& [key, idx] : prop_map) {
        llvm::Value* prop_val = builder.CreateLoad(
            builder.getFloatTy(),
            builder.CreateGEP(builder.getFloatTy(), props_arg,
                              builder.getInt32(idx)));
        const std::string unique_prop_name =
            std::format("prop_{}_{}", key.first, key.second);
        llvm::Value* alloca =
            createAllocaInEntry(builder.getFloatTy(), unique_prop_name);
        builder.CreateStore(prop_val, alloca);
        prop_allocas[unique_prop_name] = alloca;
    }

    //NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
    llvm::APInt payload_bits(32, 0x7FC0DEAD);
    llvm::APFloat nan_payload_apf(llvm::APFloat::IEEEsingle(), payload_bits);
    llvm::Value* nan_with_payload =
        llvm::ConstantFP::get(context, nan_payload_apf);

    for (const auto& prop_name : output_props_list) {
        const std::string mangled_input_name =
            std::format("prop_0_{}", prop_name);
        if (prop_allocas.contains(mangled_input_name)) {
            prop_allocas[prop_name] = prop_allocas.at(mangled_input_name);
        } else {
            if (!prop_allocas.contains(prop_name)) {
                llvm::Value* alloca =
                    createAllocaInEntry(builder.getFloatTy(), prop_name);
                builder.CreateStore(nan_with_payload, alloca);
                prop_allocas[prop_name] = alloca;
            }
        }
    }

    // Only generate IR if there are tokens to process
    if (!tokens.empty()) {
        generate_ir_from_tokens(nullptr, nullptr, nullptr, nullptr, true);
    }

    // Store output properties back
    for (const auto& [name, idx] : output_prop_map) {
        llvm::Value* val =
            builder.CreateLoad(builder.getFloatTy(), prop_allocas.at(name));
        builder.CreateStore(
            val,
            builder.CreateGEP(builder.getFloatTy(), props_arg,
                              builder.getInt32(prop_map.size() + 1 + idx)));
    }

    builder.CreateRetVoid();
}

llvm::Value* SingleExprIRGenerator::generate_pixel_load_plane(int clip_idx,
                                                              int plane_idx,
                                                              llvm::Value* x,
                                                              llvm::Value* y) {
    const VSVideoInfo* vinfo = vi[clip_idx];
    int plane_w = vinfo->width >> vinfo->format.subSamplingW;
    int plane_h = vinfo->height >> vinfo->format.subSamplingH;

    llvm::Value* final_x =
        get_final_coord(x, builder.getInt32(plane_w), mirror_boundary);
    llvm::Value* final_y =
        get_final_coord(y, builder.getInt32(plane_h), mirror_boundary);

    llvm::Value* base_ptr = plane_base_ptrs[clip_idx + 1][plane_idx];
    llvm::Value* stride = plane_strides[clip_idx + 1][plane_idx];

    llvm::Value* y_offset = builder.CreateMul(final_y, stride);
    llvm::Value* row_ptr =
        builder.CreateGEP(builder.getInt8Ty(), base_ptr, y_offset);

    const VSVideoFormat& format = vinfo->format;
    int bpp = format.bytesPerSample;

    llvm::Value* x_offset = builder.CreateMul(final_x, builder.getInt32(bpp));
    llvm::Value* pixel_addr =
        builder.CreateGEP(builder.getInt8Ty(), row_ptr, x_offset);

    llvm::Value* loaded_val = nullptr;
    if (format.sampleType == stInteger) {
        llvm::Type* load_type = nullptr;
        if (bpp == 1) {
            load_type = builder.getInt8Ty();
        } else if (bpp == 2) {
            load_type = builder.getInt16Ty();
        } else {
            load_type = builder.getInt32Ty();
        }
        llvm::LoadInst* li = builder.CreateLoad(load_type, pixel_addr);
        loaded_val = builder.CreateZExtOrBitCast(li, builder.getInt32Ty());
        return builder.CreateUIToFP(loaded_val, builder.getFloatTy());
    } // stFloat
    if (bpp == 4) {
        return builder.CreateLoad(builder.getFloatTy(), pixel_addr);
    }
    if (bpp == 2) {
        llvm::Value* half_val =
            builder.CreateLoad(builder.getHalfTy(), pixel_addr);
        return builder.CreateFPExt(half_val, builder.getFloatTy());
    }
    throw std::runtime_error("Unsupported float sample size.");
}

void SingleExprIRGenerator::generate_pixel_store_plane(
    llvm::Value* value_to_store, int plane_idx, llvm::Value* x,
    llvm::Value* y) {
    const VSVideoFormat& format = vo->format;
    int bpp = format.bytesPerSample;

    llvm::Value* base_ptr = plane_base_ptrs[0][plane_idx];
    llvm::Value* stride = plane_strides[0][plane_idx];

    llvm::Value* y_offset = builder.CreateMul(y, stride);
    llvm::Value* x_offset = builder.CreateMul(x, builder.getInt32(bpp));
    llvm::Value* total_offset = builder.CreateAdd(y_offset, x_offset);
    llvm::Value* pixel_addr =
        builder.CreateGEP(builder.getInt8Ty(), base_ptr, total_offset);

    llvm::Value* final_val = nullptr;
    if (format.sampleType == stInteger) {
        int max_val = (1 << format.bitsPerSample) - 1;
        llvm::Value* zero_f = llvm::ConstantFP::get(builder.getFloatTy(), 0.0);
        llvm::Value* max_f = llvm::ConstantFP::get(
            builder.getFloatTy(), static_cast<double>(max_val));

        llvm::Value* temp = createIntrinsicCall(llvm::Intrinsic::maxnum,
                                                value_to_store, zero_f);
        llvm::Value* clamped_f =
            createIntrinsicCall(llvm::Intrinsic::minnum, temp, max_f);

        llvm::Value* rounded_f =
            createIntrinsicCall(llvm::Intrinsic::roundeven, clamped_f);

        llvm::Type* store_type = nullptr;
        if (bpp == 1) {
            store_type = builder.getInt8Ty();
        } else if (bpp == 2) {
            store_type = builder.getInt16Ty();
        } else {
            store_type = builder.getInt32Ty();
        }
        final_val = builder.CreateFPToUI(rounded_f, store_type);
        builder.CreateStore(final_val, pixel_addr);
    } else {
        if (bpp == 4) {
            builder.CreateStore(value_to_store, pixel_addr);
        } else if (bpp == 2) {
            llvm::Value* truncated_val =
                builder.CreateFPTrunc(value_to_store, builder.getHalfTy());
            builder.CreateStore(truncated_val, pixel_addr);
        } else {
            throw std::runtime_error("Unsupported float sample size.");
        }
    }
}

bool SingleExprIRGenerator::process_mode_specific_token(
    const Token& token, std::vector<llvm::Value*>& rpn_stack,
    [[maybe_unused]] llvm::Value* x, [[maybe_unused]] llvm::Value* y,
    [[maybe_unused]] llvm::Value* x_fp, [[maybe_unused]] llvm::Value* y_fp,
    [[maybe_unused]] bool no_x_bounds_check) {
    llvm::Type* float_ty = builder.getFloatTy();
    llvm::Type* i32_ty = builder.getInt32Ty();

    switch (token.type) {
    case TokenType::CONSTANT_CLIP_WIDTH: {
        const auto& payload = std::get<TokenPayload_ClipDim>(token.payload);
        const VSVideoInfo* vinfo = vi[payload.clip_idx];
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(vinfo->width), float_ty));
        return true;
    }
    case TokenType::CONSTANT_CLIP_HEIGHT: {
        const auto& payload = std::get<TokenPayload_ClipDim>(token.payload);
        const VSVideoInfo* vinfo = vi[payload.clip_idx];
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(vinfo->height), float_ty));
        return true;
    }
    case TokenType::CONSTANT_CLIP_PLANE_WIDTH: {
        const auto& payload =
            std::get<TokenPayload_ClipPlaneDim>(token.payload);
        const VSVideoInfo* vinfo = vi[payload.clip_idx];
        int plane_w = vinfo->width;
        if (vinfo->format.colorFamily == cfYUV && payload.plane_idx > 0) {
            plane_w >>= vinfo->format.subSamplingW;
        }
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(plane_w), float_ty));
        return true;
    }
    case TokenType::CONSTANT_CLIP_PLANE_HEIGHT: {
        const auto& payload =
            std::get<TokenPayload_ClipPlaneDim>(token.payload);
        const VSVideoInfo* vinfo = vi[payload.clip_idx];
        int plane_h = vinfo->height;
        if (vinfo->format.colorFamily == cfYUV && payload.plane_idx > 0) {
            plane_h >>= vinfo->format.subSamplingH;
        }
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(plane_h), float_ty));
        return true;
    }
    case TokenType::CONSTANT_PLANE_WIDTH: {
        const auto& payload = std::get<TokenPayload_PlaneDim>(token.payload);
        int plane_w = vo->width; // NOLINT(cppcoreguidelines-init-variables)
        if (vo->format.colorFamily == cfYUV && payload.plane_idx > 0) {
            plane_w >>= vo->format.subSamplingW;
        }
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(plane_w), float_ty));
        return true;
    }
    case TokenType::CONSTANT_PLANE_HEIGHT: {
        const auto& payload = std::get<TokenPayload_PlaneDim>(token.payload);
        int plane_h = vo->height; // NOLINT(cppcoreguidelines-init-variables)
        if (vo->format.colorFamily == cfYUV && payload.plane_idx > 0) {
            plane_h >>= vo->format.subSamplingH;
        }
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(plane_h), float_ty));
        return true;
    }
    case TokenType::CLIP_ABS_PLANE: {
        const auto& payload =
            std::get<TokenPayload_ClipAccessPlane>(token.payload);
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

        rpn_stack.push_back(generate_pixel_load_plane(
            payload.clip_idx, payload.plane_idx, coord_x, coord_y));
        return true;
    }
    case TokenType::STORE_ABS_PLANE: {
        const auto& payload =
            std::get<TokenPayload_StoreAbsPlane>(token.payload);
        llvm::Value* coord_y_f = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* coord_x_f = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* val_to_store = rpn_stack.back();
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

        generate_pixel_store_plane(val_to_store, payload.plane_idx, coord_x,
                                   coord_y);
        return true;
    }
    case TokenType::PROP_STORE: {
        const auto& payload = std::get<TokenPayload_PropStore>(token.payload);
        if (payload.type == PropWriteType::DELETE) {
            // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
            llvm::APInt payload_bits(32, 0x7FC0DE1E); // PROP_DELETE_NAN_PAYLOAD
            llvm::APFloat nan_payload_apf(llvm::APFloat::IEEEsingle(),
                                          payload_bits);
            llvm::Value* nan_with_payload =
                llvm::ConstantFP::get(context, nan_payload_apf);
            builder.CreateStore(nan_with_payload,
                                prop_allocas.at(payload.prop_name));
        } else {
            llvm::Value* val_to_store = rpn_stack.back();
            rpn_stack.pop_back();
            builder.CreateStore(val_to_store,
                                prop_allocas.at(payload.prop_name));
        }
        return true;
    }
    case TokenType::PROP_ACCESS: {
        const auto& payload = std::get<TokenPayload_PropAccess>(token.payload);
        if (payload.clip_idx == 0 &&
            output_prop_map.contains(payload.prop_name)) {
            rpn_stack.push_back(builder.CreateLoad(
                float_ty, prop_allocas.at(payload.prop_name)));
        } else {
            const std::string unique_prop_name =
                std::format("prop_{}_{}", payload.clip_idx, payload.prop_name);
            rpn_stack.push_back(builder.CreateLoad(
                float_ty, prop_allocas.at(unique_prop_name)));
        }
        return true;
    }

    case TokenType::PROP_EXISTS: {
        const auto& payload = std::get<TokenPayload_PropAccess>(token.payload);
        llvm::Value* prop_val = nullptr;
        if (payload.clip_idx == 0 &&
            output_prop_map.contains(payload.prop_name)) {
            prop_val = builder.CreateLoad(
                float_ty, prop_allocas.at(payload.prop_name));
        } else {
            const std::string unique_prop_name =
                std::format("prop_{}_{}", payload.clip_idx, payload.prop_name);
            if (!prop_allocas.contains(unique_prop_name)) {
                rpn_stack.push_back(llvm::ConstantFP::get(float_ty, 0.0));
                return true;
            }
            prop_val = builder.CreateLoad(
                float_ty, prop_allocas.at(unique_prop_name));
        }

        llvm::Value* prop_val_int = builder.CreateBitCast(prop_val, i32_ty);

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
        llvm::Value* read_nan_payload = builder.getInt32(0x7FC0BEEF);
        llvm::Value* is_read_nan =
            builder.CreateICmpEQ(prop_val_int, read_nan_payload);

        // NOLINTNEXTLINE(cppcoreguidelines-avoid-magic-numbers)
        llvm::Value* delete_nan_payload = builder.getInt32(0x7FC0DE1E);
        llvm::Value* is_delete_nan =
            builder.CreateICmpEQ(prop_val_int, delete_nan_payload);

        llvm::Value* does_not_exist =
            builder.CreateOr(is_read_nan, is_delete_nan);

        llvm::Value* exists_val =
            builder.CreateSelect(does_not_exist,
                                 llvm::ConstantFP::get(float_ty, 0.0),
                                 llvm::ConstantFP::get(float_ty, 1.0));
        rpn_stack.push_back(exists_val);
        return true;
    }

    // Array
    case TokenType::ARRAY_ALLOC_STATIC: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        llvm::Value* size_val = builder.getInt64(payload.static_size);
        llvm::Value* name_str =
            builder.CreateGlobalString(payload.name, payload.name + "_name");
        llvm::Value* buffer_ptr = builder.CreateCall(
            llvmexpr_ensure_buffer_func, {name_str, size_val});
        array_ptr_cache[payload.name] = buffer_ptr;
        return true;
    }

    case TokenType::ARRAY_ALLOC_DYN: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        llvm::Value* size_f = rpn_stack.back();
        rpn_stack.pop_back();

        llvm::Value* size_val =
            builder.CreateFPToSI(size_f, builder.getInt64Ty());
        llvm::Value* name_str =
            builder.CreateGlobalString(payload.name, payload.name + "_name");
        llvm::Value* buffer_ptr = builder.CreateCall(
            llvmexpr_ensure_buffer_func, {name_str, size_val});
        array_ptr_cache[payload.name] = buffer_ptr;
        return true;
    }

    case TokenType::ARRAY_LOAD: {
        const auto& payload = std::get<TokenPayload_ArrayOp>(token.payload);
        llvm::Value* idx_f = rpn_stack.back();
        rpn_stack.pop_back();
        llvm::Value* idx = builder.CreateFPToSI(idx_f, i32_ty);

        llvm::Value* array_ptr = array_ptr_cache.at(payload.name);
        llvm::Value* elem_ptr = builder.CreateGEP(float_ty, array_ptr, idx);
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

        llvm::Value* array_ptr = array_ptr_cache.at(payload.name);
        llvm::Value* elem_ptr = builder.CreateGEP(float_ty, array_ptr, idx);
        builder.CreateStore(value, elem_ptr);
        return true;
    }

    default:
        return false;
    }
}

void SingleExprIRGenerator::finalize_and_store_result(
    [[maybe_unused]] llvm::Value* result_val, [[maybe_unused]] llvm::Value* x,
    [[maybe_unused]] llvm::Value* y) {
    // No-op for SingleExpr, all stores are explicit
}
