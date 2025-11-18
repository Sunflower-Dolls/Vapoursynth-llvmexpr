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

#include "Compiler.hpp"

#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "llvm/IR/Attributes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include "../ir/ExprIRGenerator.hpp"
#include "../ir/SingleExprIRGenerator.hpp"
#include "../utils/Diagnostics.hpp"

Compiler::Compiler(
    std::vector<Token> tokens_in, const VSVideoInfo* out_vi,
    const std::vector<const VSVideoInfo*>& in_vi, int width_in, int height_in,
    bool mirror, std::string dump_path,
    const std::map<std::pair<int, std::string>, int>& p_map,
    std::string function_name, int opt_level_in, int approx_math_in,
    const analysis::ExpressionAnalysisResults& analysis_results_in,
    ExprMode mode, const std::vector<std::string>& output_props)
    : tokens(std::move(tokens_in)), vo(out_vi), vi(in_vi),
      num_inputs(static_cast<int>(in_vi.size())), width(width_in),
      height(height_in), mirror_boundary(mirror),
      dump_ir_path(std::move(dump_path)), prop_map(p_map),
      func_name(std::move(function_name)), opt_level(opt_level_in),
      approx_math(approx_math_in), expr_mode(mode), output_props(output_props),
      analysis_results(analysis_results_in) {}

CompiledFunction Compiler::compile() {
    if (approx_math == 2) {
        return compile_with_approx_math(1);
    }
    return compile_with_approx_math(approx_math);
}

CompiledFunction Compiler::compile_with_approx_math(int actual_approx_math) {
    bool needs_nans = false;
    if (expr_mode == ExprMode::EXPR) {
        needs_nans = std::ranges::any_of(tokens, [](const auto& token) {
            return token.type == TokenType::EXIT_NO_WRITE;
        });
    } else if (expr_mode == ExprMode::SINGLE_EXPR) {
        needs_nans = std::ranges::any_of(tokens, [](const auto& token) {
            return token.type == TokenType::PROP_STORE;
        });
    }

    OrcJit& jit = needs_nans ? global_jit_nan_safe : global_jit_fast;

    VectorizationDiagnosticHandler diagnostic_handler;
    diagnostic_handler.reset();

    // Create LLVM context and module
    auto context = std::make_unique<llvm::LLVMContext>();
    context->setDiagnosticHandlerCallBack(
        VectorizationDiagnosticHandler::diagnosticHandlerCallback,
        &diagnostic_handler);

    auto module = std::make_unique<llvm::Module>("ExprJITModule", *context);
    module->setDataLayout(jit.getDataLayout());

    // Set up fast math flags
    llvm::IRBuilder<> builder(*context);
    llvm::FastMathFlags FMF;
    FMF.setFast();
    FMF.setNoNaNs(!needs_nans);
    builder.setFastMathFlags(FMF);

    // Create math library manager
    MathLibraryManager math_manager(module.get(), *context);

    // Create IR generator and generate code
    std::unique_ptr<IRGeneratorBase> ir_gen;
    if (expr_mode == ExprMode::EXPR) {
        ir_gen = std::make_unique<ExprIRGenerator>(
            tokens, vo, vi, width, height, mirror_boundary, prop_map,
            analysis_results, *context, *module, builder, math_manager,
            func_name, actual_approx_math);
    } else {
        ir_gen = std::make_unique<SingleExprIRGenerator>(
            tokens, vo, vi, mirror_boundary, prop_map, output_props,
            analysis_results, *context, *module, builder, math_manager,
            func_name, actual_approx_math);
    }
    ir_gen->generate();

    // Get the generated function and set attributes
    llvm::Function* func = module->getFunction(func_name);
    if (func == nullptr) {
        throw std::runtime_error("Failed to find generated function");
    }

    llvm::AttrBuilder FuncAttrs(func->getContext());
    if (FMF.allowContract()) {
        FuncAttrs.addAttribute("fp-contract", "fast");
    }
    if (FMF.approxFunc()) {
        FuncAttrs.addAttribute("approx-func-fp-math", "true");
    }
    if (FMF.noInfs()) {
        FuncAttrs.addAttribute("no-infs-fp-math", "true");
    }
    if (FMF.noNaNs()) {
        FuncAttrs.addAttribute("no-nans-fp-math", "true");
    }
    if (FMF.noSignedZeros()) {
        FuncAttrs.addAttribute("no-signed-zeros-fp-math", "true");
    }
    if (FMF.allowReciprocal()) {
        FuncAttrs.addAttribute("allow-reciprocal-fp-math", "true");
    }
#ifdef _WIN32
    // Fix for missing ___chkstk_ms symbol
    FuncAttrs.addAttribute("no-stack-arg-probe", "true");
#endif
    FuncAttrs.addAttribute(llvm::Attribute::NoUnwind);
    FuncAttrs.addAttribute(llvm::Attribute::WillReturn);
    func->addFnAttrs(FuncAttrs);

    // Verify module before optimization
    if (llvm::verifyModule(*module, &llvm::errs())) {
        module->print(llvm::errs(), nullptr);
        throw std::runtime_error("LLVM module verification failed (pre-opt).");
    }

    // Dump pre-optimization IR if requested
    std::string plane_specific_dump_path;
    if (!dump_ir_path.empty()) {
        plane_specific_dump_path = dump_ir_path;
        size_t dot_pos = plane_specific_dump_path.rfind('.');
        if (dot_pos != std::string::npos) {
            plane_specific_dump_path.insert(dot_pos, "." + func_name);
        } else {
            plane_specific_dump_path += "." + func_name;
        }

        std::error_code EC;
        std::string pre_path = plane_specific_dump_path + ".pre.ll";
        llvm::raw_fd_ostream dest_pre(pre_path, EC, llvm::sys::fs::OF_None);
        if (!EC) {
            module->print(dest_pre, nullptr);
            dest_pre.flush();
        }
    }

    // Run optimization passes
    {
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;

        llvm::PassBuilder PB;
        PB.registerModuleAnalyses(MAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        llvm::ModulePassManager MPM;
        std::string pipeline;
        if (opt_level > 0) {
            pipeline = "default<O3>";
            for (int i = 1; i < opt_level; ++i) {
                pipeline += ",default<O3>";
            }
        }
        if (auto Err = PB.parsePassPipeline(MPM, pipeline)) {
            llvm::errs() << "Failed to parse '" << pipeline
                         << "' pipeline: " << llvm::toString(std::move(Err))
                         << "\n";
            throw std::runtime_error(
                "Failed to create default optimization pipeline.");
        }
        MPM.run(*module, MAM);
    }

    // Verify module after optimization
    if (llvm::verifyModule(*module, &llvm::errs())) {
        module->print(llvm::errs(), nullptr);
        throw std::runtime_error("LLVM module verification failed.");
    }

    // Dump post-optimization IR if requested
    if (!plane_specific_dump_path.empty()) {
        std::error_code EC;
        llvm::raw_fd_ostream dest(plane_specific_dump_path, EC,
                                  llvm::sys::fs::OF_None);
        if (EC) {
            throw std::runtime_error("Could not open file: " + EC.message() +
                                     " for writing IR to " +
                                     plane_specific_dump_path);
        }
        module->print(dest, nullptr);
        dest.flush();
    }

    // Handle vectorization fallback
    if (diagnostic_handler.hasVectorizationFailed() && approx_math == 2 &&
        actual_approx_math == 1) {
        Compiler fallback_compiler(std::vector<Token>(tokens), vo, vi, width,
                                   height, mirror_boundary, dump_ir_path,
                                   prop_map, func_name, opt_level, approx_math,
                                   analysis_results, expr_mode, output_props);
        return fallback_compiler.compile_with_approx_math(0);
    }

    // Add module to JIT and get function address
    jit.addModule(std::move(module), std::move(context));
    void* func_addr = jit.getFunctionAddress(func_name);

    if (func_addr == nullptr) {
        throw std::runtime_error("Failed to get JIT'd function address.");
    }

    CompiledFunction compiled;
    compiled.func_ptr =
        reinterpret_cast< // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            ProcessProc>(func_addr);
    return compiled;
}
