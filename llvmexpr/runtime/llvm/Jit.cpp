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

#include "Jit.hpp"

#ifdef _WIN32
#include <cmath>
#endif
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#include <cmath>
#endif

#ifdef _WIN32
#include "llvm/Analysis/TargetLibraryInfo.h"
#endif
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/ThreadSafeModule.h"
#ifdef _WIN32
#include "llvm/IR/IRBuilder.h"
#endif
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LegacyPassManager.h"
#include "llvm/Support/Error.h"
#ifdef _WIN32
#include "llvm/Support/SmallVectorMemoryBuffer.h"
#endif
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetOptions.h"
#include "llvm/TargetParser/Host.h"

// Forward declare the host API functions
extern "C" {
float* llvmexpr_ensure_buffer(const char*, int64_t);
int64_t llvmexpr_get_buffer_size(const char*);
}

#ifdef _WIN32
namespace {
class WindowsLibCallFixupCompiler final
    : public llvm::orc::IRCompileLayer::IRCompiler {
  public:
    explicit WindowsLibCallFixupCompiler(
        llvm::orc::JITTargetMachineBuilder jtmb)
        : IRCompiler(
              llvm::orc::irManglingOptionsFromTargetOptions(jtmb.getOptions())),
          jtmb(std::move(jtmb)) {}

    llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
    operator()(llvm::Module& m) override {
        auto tm = jtmb.createTargetMachine();
        if (!tm) {
            return tm.takeError();
        }

        m.setTargetTriple((*tm)->getTargetTriple());
        m.setDataLayout((*tm)->createDataLayout());

        llvm::SmallVector<char, 0> obj_buffer_sv;
        llvm::raw_svector_ostream obj_stream(obj_buffer_sv);

        llvm::legacy::PassManager pm;
        llvm::TargetLibraryInfoImpl tlii(llvm::Triple(m.getTargetTriple()));
        tlii.setUnavailable(llvm::LibFunc_sincosf);
        tlii.setUnavailable(llvm::LibFunc_sincos);
        pm.add(new llvm::TargetLibraryInfoWrapperPass(tlii));

        if ((*tm)->addPassesToEmitFile(pm, obj_stream, nullptr,
                                       llvm::CodeGenFileType::ObjectFile)) {
            return llvm::createStringError(
                llvm::inconvertibleErrorCode(),
                "TargetMachine can't emit an object file");
        }

        pm.run(m);
        return std::make_unique<llvm::SmallVectorMemoryBuffer>(
            std::move(obj_buffer_sv), "<jit object>");
    }

  private:
    llvm::orc::JITTargetMachineBuilder jtmb;
};

} // namespace
#endif

OrcJit::OrcJit(bool no_nans_fp_math) {
    static struct LLVMInitializer {
        LLVMInitializer() { // NOLINT(modernize-use-equals-default)
            llvm::InitializeNativeTarget();
            llvm::InitializeNativeTargetAsmPrinter();
            llvm::InitializeNativeTargetAsmParser();
        }
        LLVMInitializer(const LLVMInitializer&) = delete;
        LLVMInitializer& operator=(const LLVMInitializer&) = delete;
        LLVMInitializer(LLVMInitializer&&) = delete;
        LLVMInitializer& operator=(LLVMInitializer&&) = delete;
        ~LLVMInitializer() = default;
    } initializer;

    auto jtmb =
        llvm::cantFail(llvm::orc::JITTargetMachineBuilder::detectHost());
    jtmb.setCodeGenOptLevel(llvm::CodeGenOptLevel::Aggressive);

    llvm::StringMap<bool> host_features = llvm::sys::getHostCPUFeatures();
    if (host_features.size() > 0) {
        std::vector<std::string> features;
        for (auto& f : host_features) {
            if (f.getValue()) {
                features.push_back("+" + f.getKey().str());
            }
        }
        jtmb.addFeatures(features);
    }

    llvm::TargetOptions opts;
    opts.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    opts.UnsafeFPMath = true;
    opts.NoInfsFPMath = true;
    opts.NoNaNsFPMath = no_nans_fp_math;
    jtmb.setOptions(opts);

    auto jit_builder = llvm::orc::LLJITBuilder();
    jit_builder.setJITTargetMachineBuilder(std::move(jtmb));
#ifdef _WIN32
    jit_builder.setCompileFunctionCreator(
        [](llvm::orc::JITTargetMachineBuilder jtmb_in)
            -> llvm::Expected<
                std::unique_ptr<llvm::orc::IRCompileLayer::IRCompiler>> {
            return std::make_unique<WindowsLibCallFixupCompiler>(
                std::move(jtmb_in));
        });
#endif
    auto temp_jit = jit_builder.create();
    if (!temp_jit) {
        llvm::errs() << "Failed to create LLJIT instance: "
                     << llvm::toString(temp_jit.takeError()) << "\n";
        throw std::runtime_error("LLJIT creation failed");
    }
    lljit = std::move(*temp_jit);

    // Register Host API symbols for dynamic array management
    auto& main_jd = lljit->getMainJITDylib();
    llvm::orc::SymbolMap symbols;

    symbols[lljit->mangleAndIntern("llvmexpr_ensure_buffer")] =
        llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr(
                llvm::pointerToJITTargetAddress(&llvmexpr_ensure_buffer)),
            llvm::JITSymbolFlags::Callable | llvm::JITSymbolFlags::Exported);

    symbols[lljit->mangleAndIntern("llvmexpr_get_buffer_size")] =
        llvm::orc::ExecutorSymbolDef(
            llvm::orc::ExecutorAddr(
                llvm::pointerToJITTargetAddress(&llvmexpr_get_buffer_size)),
            llvm::JITSymbolFlags::Callable | llvm::JITSymbolFlags::Exported);

    if (auto err = main_jd.define(llvm::orc::absoluteSymbols(symbols))) {
        llvm::errs() << "Failed to define host call symbols: "
                     << llvm::toString(std::move(err)) << "\n";
        throw std::runtime_error("Failed to define host call symbols in JIT");
    }
}

const llvm::DataLayout& OrcJit::getDataLayout() const {
    return lljit->getDataLayout();
}

const llvm::Triple& OrcJit::getTargetTriple() const {
    return lljit->getTargetTriple();
}

void OrcJit::addModule(std::unique_ptr<llvm::Module> m,
                       std::unique_ptr<llvm::LLVMContext> ctx) {
    std::vector<llvm::Function*> functions_to_remove;
    for (auto& f : *m) {
        if (!f.isDeclaration()) {
            std::string func_name = f.getName().str();

            if (!func_name.starts_with("process_plane_")) {
                if (func_name.starts_with("fast_") ||
                    func_name.find("_v4") != std::string::npos ||
                    func_name.find("_v8") != std::string::npos ||
                    func_name.find("_v16") != std::string::npos) {

                    // Try a test lookup to see if symbol exists
                    auto test_sym = lljit->lookup(func_name);
                    if (test_sym) {
                        // Symbol already exists, mark for removal
                        functions_to_remove.push_back(&f);
                        continue;
                    }
                    // If test lookup failed, the symbol doesn't exist, so we can add it
                    llvm::consumeError(test_sym.takeError());
                }
            }
        }
    }

    // Remove duplicate functions from the module
    for (auto* f : functions_to_remove) {
        f->eraseFromParent();
    }

    auto tsm = llvm::orc::ThreadSafeModule(std::move(m), std::move(ctx));
    auto err = lljit->addIRModule(std::move(tsm));
    if (err) {
        llvm::errs() << "Failed to add IR module: "
                     << llvm::toString(std::move(err)) << "\n";
        throw std::runtime_error("Failed to add IR module to JIT");
    }
}

void* OrcJit::getFunctionAddress(const std::string& name) {
    // Try to lookup the symbol
    auto sym = lljit->lookup(name);
    if (!sym) {
        llvm::errs() << "Failed to find symbol '" << name
                     << "': " << llvm::toString(sym.takeError()) << "\n";
        return nullptr;
    }
    return sym->toPtr<void*>();
}

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// Global JIT instances
OrcJit global_jit_fast(true);
OrcJit global_jit_nan_safe(false);

// JIT cache
std::unordered_map<std::string, CompiledFunction> jit_cache;
std::mutex cache_mutex;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)
