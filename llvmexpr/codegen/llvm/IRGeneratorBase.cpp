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

#include "IRGeneratorBase.hpp"
#include "../Sorting.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <map>
#include <numbers>
#include <numeric>
#include <unordered_map>

#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/TargetParser/Host.h"

constexpr unsigned ALIGNMENT = 32; // Vapoursynth should guarantee this

IRGeneratorBase::IRGeneratorBase(
    const std::vector<Token>& tokens_in, const VSVideoInfo* out_vi,
    const std::vector<const VSVideoInfo*>& in_vi, int width_in, int height_in,
    bool mirror, const std::map<std::pair<int, std::string>, int>& p_map,
    const analysis::ExpressionAnalysisResults& analysis_results_in,
    llvm::LLVMContext& context_ref, llvm::Module& module_ref,
    llvm::IRBuilder<>& builder_ref, MathLibraryManager& math_mgr,
    std::string func_name_in, int approx_math_in)
    : tokens(tokens_in), vo(out_vi), vi(in_vi),
      num_inputs(static_cast<int>(in_vi.size())), width(width_in),
      height(height_in), mirror_boundary(mirror), prop_map(p_map),
      analysis_results(analysis_results_in), func_name(std::move(func_name_in)),
      approx_math(approx_math_in), context(context_ref), module(module_ref),
      builder(builder_ref), math_manager(math_mgr), func(nullptr),
      rwptrs_arg(nullptr), strides_arg(nullptr), props_arg(nullptr),
      alias_scope_domain(nullptr) {}

void IRGeneratorBase::generate() {
    defineFunctionSignature();
    generateLoops();
}

llvm::AllocaInst*
IRGeneratorBase::createAllocaInEntry(llvm::Type* type,
                                     const std::string& name) {
    llvm::IRBuilder<> entry_builder(&func->getEntryBlock(),
                                    func->getEntryBlock().begin());
    return entry_builder.CreateAlloca(type, nullptr, name);
}

void IRGeneratorBase::assumeAligned(llvm::Value* ptr_value,
                                    unsigned alignment) {
    llvm::Function* assume_fn = llvm::Intrinsic::getOrInsertDeclaration(
        &module, llvm::Intrinsic::assume);
    llvm::Value* cond = builder.getInt1(true);
    llvm::SmallVector<llvm::Value*, 2> args;
    args.push_back(ptr_value);
    args.push_back(builder.getInt64(static_cast<uint64_t>(alignment)));
    llvm::OperandBundleDefT<llvm::Value*> align_bundle("align", args);
    builder.CreateCall(assume_fn, {cond}, {align_bundle});
}

llvm::Value* IRGeneratorBase::getFinalCoord(llvm::Value* coord,
                                            llvm::Value* max_dim,
                                            bool use_mirror) {
    llvm::Value* zero = builder.getInt32(0);
    llvm::Value* one = builder.getInt32(1);

    llvm::Value* result = nullptr;
    if (use_mirror) {
        auto* period = builder.CreateMul(max_dim, builder.getInt32(2));

        auto* modulo_coord = builder.CreateSRem(coord, period);

        auto* is_negative = builder.CreateICmpSLT(modulo_coord, zero);
        auto* adjusted_modulo = builder.CreateAdd(modulo_coord, period);
        modulo_coord =
            builder.CreateSelect(is_negative, adjusted_modulo, modulo_coord);

        auto* in_first_half = builder.CreateICmpSLT(modulo_coord, max_dim);
        auto* period_minus_1 = builder.CreateSub(period, one);
        auto* mirrored_coord = builder.CreateSub(period_minus_1, modulo_coord);

        result =
            builder.CreateSelect(in_first_half, modulo_coord, mirrored_coord);
    } else { // Clamping
        // clamp(coord, 0, max_dim - 1)
        auto* dim_minus_1 = builder.CreateSub(max_dim, one);

        llvm::Function* smax_func = llvm::Intrinsic::getOrInsertDeclaration(
            &module, llvm::Intrinsic::smax, {builder.getInt32Ty()});
        llvm::Function* smin_func = llvm::Intrinsic::getOrInsertDeclaration(
            &module, llvm::Intrinsic::smin, {builder.getInt32Ty()});

        auto* clamped_at_zero = builder.CreateCall(smax_func, {coord, zero});
        result = builder.CreateCall(smin_func, {clamped_at_zero, dim_minus_1});
    }

    return result;
}

llvm::Value* IRGeneratorBase::generateLoadFromRowPtr(llvm::Value* row_ptr,
                                                     int clip_idx,
                                                     llvm::Value* x, int rel_x,
                                                     bool use_mirror,
                                                     bool no_x_bounds_check) {
    const VSVideoInfo* vinfo = vi[clip_idx];
    llvm::Value* coord_x = builder.CreateAdd(x, builder.getInt32(rel_x));
    llvm::Value* final_x = nullptr;
    if (no_x_bounds_check) {
        final_x = coord_x;
    } else {
        final_x = getFinalCoord(coord_x, builder.getInt32(width), use_mirror);
    }

    const VSVideoFormat& format = vinfo->format;
    int bpp = format.bytesPerSample;
    int vs_clip_idx = clip_idx + 1;

    llvm::Value* x_offset = builder.CreateMul(final_x, builder.getInt32(bpp));
    llvm::Value* pixel_addr =
        builder.CreateGEP(builder.getInt8Ty(), row_ptr, x_offset);

    unsigned pixel_align = std::gcd(ALIGNMENT, bpp);
    assumeAligned(pixel_addr, pixel_align);

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
        setMemoryInstAttrs(li, pixel_align, vs_clip_idx);
        loaded_val = builder.CreateZExtOrBitCast(li, builder.getInt32Ty());
        return builder.CreateUIToFP(loaded_val, builder.getFloatTy());
    }
    // stFloat
    if (bpp == 4) {
        llvm::LoadInst* li =
            builder.CreateLoad(builder.getFloatTy(), pixel_addr);
        setMemoryInstAttrs(li, pixel_align, vs_clip_idx);
        return li;
    }
    if (bpp == 2) {
        llvm::LoadInst* li =
            builder.CreateLoad(builder.getHalfTy(), pixel_addr);
        setMemoryInstAttrs(li, pixel_align, vs_clip_idx);
        return builder.CreateFPExt(li, builder.getFloatTy());
    }
    throw std::runtime_error("Unsupported float sample size.");
}

void IRGeneratorBase::addLoopMetadata(
    llvm::BranchInst* loop_br) { // NOLINT(readability-non-const-parameter)
    llvm::StringMap<bool> host_features = llvm::sys::getHostCPUFeatures();
    unsigned simd_width = 4;
    if (!host_features.empty()) {
        if (host_features["avx512f"]) {
            simd_width = 16; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
        } else if (host_features["avx2"]) {
            simd_width = 8; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
        }
    }

    auto create_md_node = [this](const char* name, llvm::Type* type,
                                 uint64_t value) -> llvm::MDNode* {
        std::array<llvm::Metadata*, 2> md = {
            llvm::MDString::get(context, name),
            llvm::ConstantAsMetadata::get(llvm::ConstantInt::get(type, value))};
        return llvm::MDNode::get(context, md);
    };

    llvm::MDNode* vec_width_node =
        create_md_node("llvm.loop.vectorize.width",
                       llvm::Type::getInt32Ty(context), simd_width);

    llvm::MDNode* enable_vec_node = create_md_node(
        "llvm.loop.vectorize.enable", llvm::Type::getInt1Ty(context), 1);

    llvm::MDNode* interleave_node = create_md_node(
        "llvm.loop.interleave.count", llvm::Type::getInt32Ty(context), 4);

    llvm::SmallVector<llvm::Metadata*,
                      5> // NOLINT(cppcoreguidelines-avoid-magic-numbers)
        loop_md_elems;
    loop_md_elems.push_back(nullptr); // to be replaced with self reference
    loop_md_elems.push_back(enable_vec_node);
    loop_md_elems.push_back(vec_width_node);
    loop_md_elems.push_back(interleave_node);
    llvm::MDNode* loop_id = llvm::MDNode::getDistinct(context, loop_md_elems);
    loop_id->replaceOperandWith(0, loop_id);

    loop_br->setMetadata(llvm::LLVMContext::MD_loop, loop_id);
}

llvm::Value* IRGeneratorBase::generatePixelLoad(int clip_idx, llvm::Value* x,
                                                llvm::Value* y, bool mirror) {
    llvm::Value* final_x = getFinalCoord(x, builder.getInt32(width), mirror);
    llvm::Value* final_y = getFinalCoord(y, builder.getInt32(height), mirror);

    int vs_clip_idx = clip_idx + 1;
    llvm::Value* base_ptr = preloaded_base_ptrs[vs_clip_idx];
    llvm::Value* stride = preloaded_strides[vs_clip_idx];

    llvm::Value* y_offset = builder.CreateMul(final_y, stride);
    llvm::Value* row_ptr =
        builder.CreateGEP(builder.getInt8Ty(), base_ptr, y_offset);

    return generateLoadFromRowPtr(row_ptr, clip_idx, final_x, 0, mirror, true);
}

void IRGeneratorBase::generatePixelStore(llvm::Value* value_to_store,
                                         llvm::Value* x, llvm::Value* y) {
    const VSVideoFormat& format = vo->format;
    int bpp = format.bytesPerSample;
    constexpr int DST_IDX = 0;

    llvm::Value* base_ptr = preloaded_base_ptrs[DST_IDX];
    llvm::Value* stride = preloaded_strides[DST_IDX];

    llvm::Value* y_offset = builder.CreateMul(y, stride);
    llvm::Value* x_offset = builder.CreateMul(x, builder.getInt32(bpp));
    llvm::Value* total_offset = builder.CreateAdd(y_offset, x_offset);
    llvm::Value* pixel_addr =
        builder.CreateGEP(builder.getInt8Ty(), base_ptr, total_offset);

    unsigned pixel_align = std::gcd(ALIGNMENT, bpp);
    assumeAligned(pixel_addr, pixel_align);

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
        llvm::StoreInst* si = builder.CreateStore(final_val, pixel_addr);
        setMemoryInstAttrs(si, pixel_align, DST_IDX);
    } else {
        if (bpp == 4) {
            llvm::StoreInst* si =
                builder.CreateStore(value_to_store, pixel_addr);
            setMemoryInstAttrs(si, pixel_align, DST_IDX);
        } else if (bpp == 2) {
            llvm::Value* truncated_val =
                builder.CreateFPTrunc(value_to_store, builder.getHalfTy());
            llvm::StoreInst* si =
                builder.CreateStore(truncated_val, pixel_addr);
            setMemoryInstAttrs(si, pixel_align, DST_IDX);
        } else {
            throw std::runtime_error("Unsupported float sample size.");
        }
    }
}

bool IRGeneratorBase::processCommonToken(const Token& token,
                                         std::vector<llvm::Value*>& rpn_stack,
                                         llvm::Type* float_ty,
                                         llvm::Type* i32_ty,
                                         bool use_approx_math) {
    auto apply_stack_op = [&]<size_t ARITY>(auto&& op) {
        std::array<llvm::Value*, ARITY> args{};
        for (size_t i = ARITY; i > 0; --i) {
            args.at(i - 1) = rpn_stack.back();
            rpn_stack.pop_back();
        }
        rpn_stack.push_back(std::apply(op, args));
    };

    auto apply_intrinsic = [&]<size_t ARITY>(llvm::Intrinsic::ID id) {
        apply_stack_op.operator()<ARITY>(
            [&](auto... args) { return createIntrinsicCall(id, args...); });
    };

    auto apply_binary_op = [&](auto op_callable) {
        apply_stack_op.operator()<2>(
            [&](auto a, auto b) { return op_callable(a, b); });
    };

    auto apply_binary_cmp = [&](llvm::CmpInst::Predicate pred) {
        apply_stack_op.operator()<2>([&](auto a, auto b) {
            auto cmp = builder.CreateFCmp(pred, a, b);
            return builder.CreateSelect(cmp,
                                        llvm::ConstantFP::get(float_ty, 1.0),
                                        llvm::ConstantFP::get(float_ty, 0.0));
        });
    };

    auto apply_logical_op = [&](auto op) {
        apply_stack_op.operator()<2>([&](auto a_val, auto b_val) {
            auto a_bool = builder.CreateFCmpOGT(
                a_val, llvm::ConstantFP::get(float_ty, 0.0));
            auto b_bool = builder.CreateFCmpOGT(
                b_val, llvm::ConstantFP::get(float_ty, 0.0));
            auto logic_res = op(a_bool, b_bool);
            return builder.CreateSelect(logic_res,
                                        llvm::ConstantFP::get(float_ty, 1.0),
                                        llvm::ConstantFP::get(float_ty, 0.0));
        });
    };

    auto apply_bitwise_op = [&](auto op) {
        apply_stack_op.operator()<2>([&](auto a, auto b) {
            auto a_rounded = createIntrinsicCall(llvm::Intrinsic::nearbyint, a);
            auto b_rounded = createIntrinsicCall(llvm::Intrinsic::nearbyint, b);
            auto ai = builder.CreateFPToSI(a_rounded, i32_ty);
            auto bi = builder.CreateFPToSI(b_rounded, i32_ty);
            auto resi = op(ai, bi);
            return builder.CreateSIToFP(resi, float_ty);
        });
    };

    auto apply_approx_math_op =
        [&]<size_t ARITY>(MathOp math_op, llvm::Intrinsic::ID intrinsic_id) {
            static_assert(ARITY == 1 || ARITY == 2,
                          "Only unary or binary operations supported");

            std::array<llvm::Value*, ARITY> args{};
            for (size_t i = 0; i < ARITY; ++i) {
                args.at(ARITY - 1 - i) = rpn_stack.back();
                rpn_stack.pop_back();
            }

            if (use_approx_math) {
                auto* callee = math_manager.getFunction(math_op);
                llvm::SmallVector<llvm::Value*, 2> call_args(args.begin(),
                                                             args.end());
                auto* call = builder.CreateCall(callee, call_args);
                call->setFastMathFlags(builder.getFastMathFlags());
                rpn_stack.push_back(call);
            } else {
                rpn_stack.push_back(std::apply(
                    [&](auto... args) {
                        return createIntrinsicCall(intrinsic_id, args...);
                    },
                    args));
            }
        };

    switch (token.type) {
    case TokenType::Number: {
        const auto& payload = std::get<TokenPayloadNumber>(token.payload);
        rpn_stack.push_back(llvm::ConstantFP::get(float_ty, payload.value));
        return true;
    }
    case TokenType::ConstantWidth:
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(width), float_ty));
        return true;
    case TokenType::ConstantHeight:
        rpn_stack.push_back(
            builder.CreateSIToFP(builder.getInt32(height), float_ty));
        return true;
    case TokenType::ConstantN:
        rpn_stack.push_back(builder.CreateLoad(
            float_ty,
            builder.CreateGEP(float_ty, props_arg, builder.getInt32(0))));
        return true;
    case TokenType::ConstantPi:
        rpn_stack.push_back(llvm::ConstantFP::get(float_ty, std::numbers::pi));
        return true;

    // Binary Operators
    case TokenType::Add:
        apply_binary_op([&](llvm::Value* a, llvm::Value* b) {
            return builder.CreateFAdd(a, b);
        });
        return true;
    case TokenType::Sub:
        apply_binary_op([&](llvm::Value* a, llvm::Value* b) {
            return builder.CreateFSub(a, b);
        });
        return true;
    case TokenType::Mul:
        apply_binary_op([&](llvm::Value* a, llvm::Value* b) {
            return builder.CreateFMul(a, b);
        });
        return true;
    case TokenType::Div:
        apply_binary_op([&](llvm::Value* a, llvm::Value* b) {
            return builder.CreateFDiv(a, b);
        });
        return true;
    case TokenType::Mod:
        apply_binary_op([&](llvm::Value* a, llvm::Value* b) {
            return builder.CreateFRem(a, b);
        });
        return true;
    case TokenType::Pow:
        apply_intrinsic.operator()<2>(llvm::Intrinsic::pow);
        return true;
    case TokenType::Atan2:
        apply_approx_math_op.operator()<2>(MathOp::Atan2,
                                           llvm::Intrinsic::atan2);
        return true;
    case TokenType::Copysign:
        apply_intrinsic.operator()<2>(llvm::Intrinsic::copysign);
        return true;
    case TokenType::Min:
        apply_intrinsic.operator()<2>(llvm::Intrinsic::minnum);
        return true;
    case TokenType::Max:
        apply_intrinsic.operator()<2>(llvm::Intrinsic::maxnum);
        return true;

    // Binary comparisons
    case TokenType::Gt:
        apply_binary_cmp(llvm::CmpInst::FCMP_OGT);
        return true;
    case TokenType::Lt:
        apply_binary_cmp(llvm::CmpInst::FCMP_OLT);
        return true;
    case TokenType::Ge:
        apply_binary_cmp(llvm::CmpInst::FCMP_OGE);
        return true;
    case TokenType::Le:
        apply_binary_cmp(llvm::CmpInst::FCMP_OLE);
        return true;
    case TokenType::Eq:
        apply_binary_cmp(llvm::CmpInst::FCMP_OEQ);
        return true;

    // Logical ops
    case TokenType::And:
        apply_logical_op(
            [&](auto a, auto b) { return builder.CreateAnd(a, b); });
        return true;
    case TokenType::Or:
        apply_logical_op(
            [&](auto a, auto b) { return builder.CreateOr(a, b); });
        return true;
    case TokenType::Xor:
        apply_logical_op(
            [&](auto a, auto b) { return builder.CreateXor(a, b); });
        return true;

    // Bitwise ops
    case TokenType::Bitand:
        apply_bitwise_op(
            [&](auto a, auto b) { return builder.CreateAnd(a, b); });
        return true;
    case TokenType::Bitor:
        apply_bitwise_op(
            [&](auto a, auto b) { return builder.CreateOr(a, b); });
        return true;
    case TokenType::Bitxor:
        apply_bitwise_op(
            [&](auto a, auto b) { return builder.CreateXor(a, b); });
        return true;

    // Unary Operators
    case TokenType::Sqrt: {
        auto* a = rpn_stack.back();
        rpn_stack.pop_back();
        auto* zero = llvm::ConstantFP::get(float_ty, 0.0);
        auto* max_val = createIntrinsicCall(llvm::Intrinsic::maxnum, a, zero);
        rpn_stack.push_back(
            createIntrinsicCall(llvm::Intrinsic::sqrt, max_val));
        return true;
    }
    case TokenType::Exp:
        apply_approx_math_op.operator()<1>(MathOp::Exp, llvm::Intrinsic::exp);
        return true;
    case TokenType::Log:
        apply_approx_math_op.operator()<1>(MathOp::Log, llvm::Intrinsic::log);
        return true;
    case TokenType::Abs:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::fabs);
        return true;
    case TokenType::Floor:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::floor);
        return true;
    case TokenType::Ceil:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::ceil);
        return true;
    case TokenType::Trunc:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::trunc);
        return true;
    case TokenType::Round:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::round);
        return true;
    case TokenType::Sin:
        apply_approx_math_op.operator()<1>(MathOp::Sin, llvm::Intrinsic::sin);
        return true;
    case TokenType::Cos:
        apply_approx_math_op.operator()<1>(MathOp::Cos, llvm::Intrinsic::cos);
        return true;
    case TokenType::Tan:
        apply_approx_math_op.operator()<1>(MathOp::Tan, llvm::Intrinsic::tan);
        return true;
    case TokenType::Asin:
        apply_approx_math_op.operator()<1>(MathOp::Asin, llvm::Intrinsic::asin);
        return true;
    case TokenType::Acos:
        apply_approx_math_op.operator()<1>(MathOp::Acos, llvm::Intrinsic::acos);
        return true;
    case TokenType::Atan:
        apply_approx_math_op.operator()<1>(MathOp::Atan, llvm::Intrinsic::atan);
        return true;
    case TokenType::Exp2:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::exp2);
        return true;
    case TokenType::Log10:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::log10);
        return true;
    case TokenType::Log2:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::log2);
        return true;
    case TokenType::Sinh:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::sinh);
        return true;
    case TokenType::Cosh:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::cosh);
        return true;
    case TokenType::Tanh:
        apply_intrinsic.operator()<1>(llvm::Intrinsic::tanh);
        return true;
    case TokenType::Sgn: {
        auto* x = rpn_stack.back();
        rpn_stack.pop_back();
        auto* zero = llvm::ConstantFP::get(float_ty, 0.0);
        auto* one = llvm::ConstantFP::get(float_ty, 1.0);
        auto* nonzero = builder.CreateFCmpONE(x, zero);
        auto* sign = builder.CreateCall(
            llvm::Intrinsic::getOrInsertDeclaration(
                &module, llvm::Intrinsic::copysign, {float_ty}),
            {one, x});
        rpn_stack.push_back(builder.CreateSelect(nonzero, sign, zero));
        return true;
    }
    case TokenType::Neg: {
        auto* a = rpn_stack.back();
        rpn_stack.pop_back();
        rpn_stack.push_back(builder.CreateFNeg(a));
        return true;
    }
    case TokenType::Not: {
        auto* a = rpn_stack.back();
        rpn_stack.pop_back();
        rpn_stack.push_back(builder.CreateSelect(
            builder.CreateFCmpOLE(a, llvm::ConstantFP::get(float_ty, 0.0)),
            llvm::ConstantFP::get(float_ty, 1.0),
            llvm::ConstantFP::get(float_ty, 0.0)));
        return true;
    }
    case TokenType::Bitnot: {
        auto* a = rpn_stack.back();
        rpn_stack.pop_back();
        auto* a_rounded = createIntrinsicCall(llvm::Intrinsic::nearbyint, a);
        rpn_stack.push_back(builder.CreateSIToFP(
            builder.CreateNot(builder.CreateFPToSI(a_rounded, i32_ty)),
            float_ty));
        return true;
    }

    // Ternary and other multi-arg
    case TokenType::Ternary: {
        auto* c = rpn_stack.back();
        rpn_stack.pop_back();
        auto* b = rpn_stack.back();
        rpn_stack.pop_back();
        auto* a = rpn_stack.back();
        rpn_stack.pop_back();
        rpn_stack.push_back(builder.CreateSelect(
            builder.CreateFCmpOGT(a, llvm::ConstantFP::get(float_ty, 0.0)), b,
            c));
        return true;
    }
    case TokenType::Clip:
    case TokenType::Clamp: {
        auto* max_val = rpn_stack.back();
        rpn_stack.pop_back();
        auto* min_val = rpn_stack.back();
        rpn_stack.pop_back();
        auto* val = rpn_stack.back();
        rpn_stack.pop_back();
        auto* temp = createIntrinsicCall(llvm::Intrinsic::maxnum, val, min_val);
        auto* clamped =
            createIntrinsicCall(llvm::Intrinsic::minnum, temp, max_val);
        rpn_stack.push_back(clamped);
        return true;
    }
    case TokenType::Fma: {
        auto* c = rpn_stack.back();
        rpn_stack.pop_back();
        auto* b = rpn_stack.back();
        rpn_stack.pop_back();
        auto* a = rpn_stack.back();
        rpn_stack.pop_back();
        rpn_stack.push_back(builder.CreateCall(
            llvm::Intrinsic::getOrInsertDeclaration(
                &module, llvm::Intrinsic::fma, {builder.getFloatTy()}),
            {a, b, c}));
        return true;
    }

    // Stack manipulation
    case TokenType::Dup: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        rpn_stack.push_back(rpn_stack[rpn_stack.size() - 1 - payload.n]);
        return true;
    }
    case TokenType::Drop: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        if (payload.n > 0) {
            rpn_stack.resize(rpn_stack.size() - payload.n);
        }
        return true;
    }
    case TokenType::Swap: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        std::swap(rpn_stack.back(),
                  rpn_stack[rpn_stack.size() - 1 - payload.n]);
        return true;
    }
    case TokenType::SortN: {
        const auto& payload = std::get<TokenPayloadStackOp>(token.payload);
        int n = payload.n;
        if (n < 2) {
            return true;
        }

        std::vector<llvm::Value*> values;
        values.reserve(n);
        for (int k = 0; k < n; ++k) {
            values.push_back(rpn_stack.back());
            rpn_stack.pop_back();
        }

        auto compare_swap = [&](int i_idx, int j_idx) {
            llvm::Value* val_i = values[i_idx];
            llvm::Value* val_j = values[j_idx];
            llvm::Value* cond = builder.CreateFCmpOGT(val_i, val_j);
            values[i_idx] = builder.CreateSelect(cond, val_j, val_i); // min
            values[j_idx] = builder.CreateSelect(cond, val_i, val_j); // max
        };

        auto network = get_sorting_network(n);
        for (const auto& pair : network) {
            compare_swap(pair.first, pair.second);
        }

        for (int k = n - 1; k >= 0; --k) {
            rpn_stack.push_back(values[k]);
        }
        return true;
    }

    // Control Flow (no-op during this pass)
    case TokenType::LabelDef:
    case TokenType::Jump:
        return true;

    default:
        // Not a common token - let derived class handle it
        return false;
    }
}

void IRGeneratorBase::generateIRFromTokens(llvm::Value* x, llvm::Value* y,
                                           llvm::Value* x_fp, llvm::Value* y_fp,
                                           bool no_x_bounds_check) {
    llvm::Type* float_ty = builder.getFloatTy();
    llvm::Type* i32_ty = builder.getInt32Ty();
    llvm::Function* parent_func = builder.GetInsertBlock()->getParent();

    bool use_approx_math = false;
    if (approx_math == 1) {
        use_approx_math = true;
    } else if (approx_math == 2) {
        // In auto mode, always try approx math first
        use_approx_math = true;
    }

    if (tokens.empty()) {
        generatePixelStore(llvm::ConstantFP::get(float_ty, 0.0), x, y);
        return;
    }

    std::unordered_map<std::string, llvm::Value*> named_vars;
    const auto& all_vars = analysis_results.getVariableUsageResult().all_vars;

    for (const std::string& var_name : all_vars) {
        named_vars[var_name] = createAllocaInEntry(float_ty, var_name);
    }

    std::map<int, llvm::BasicBlock*> llvm_blocks;
    const auto& cfg_blocks = analysis_results.getCFGBlocks();
    const auto& label_to_block_idx = analysis_results.getLabelToBlockIdx();
    const auto& stack_depth_in = analysis_results.getStackDepthIn();

    for (int i = 0; i < static_cast<int>(cfg_blocks.size()); ++i) {
        std::string name = std::format("b{}", i);
        for (const auto& [label_name, block_idx] : label_to_block_idx) {
            if (block_idx == i) {
                name = label_name;
                break;
            }
        }
        llvm_blocks[i] = llvm::BasicBlock::Create(context, name, parent_func);
    }
    llvm::BasicBlock* exit_bb =
        llvm::BasicBlock::Create(context, "exit", parent_func);

    // Branch from current block to the first CFG block
    builder.CreateBr(llvm_blocks[0]);

    // Initial PHI generation for merge blocks
    std::map<int, std::vector<llvm::Value*>> block_initial_stacks;
    for (int i = 0; i < static_cast<int>(cfg_blocks.size()); ++i) {
        if (cfg_blocks[i].predecessors.size() > 1) {
            builder.SetInsertPoint(llvm_blocks[i]);
            std::vector<llvm::Value*> initial_stack;
            int depth = stack_depth_in[i];
            initial_stack.reserve(depth);
            for (int j = 0; j < depth; ++j) {
                initial_stack.push_back(builder.CreatePHI(
                    float_ty, cfg_blocks[i].predecessors.size()));
            }
            block_initial_stacks[i] = initial_stack;
        }
    }

    // Process blocks
    std::map<int, std::vector<llvm::Value*>> block_final_stacks;

    for (int i = 0; i < static_cast<int>(cfg_blocks.size()); ++i) {
        const auto& block_info = cfg_blocks[i];
        builder.SetInsertPoint(llvm_blocks[i]);

        std::vector<llvm::Value*> rpn_stack;
        if (block_info.predecessors.empty()) {
            // Entry block, empty stack
        } else if (block_info.predecessors.size() == 1) {
            int pred_idx = block_info.predecessors[0];
            if (block_final_stacks.contains(pred_idx)) {
                rpn_stack = block_final_stacks.at(pred_idx);
            }
        } else {
            rpn_stack = block_initial_stacks.at(i);
        }

        for (int j = block_info.start_token_idx; j < block_info.end_token_idx;
             ++j) {
            const auto& token = tokens[j];

            // Try common tokens first
            if (processCommonToken(token, rpn_stack, float_ty, i32_ty,
                                   use_approx_math)) {
                continue;
            }

            // Variables
            if (token.type == TokenType::VarStore) {
                const auto& payload = std::get<TokenPayloadVar>(token.payload);
                llvm::Value* val_to_store = rpn_stack.back();
                rpn_stack.pop_back();
                llvm::Value* var_ptr = named_vars[payload.name];
                builder.CreateStore(val_to_store, var_ptr);
                continue;
            }
            if (token.type == TokenType::VarLoad) {
                const auto& payload = std::get<TokenPayloadVar>(token.payload);
                llvm::Value* var_ptr = named_vars[payload.name];
                rpn_stack.push_back(builder.CreateLoad(float_ty, var_ptr));
                continue;
            }

            // Special tokens - delegate to derived class
            if (!processModeSpecificToken(token, rpn_stack, x, y, x_fp, y_fp,
                                          no_x_bounds_check)) {
                throw std::runtime_error(std::format(
                    "Unhandled token type: {}", static_cast<int>(token.type)));
            }
        }

        // Create Terminator
        if (block_info.successors.empty()) {
            builder.CreateBr(exit_bb);
        } else if (block_info.successors.size() == 1) {
            builder.CreateBr(llvm_blocks[block_info.successors[0]]);
        } else { // size is 2, from a JUMP
            llvm::Value* cond_val = rpn_stack.back();
            llvm::Value* cond = builder.CreateFCmpOGT(
                cond_val, llvm::ConstantFP::get(float_ty, 0.0));
            builder.CreateCondBr(cond, llvm_blocks[block_info.successors[0]],
                                 llvm_blocks[block_info.successors[1]]);
            rpn_stack.pop_back();
        }

        block_final_stacks[i] = rpn_stack;
    }

    // Populate PHI nodes
    for (int i = 0; i < static_cast<int>(cfg_blocks.size()); ++i) {
        if (cfg_blocks[i].predecessors.size() > 1) {
            auto& phis = block_initial_stacks.at(i);
            for (int pred_idx : cfg_blocks[i].predecessors) {
                auto& incoming_stack = block_final_stacks.at(pred_idx);
                auto* incoming_block = llvm_blocks.at(pred_idx);
                for (size_t j = 0; j < phis.size(); ++j) {
                    if (j < incoming_stack.size()) {
                        llvm::cast<llvm::PHINode>(phis[j])->addIncoming(
                            incoming_stack[j], incoming_block);
                    }
                }
            }
        }
    }

    // Final Result PHI
    builder.SetInsertPoint(exit_bb);
    std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> final_values;
    for (int i = 0; i < static_cast<int>(cfg_blocks.size()); ++i) {
        if (cfg_blocks[i].successors.empty()) {
            auto& stack = block_final_stacks.at(i);
            if (!stack.empty()) {
                final_values.emplace_back(stack.back(), llvm_blocks.at(i));
            }
        }
    }

    llvm::Value* result_val = nullptr;
    if (final_values.empty()) {
        result_val = llvm::UndefValue::get(float_ty);
    } else if (final_values.size() == 1) {
        result_val = final_values[0].first;
    } else {
        llvm::PHINode* phi =
            builder.CreatePHI(float_ty, final_values.size(), "result_phi");
        for (const auto& pair : final_values) {
            phi->addIncoming(pair.first, pair.second);
        }
        result_val = phi;
    }

    // Let derived class handle exit logic (if any) and final store
    finalizeAndStoreResult(result_val, x, y);
}
