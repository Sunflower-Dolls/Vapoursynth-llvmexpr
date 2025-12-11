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
 *
 * The implementation of fast math functions is partially borrowed from vapoursynth
 * which is licensed under the GNU Lesser General Public License v2.1.
 *
 *
 * Copyright (c) 2013-2020 Fredrik Mellbin
 *
 * This file is part of VapourSynth.
 *
 * VapourSynth is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * VapourSynth is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with VapourSynth; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * The `atan` implementation is from a Stack Overflow answer by user njuffa
 * (https://stackoverflow.com/a/23097989), licensed under CC BY-SA 4.0.
 *
 * The `acos` implementation is from https://forwardscattering.org/post/66.
 * NOTE: No license was specified on the source website.
 */

#ifndef LLVMEXPR_UTILS_MATH_HPP
#define LLVMEXPR_UTILS_MATH_HPP

#include <format>
#include <functional>
#include <map>
#include <numbers>
#include <string>
#include <tuple>
#include <utility>

#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Verifier.h"

enum class MathOp : std::uint8_t {
    Exp,
    Log,
    Sin,
    Cos,
    Tan,
    Atan,
    Atan2,
    Acos,
    Asin
};

struct MathOpInfo {
    int arity;
    const char* name;
};

constexpr MathOpInfo getMathOpInfo(MathOp op) {
    switch (op) {
    case MathOp::Exp:
        return {.arity = 1, .name = "fast_exp"};
    case MathOp::Log:
        return {.arity = 1, .name = "fast_log"};
    case MathOp::Sin:
        return {.arity = 1, .name = "fast_sin"};
    case MathOp::Cos:
        return {.arity = 1, .name = "fast_cos"};
    case MathOp::Tan:
        return {.arity = 1, .name = "fast_tan"};
    case MathOp::Atan:
        return {.arity = 1, .name = "fast_atan"};
    case MathOp::Atan2:
        return {.arity = 2, .name = "fast_atan2"};
    case MathOp::Acos:
        return {.arity = 1, .name = "fast_acos"};
    case MathOp::Asin:
        return {.arity = 1, .name = "fast_asin"};
    }
}

#ifdef __x86_64__
using SupportedVectorWidths =
    std::integer_sequence<int, 4,
                          8,   // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                          16>; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
#elif defined(__ARM_NEON__)
using SupportedVectorWidths =
    std::integer_sequence<int,
                          4>; // NOLINT(cppcoreguidelines-avoid-magic-numbers)
#else
using SupportedVectorWidths = std::integer_sequence<int>;
#endif

template <int VectorWidth> class MathFunctionGenerator {
  public:
    MathFunctionGenerator(llvm::Module* module, llvm::LLVMContext& context)
        : module(module), context(context), builder(context) {}

    template <MathOp op> llvm::Function* getOrCreate();

  private:
    template <int, MathOp> friend struct MathFunctionImpl;

    llvm::Module* module;
    llvm::LLVMContext& context;
    llvm::IRBuilder<> builder;

    llvm::Type* getFloatType() {
        auto* ty = llvm::Type::getFloatTy(context);
        if (VectorWidth == 1) {
            return ty;
        }
        return llvm::VectorType::get(ty, VectorWidth, false);
    }

    llvm::Type* getInt32Type() {
        auto* ty = llvm::Type::getInt32Ty(context);
        if (VectorWidth == 1) {
            return ty;
        }
        return llvm::VectorType::get(ty, VectorWidth, false);
    }

    llvm::Value* getConstant(double val) {
        auto* scalarConst =
            llvm::ConstantFP::get(llvm::Type::getFloatTy(context), val);
        return (VectorWidth == 1)
                   ? (llvm::Value*)scalarConst
                   : builder.CreateVectorSplat(VectorWidth, scalarConst);
    }

    llvm::Value* getInt32Constant(int32_t val) {
        auto* scalarConst =
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(context), val);
        return (VectorWidth == 1)
                   ? (llvm::Value*)scalarConst
                   : builder.CreateVectorSplat(VectorWidth, scalarConst);
    }

    std::string getFunctionName(const std::string& base_name) {
        if (VectorWidth == 1) {
            return base_name;
        }
        return base_name + "_v" + std::to_string(VectorWidth);
    }

    llvm::Value* createIntrinsicCall(llvm::Intrinsic::ID intrinsic_id,
                                     llvm::ArrayRef<llvm::Value*> args) {
        auto* intrinsic = llvm::Intrinsic::getOrInsertDeclaration(
            module, intrinsic_id, getFloatType());
        return builder.CreateCall(intrinsic, args);
    }

    llvm::Function* createFunction(
        const std::string& base_name, int arity,
        const std::function<llvm::Value*(llvm::ArrayRef<llvm::Value*>)>&
            body_generator) {
        std::string func_name = getFunctionName(base_name);
        if (auto* existing_func = module->getFunction(func_name)) {
            return existing_func;
        }

        auto last_ip = builder.saveIP();

        auto* float_ty = getFloatType();
        std::vector<llvm::Type*> arg_types(arity, float_ty);
        auto* func_ty = llvm::FunctionType::get(float_ty, arg_types, false);
        auto* func = llvm::Function::Create(
            func_ty, llvm::Function::ExternalLinkage, func_name, module);

        auto* entry_bb = llvm::BasicBlock::Create(context, "entry", func);
        builder.SetInsertPoint(entry_bb);

        std::vector<llvm::Value*> args;
        std::ranges::transform(func->args(), std::back_inserter(args),
                               [](auto& arg) { return &arg; });
        if (arity > 0) {
            args[0]->setName("x");
            if (arity > 1) {
                args[1]->setName("y");
            }
            if (arity > 2) {
                args[2]->setName("z");
            }
        }

        llvm::Value* result = body_generator(args);

        builder.CreateRet(result);
        builder.restoreIP(last_ip);

        if (llvm::verifyFunction(*func, &llvm::errs())) {
            func->eraseFromParent();
            return nullptr;
        }

        return func;
    }
};

template <int VectorWidth, MathOp op> struct MathFunctionImpl;

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Exp> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Exp);
        // https://github.com/vapoursynth/vapoursynth/blob/2a3d3657320ca505c784b98f10e7cd9649d6169a/src/core/expr/jitcompiler_x86.cpp#L635
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* x = args[0];
                // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
                auto* exp_hi = gen->getConstant(88.3762626647949F);
                auto* exp_lo = gen->getConstant(-88.3762626647949F);
                auto* log2e = gen->getConstant(std::numbers::log2e_v<float>);
                auto* exp_p0 = gen->getConstant(1.9875691500E-4F);
                auto* exp_p1 = gen->getConstant(1.3981999507E-3F);
                auto* exp_p2 = gen->getConstant(8.3334519073E-3F);
                auto* exp_p3 = gen->getConstant(4.1665795894E-2F);
                auto* exp_p4 = gen->getConstant(1.6666665459E-1F);
                auto* exp_p5 = gen->getConstant(5.0000001201E-1F);
                auto* half = gen->getConstant(0.5F);
                auto* one = gen->getConstant(1.0F);
                auto* neg_exp_c1 = gen->getConstant(-0.693359375F);
                auto* neg_exp_c2 = gen->getConstant(2.12194440e-4F);
                auto* const_0x7f = gen->getInt32Constant(0x7F);
                auto* const_23 = gen->getInt32Constant(23);
                // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
                x = gen->createIntrinsicCall(llvm::Intrinsic::minnum,
                                             {x, exp_hi});
                x = gen->createIntrinsicCall(llvm::Intrinsic::maxnum,
                                             {x, exp_lo});
                auto* fx = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                                    {log2e, x, half});
                auto* etmp =
                    gen->createIntrinsicCall(llvm::Intrinsic::nearbyint, {fx});
                auto* cmp_gt = gen->builder.CreateFCmpOGT(etmp, fx);
                auto* ext_cmp =
                    gen->builder.CreateSExt(cmp_gt, gen->getInt32Type());
                auto* one_int =
                    gen->builder.CreateBitCast(one, gen->getInt32Type());
                auto* mask_int = gen->builder.CreateAnd(ext_cmp, one_int);
                auto* mask =
                    gen->builder.CreateBitCast(mask_int, gen->getFloatType());
                fx = gen->builder.CreateFSub(etmp, mask);
                x = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {fx, neg_exp_c1, x});
                x = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {fx, neg_exp_c2, x});
                auto* z = gen->builder.CreateFMul(x, x);
                llvm::Value* y = exp_p0;
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, exp_p1});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, exp_p2});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, exp_p3});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, exp_p4});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, exp_p5});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma, {y, z, x});
                y = gen->builder.CreateFAdd(y, one);
                auto* emm0_float =
                    gen->createIntrinsicCall(llvm::Intrinsic::nearbyint, {fx});
                auto* emm0 =
                    gen->builder.CreateFPToSI(emm0_float, gen->getInt32Type());
                emm0 = gen->builder.CreateAdd(emm0, const_0x7f);
                emm0 = gen->builder.CreateShl(emm0, const_23);
                auto* emm0_as_float =
                    gen->builder.CreateBitCast(emm0, gen->getFloatType());
                x = gen->builder.CreateFMul(y, emm0_as_float);
                return x;
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Log> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Log);
        // https://github.com/vapoursynth/vapoursynth/blob/2a3d3657320ca505c784b98f10e7cd9649d6169a/src/core/expr/jitcompiler_x86.cpp#L671
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* x = args[0];
                // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
                auto* min_norm_pos = gen->getInt32Constant(0x00800000);
                auto* inv_mant_mask = gen->getInt32Constant(~0x7F800000);
                auto* sqrt_1_2 = gen->getConstant(0.707106781186547524F);
                auto* log_p0 = gen->getConstant(7.0376836292E-2F);
                auto* log_p1 = gen->getConstant(-1.1514610310E-1F);
                auto* log_p2 = gen->getConstant(1.1676998740E-1F);
                auto* log_p3 = gen->getConstant(-1.2420140846E-1F);
                auto* log_p4 = gen->getConstant(1.4249322787E-1F);
                auto* log_p5 = gen->getConstant(-1.6668057665E-1F);
                auto* log_p6 = gen->getConstant(2.0000714765E-1F);
                auto* log_p7 = gen->getConstant(-2.4999993993E-1F);
                auto* log_p8 = gen->getConstant(3.3333331174E-1F);
                auto* log_q2 = gen->getConstant(0.693359375F);
                auto* log_q1 = gen->getConstant(-2.12194440e-4F);
                auto* one = gen->getConstant(1.0F);
                auto* neg_half = gen->getConstant(-0.5F);
                auto* const_0x7f = gen->getInt32Constant(0x7F);
                auto* const_23 = gen->getInt32Constant(23);
                // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
                auto* is_one = gen->builder.CreateFCmpOEQ(x, one);
                auto* min_norm_pos_float = gen->builder.CreateBitCast(
                    min_norm_pos, gen->getFloatType());
                x = gen->createIntrinsicCall(llvm::Intrinsic::maxnum,
                                             {x, min_norm_pos_float});
                auto* x_as_int =
                    gen->builder.CreateBitCast(x, gen->getInt32Type());
                auto* emm0i = gen->builder.CreateLShr(x_as_int, const_23);
                auto* x_masked =
                    gen->builder.CreateAnd(x_as_int, inv_mant_mask);
                auto* half_as_int = gen->builder.CreateBitCast(
                    gen->getConstant(
                        0.5F), // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                    gen->getInt32Type()); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                x_masked = gen->builder.CreateOr(x_masked, half_as_int);
                x = gen->builder.CreateBitCast(x_masked, gen->getFloatType());
                emm0i = gen->builder.CreateSub(emm0i, const_0x7f);
                auto* emm0 =
                    gen->builder.CreateSIToFP(emm0i, gen->getFloatType());
                emm0 = gen->builder.CreateFAdd(emm0, one);
                auto* mask = gen->builder.CreateFCmpOLT(x, sqrt_1_2);
                auto* ext_mask =
                    gen->builder.CreateSExt(mask, gen->getInt32Type());
                x_as_int = gen->builder.CreateBitCast(x, gen->getInt32Type());
                auto* etmp_as_int = gen->builder.CreateAnd(ext_mask, x_as_int);
                auto* etmp = gen->builder.CreateBitCast(etmp_as_int,
                                                        gen->getFloatType());
                x = gen->builder.CreateFSub(x, one);
                auto* one_as_int =
                    gen->builder.CreateBitCast(one, gen->getInt32Type());
                auto* maskf_as_int =
                    gen->builder.CreateAnd(ext_mask, one_as_int);
                auto* maskf = gen->builder.CreateBitCast(maskf_as_int,
                                                         gen->getFloatType());
                emm0 = gen->builder.CreateFSub(emm0, maskf);
                x = gen->builder.CreateFAdd(x, etmp);
                auto* z = gen->builder.CreateFMul(x, x);
                llvm::Value* y = log_p0;
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p1});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p2});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p3});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p4});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p5});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p6});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p7});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {y, x, log_p8});
                y = gen->builder.CreateFMul(y, x);
                y = gen->builder.CreateFMul(y, z);
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {emm0, log_q1, y});
                y = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {z, neg_half, y});
                x = gen->builder.CreateFAdd(x, y);
                x = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {emm0, log_q2, x});
                x_as_int = gen->builder.CreateBitCast(x, gen->getInt32Type());
                auto* ext_is_one =
                    gen->builder.CreateSExt(is_one, gen->getInt32Type());
                auto* not_ext_is_one = gen->builder.CreateNot(ext_is_one);
                auto* result_as_int =
                    gen->builder.CreateAnd(not_ext_is_one, x_as_int);
                x = gen->builder.CreateBitCast(result_as_int,
                                               gen->getFloatType());
                return x;
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Sin> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Sin);
        // https://github.com/vapoursynth/vapoursynth/blob/2a3d3657320ca505c784b98f10e7cd9649d6169a/src/core/expr/jitcompiler_x86.cpp#L813
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* x = args[0];
                auto* float_ty = gen->getFloatType();
                auto* int32_ty = gen->getInt32Type();
                auto* float_invpi =
                    gen->getConstant(std::numbers::inv_pi_v<float>);
                // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
                auto* float_pi1 = gen->getConstant(3.140625F);
                auto* float_pi2 = gen->getConstant(0.0009670257568359375F);
                auto* float_pi3 = gen->getConstant(1.984187252998352e-07F);
                auto* float_pi4 = gen->getConstant(1.273533813134432e-11F);
                auto* float_sinC3 = gen->getConstant(-0.1666666567325592F);
                auto* float_sinC5 = gen->getConstant(0.00833307858556509F);
                auto* float_sinC7 = gen->getConstant(-0.00019807418575510383F);
                auto* float_sinC9 = gen->getConstant(2.6019030363451748e-06F);
                auto* signmask = gen->getInt32Constant(0x80000000);
                // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
                llvm::Value* sign = gen->builder.CreateBitCast(x, int32_ty);
                sign = gen->builder.CreateAnd(sign, signmask);
                llvm::Value* t1 =
                    gen->createIntrinsicCall(llvm::Intrinsic::fabs, {x});
                llvm::Value* t2 = gen->builder.CreateFMul(t1, float_invpi);
                llvm::Value* t2_rounded =
                    gen->createIntrinsicCall(llvm::Intrinsic::nearbyint, {t2});
                llvm::Value* t2i =
                    gen->builder.CreateFPToSI(t2_rounded, int32_ty);
                llvm::Value* t4 = gen->builder.CreateShl(
                    t2i, 31); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                sign = gen->builder.CreateXor(sign, t4);
                t2 = gen->builder.CreateSIToFP(t2i, float_ty);
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi1), t1});
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi2), t1});
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi3), t1});
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi4), t1});
                t2 = gen->builder.CreateFMul(t1, t1);
                llvm::Value* t3 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma, {t2, float_sinC9, float_sinC7});
                t3 = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                              {t3, t2, float_sinC5});
                t3 = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                              {t3, t2, float_sinC3});
                t3 = gen->builder.CreateFMul(t3, t2);
                t3 = gen->builder.CreateFMul(t3, t1);
                t1 = gen->builder.CreateFAdd(t1, t3);
                llvm::Value* t1_as_int =
                    gen->builder.CreateBitCast(t1, int32_ty);
                llvm::Value* result_as_int =
                    gen->builder.CreateXor(sign, t1_as_int);
                return gen->builder.CreateBitCast(result_as_int, float_ty);
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Cos> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Cos);
        // https://github.com/vapoursynth/vapoursynth/blob/2a3d3657320ca505c784b98f10e7cd9649d6169a/src/core/expr/jitcompiler_x86.cpp#L813
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* x = args[0];
                auto* float_ty = gen->getFloatType();
                auto* int32_ty = gen->getInt32Type();
                auto* float_invpi =
                    gen->getConstant(std::numbers::inv_pi_v<float>);
                // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
                auto* float_pi1 = gen->getConstant(3.140625F);
                auto* float_pi2 = gen->getConstant(0.0009670257568359375F);
                auto* float_pi3 = gen->getConstant(1.984187252998352e-07F);
                auto* float_pi4 = gen->getConstant(1.273533813134432e-11F);
                auto* float_cosC2 = gen->getConstant(-0.4999999701976776F);
                auto* float_cosC4 = gen->getConstant(0.04166652262210846F);
                auto* float_cosC6 = gen->getConstant(-0.001388676579343155F);
                auto* float_cosC8 = gen->getConstant(2.4390448881604243e-05F);
                // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
                auto* one_float = gen->getConstant(1.0F);
                llvm::Value* sign = gen->getInt32Constant(0);
                llvm::Value* t1 =
                    gen->createIntrinsicCall(llvm::Intrinsic::fabs, {x});
                llvm::Value* t2 = gen->builder.CreateFMul(t1, float_invpi);
                llvm::Value* t2_rounded =
                    gen->createIntrinsicCall(llvm::Intrinsic::nearbyint, {t2});
                llvm::Value* t2i =
                    gen->builder.CreateFPToSI(t2_rounded, int32_ty);
                llvm::Value* t4 = gen->builder.CreateShl(
                    t2i, 31); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                sign = gen->builder.CreateXor(sign, t4);
                t2 = gen->builder.CreateSIToFP(t2i, float_ty);
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi1), t1});
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi2), t1});
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi3), t1});
                t1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t2, gen->builder.CreateFNeg(float_pi4), t1});
                t2 = gen->builder.CreateFMul(t1, t1);
                llvm::Value* t3 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma, {t2, float_cosC8, float_cosC6});
                t3 = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                              {t3, t2, float_cosC4});
                t3 = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                              {t3, t2, float_cosC2});
                t1 = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                              {t3, t2, one_float});
                llvm::Value* t1_as_int =
                    gen->builder.CreateBitCast(t1, int32_ty);
                llvm::Value* result_as_int =
                    gen->builder.CreateXor(sign, t1_as_int);
                return gen->builder.CreateBitCast(result_as_int, float_ty);
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Tan> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Tan);
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* x = args[0];
                llvm::Function* sinFunc =
                    MathFunctionImpl<VectorWidth, MathOp::Sin>::generate(gen);
                llvm::Function* cosFunc =
                    MathFunctionImpl<VectorWidth, MathOp::Cos>::generate(gen);
                llvm::Value* sin_x = gen->builder.CreateCall(sinFunc, {x});
                llvm::Value* cos_x = gen->builder.CreateCall(cosFunc, {x});
                return gen->builder.CreateFDiv(sin_x, cos_x);
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Atan> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Atan);
        // https://stackoverflow.com/a/23097989
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* var = args[0];
                // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
                auto* one = gen->getConstant(1.0F);
                auto* pi_div_2 = gen->getConstant(1.5707963267948966F);
                auto* z =
                    gen->createIntrinsicCall(llvm::Intrinsic::fabs, {var});
                auto* z_gt_1 = gen->builder.CreateFCmpOGT(z, one);
                auto* one_div_zz = gen->builder.CreateFDiv(one, z);
                auto* a = gen->builder.CreateSelect(z_gt_1, one_div_zz, z);
                auto* s = gen->builder.CreateFMul(a, a);
                auto* q = gen->builder.CreateFMul(s, s);
                llvm::Value* p = gen->getConstant(-2.0258553044340116e-5F);
                llvm::Value* t = gen->getConstant(2.2302240345710764e-4F);
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, q, gen->getConstant(-1.1640717779912220e-3F)});
                t = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t, q, gen->getConstant(3.8559749383656407e-3F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, q, gen->getConstant(-9.1845592187222193e-3F)});
                t = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t, q, gen->getConstant(1.6978035834594660e-2F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, q, gen->getConstant(-2.5826796814492296e-2F)});
                t = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t, q, gen->getConstant(3.4067811082715810e-2F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, q, gen->getConstant(-4.0926382420509999e-2F)});
                t = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t, q, gen->getConstant(4.6739496199158334e-2F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, q, gen->getConstant(-5.2392330054601366e-2F)});
                t = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t, q, gen->getConstant(5.8773077721790683e-2F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, q, gen->getConstant(-6.6658603633512892e-2F)});
                t = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {t, q, gen->getConstant(7.6922129305867892e-2F)});
                p = gen->createIntrinsicCall(llvm::Intrinsic::fma, {p, s, t});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, s, gen->getConstant(-9.0909012354005267e-2F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, s, gen->getConstant(1.1111110678749421e-1F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, s, gen->getConstant(-1.4285714271334810e-1F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, s, gen->getConstant(1.9999999999755005e-1F)});
                p = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma,
                    {p, s, gen->getConstant(-3.3333333333331838e-1F)});
                // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
                auto* pp_mul_ss = gen->builder.CreateFMul(p, s);
                p = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                             {pp_mul_ss, a, a});
                auto* rr_if_gt_1 = gen->builder.CreateFSub(pi_div_2, p);
                auto* rr = gen->builder.CreateSelect(z_gt_1, rr_if_gt_1, p);
                return gen->createIntrinsicCall(llvm::Intrinsic::copysign,
                                                {rr, var});
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Atan2> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Atan2);
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* var_y = args[0];
                auto* var_x = args[1];
                auto* atan_func =
                    MathFunctionImpl<VectorWidth, MathOp::Atan>::generate(gen);
                auto* zero = gen->getConstant(0.0F);
                auto* pi = gen->getConstant(std::numbers::pi_v<float>);
                auto* pi_div_2 = gen->getConstant(
                    1.5707963267948966F); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                auto* y_div_x = gen->builder.CreateFDiv(var_y, var_x);
                auto* atan_y_div_x =
                    gen->builder.CreateCall(atan_func, {y_div_x});
                auto* res_x_gt_0 = atan_y_div_x;
                auto* signed_pi = gen->createIntrinsicCall(
                    llvm::Intrinsic::copysign, {pi, var_y});
                auto* res_x_lt_0 =
                    gen->builder.CreateFAdd(atan_y_div_x, signed_pi);
                auto* res_x_eq_0 = gen->createIntrinsicCall(
                    llvm::Intrinsic::copysign, {pi_div_2, var_y});
                auto* x_gt_0 = gen->builder.CreateFCmpOGT(var_x, zero);
                auto* x_lt_0 = gen->builder.CreateFCmpOLT(var_x, zero);
                auto* result =
                    gen->builder.CreateSelect(x_gt_0, res_x_gt_0, res_x_lt_0);
                result = gen->builder.CreateSelect(
                    x_lt_0, res_x_lt_0,
                    gen->builder.CreateSelect(x_gt_0, res_x_gt_0, res_x_eq_0));
                auto* x_is_zero = gen->builder.CreateFCmpOEQ(var_x, zero);
                auto* y_is_zero = gen->builder.CreateFCmpOEQ(var_y, zero);
                auto* both_zero = gen->builder.CreateAnd(x_is_zero, y_is_zero);
                result = gen->builder.CreateSelect(both_zero, zero, result);
                return result;
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Acos> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Acos);
        // https://forwardscattering.org/post/66
        // TODO: Switch to another implementation that doesn't has licensing issues.
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* x = args[0];
                auto* pi = gen->getConstant(std::numbers::pi_v<float>);
                auto* ax = gen->createIntrinsicCall(llvm::Intrinsic::fabs, {x});
                // NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
                auto* term1_mul = gen->getConstant(-0.124605335F);
                auto* term1_add = gen->getConstant(0.1570634F);
                auto* term1 = gen->createIntrinsicCall(
                    llvm::Intrinsic::fma, {ax, term1_mul, term1_add});

                auto* term2_sub = gen->getConstant(0.99418175F);
                auto* term2 = gen->builder.CreateFSub(term2_sub, ax);

                auto* poly_part = gen->builder.CreateFMul(term1, term2);

                auto* two = gen->getConstant(2.0F);
                auto* neg_two = gen->getConstant(-2.0F);
                // NOLINTEND(cppcoreguidelines-avoid-magic-numbers)
                auto* sqrt_arg = gen->createIntrinsicCall(llvm::Intrinsic::fma,
                                                          {ax, neg_two, two});
                auto* sqrt_part =
                    gen->createIntrinsicCall(llvm::Intrinsic::sqrt, {sqrt_arg});

                auto* res_pos = gen->builder.CreateFAdd(poly_part, sqrt_part);

                auto* zero = gen->getConstant(0.0F);
                auto* is_neg = gen->builder.CreateFCmpOLT(x, zero);

                auto* res_neg = gen->builder.CreateFSub(pi, res_pos);

                return gen->builder.CreateSelect(is_neg, res_neg, res_pos);
            });
    }
};

template <int VectorWidth> struct MathFunctionImpl<VectorWidth, MathOp::Asin> {
    static llvm::Function* generate(MathFunctionGenerator<VectorWidth>* gen) {
        constexpr auto opInfo = getMathOpInfo(MathOp::Asin);
        // asin(x) = pi/2 - acos(x)
        return gen->createFunction(
            opInfo.name, opInfo.arity,
            [gen](llvm::ArrayRef<llvm::Value*> args) -> llvm::Value* {
                auto* x = args[0];
                auto* pi_div_2 = gen->getConstant(
                    1.5707963267948966F); // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                auto* acos_func =
                    MathFunctionImpl<VectorWidth, MathOp::Acos>::generate(gen);
                auto* acos_x = gen->builder.CreateCall(acos_func, {x});
                return gen->builder.CreateFSub(pi_div_2, acos_x);
            });
    }
};

template <int VectorWidth>
template <MathOp op>
llvm::Function* MathFunctionGenerator<VectorWidth>::getOrCreate() {
    return MathFunctionImpl<VectorWidth, op>::generate(this);
}

using SupportedMathOpsTuple =
    std::tuple<std::integral_constant<MathOp, MathOp::Exp>,
               std::integral_constant<MathOp, MathOp::Log>,
               std::integral_constant<MathOp, MathOp::Sin>,
               std::integral_constant<MathOp, MathOp::Cos>,
               std::integral_constant<MathOp, MathOp::Tan>,
               std::integral_constant<MathOp, MathOp::Atan>,
               std::integral_constant<MathOp, MathOp::Atan2>,
               std::integral_constant<MathOp, MathOp::Acos>,
               std::integral_constant<MathOp, MathOp::Asin>>;

class MathLibraryManager {
  public:
    MathLibraryManager(llvm::Module* module, llvm::LLVMContext& context)
        : module(module), context(context) {}

    llvm::Function* getFunction(MathOp op) {
        if (auto it = funcCache.find(op); it != funcCache.end()) {
            return it->second;
        }
        return generateAndCache(op);
    }

  private:
    llvm::Module* module;
    llvm::LLVMContext& context;
    std::map<MathOp, llvm::Function*> funcCache;

    template <MathOp op, int VectorWidth> llvm::Function* dispatch() {
        MathFunctionGenerator<VectorWidth> generator(module, context);
        return generator.template getOrCreate<op>();
    }

    template <MathOp op> llvm::Function* generateAndCacheImpl() {
        llvm::Function* scalarFunc = dispatch<op, 1>();

        if (!scalarFunc) {
            return nullptr;
        }

        // https://llvm.org/docs/LangRef.html#id1998
        auto link_vectors = [&]<int... vlen>(
                                std::integer_sequence<int, vlen...>) {
            (
                [&] {
                    const llvm::Function* vecFunc = dispatch<op, vlen>();

                    if (vecFunc) {
#if defined(__x86_64__) || defined(__ARM_NEON__)
                        std::string isa;
#ifdef __x86_64__
                        if constexpr (vlen == 4) {
                            isa = "b"; // SSE
                        } else if constexpr (
                            vlen ==
                            8) { // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                            isa = "d"; // AVX2
                        } else if constexpr (
                            vlen ==
                            16) { // NOLINT(cppcoreguidelines-avoid-magic-numbers)
                            isa = "e"; // AVX512
                        }
#elif defined(__ARM_NEON__)
                        if constexpr (vlen == 4) {
                            isa = "n"; // Armv8 Advanced SIMD
                        } else {
                            isa = "s"; // SVE
                        }
#endif
                        constexpr auto opInfo = getMathOpInfo(op);
                        std::string parameters(opInfo.arity, 'v');
                        std::string mask = "N";
                        std::string abi_string =
                            std::format("_ZGV{}{}{}{}_{}({})", isa, mask, vlen,
                                        parameters, scalarFunc->getName().str(),
                                        vecFunc->getName().str());

                        scalarFunc->addFnAttr(llvm::Attribute::get(
                            context, "vector-function-abi-variant",
                            abi_string));
#endif
                    }
                }(),
                ...);
        };

        link_vectors(SupportedVectorWidths{});

        funcCache[op] = scalarFunc;
        return scalarFunc;
    }

    llvm::Function* generateAndCache(MathOp op) {
        llvm::Function* result = nullptr;
        std::apply(
            [&, this](auto... op_constant) {
                auto dispatcher = [&, this](auto op_c) {
                    if (op_c.value == op) {
                        result = this->generateAndCacheImpl<op_c.value>();
                    }
                };
                (dispatcher(op_constant), ...);
            },
            SupportedMathOpsTuple{});
        return result;
    }
};

#endif // LLVMEXPR_UTILS_MATH_HPP
