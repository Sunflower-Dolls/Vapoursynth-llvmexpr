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

#include <algorithm>
#include <cctype>
#include <cstdlib>
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
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassTimingInfo.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include "../../codegen/llvm/Diagnostics.hpp"
#include "../../codegen/llvm/ExprIRGenerator.hpp"
#include "../../codegen/llvm/SingleExprIRGenerator.hpp"

namespace {

bool is_env_var_enabled(const char* value) {
    if (value == nullptr || *value == '\0') {
        return false;
    }

    std::string normalized;
    for (const char* ptr = value; *ptr != '\0'; ++ptr) {
        const auto ch = static_cast<unsigned char>(*ptr);
        if (std::isspace(ch) != 0) {
            continue;
        }
        normalized.push_back(static_cast<char>(std::tolower(ch)));
    }

    if (normalized.empty()) {
        return false;
    }

    return normalized != "0" && normalized != "false" && normalized != "off" &&
           normalized != "no";
}

bool is_time_passes_enabled() {
    return is_env_var_enabled(std::getenv("LLVMEXPR_TIME_PASSES"));
}

} // namespace

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
        return compileWithApproxMath(1);
    }
    return compileWithApproxMath(approx_math);
}

CompiledFunction Compiler::compileWithApproxMath(int actual_approx_math) {
    bool needs_nans = false;
    if (expr_mode == ExprMode::Expr) {
        needs_nans = std::ranges::any_of(tokens, [](const auto& token) {
            return token.type == TokenType::ExitNoWrite ||
                   token.type == TokenType::PropExists;
        });
    } else if (expr_mode == ExprMode::SingleExpr) {
        needs_nans = std::ranges::any_of(tokens, [](const auto& token) {
            return token.type == TokenType::PropStore ||
                   token.type == TokenType::PropExists;
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
    llvm::FastMathFlags fmf;
    fmf.setFast();
    fmf.setNoNaNs(!needs_nans);
    builder.setFastMathFlags(fmf);

    // Create math library manager
    MathLibraryManager math_manager(module.get(), *context);

    // Create IR generator and generate code
    std::unique_ptr<IRGeneratorBase> ir_gen;
    if (expr_mode == ExprMode::Expr) {
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

    llvm::AttrBuilder func_attrs(func->getContext());
    if (fmf.allowContract()) {
        func_attrs.addAttribute("fp-contract", "fast");
    }
    if (fmf.approxFunc()) {
        func_attrs.addAttribute("approx-func-fp-math", "true");
    }
    if (fmf.noInfs()) {
        func_attrs.addAttribute("no-infs-fp-math", "true");
    }
    if (fmf.noNaNs()) {
        func_attrs.addAttribute("no-nans-fp-math", "true");
    }
    if (fmf.noSignedZeros()) {
        func_attrs.addAttribute("no-signed-zeros-fp-math", "true");
    }
    if (fmf.allowReciprocal()) {
        func_attrs.addAttribute("allow-reciprocal-fp-math", "true");
    }
#ifdef _WIN32
    // Fix for missing ___chkstk_ms symbol
    func_attrs.addAttribute("no-stack-arg-probe", "true");
#endif
    func_attrs.addAttribute(llvm::Attribute::NoUnwind);
    func_attrs.addAttribute(llvm::Attribute::WillReturn);
    func->addFnAttrs(func_attrs);

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

        std::error_code ec;
        std::string pre_path = plane_specific_dump_path + ".pre.ll";
        llvm::raw_fd_ostream dest_pre(pre_path, ec, llvm::sys::fs::OF_None);
        if (!ec) {
            module->print(dest_pre, nullptr);
            dest_pre.flush();
        }
    }

    // Run optimization passes
    {
        llvm::LoopAnalysisManager lam;
        llvm::FunctionAnalysisManager fam;
        llvm::CGSCCAnalysisManager cgam;
        llvm::ModuleAnalysisManager mam;

        const bool time_passes_enabled = is_time_passes_enabled();
        llvm::PassInstrumentationCallbacks pic;
        llvm::TimePassesHandler time_passes_handler(time_passes_enabled);

        if (time_passes_enabled) {
            time_passes_handler.registerCallbacks(pic);
        }

        llvm::PassBuilder pb(nullptr, llvm::PipelineTuningOptions(), {}, &pic);
        pb.registerModuleAnalyses(mam);
        pb.registerFunctionAnalyses(fam);
        pb.registerCGSCCAnalyses(cgam);
        pb.registerLoopAnalyses(lam);
        pb.crossRegisterProxies(lam, fam, cgam, mam);

        llvm::ModulePassManager mpm;
        std::string pipeline;
        if (opt_level > 0) {
            pipeline = "default<O3>";
            for (int i = 1; i < opt_level; ++i) {
                pipeline += ",default<O3>";
            }
        }
        if (auto err = pb.parsePassPipeline(mpm, pipeline)) {
            llvm::errs() << "Failed to parse '" << pipeline
                         << "' pipeline: " << llvm::toString(std::move(err))
                         << "\n";
            throw std::runtime_error(
                "Failed to create default optimization pipeline.");
        }
        mpm.run(*module, mam);

        if (time_passes_enabled) {
            time_passes_handler.print();
        }
    }

    // Verify module after optimization
    if (llvm::verifyModule(*module, &llvm::errs())) {
        module->print(llvm::errs(), nullptr);
        throw std::runtime_error("LLVM module verification failed.");
    }

    // Dump post-optimization IR if requested
    if (!plane_specific_dump_path.empty()) {
        std::error_code ec;
        llvm::raw_fd_ostream dest(plane_specific_dump_path, ec,
                                  llvm::sys::fs::OF_None);
        if (ec) {
            throw std::runtime_error("Could not open file: " + ec.message() +
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
        return fallback_compiler.compileWithApproxMath(0);
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
