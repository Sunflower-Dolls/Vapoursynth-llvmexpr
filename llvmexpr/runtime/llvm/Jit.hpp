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

#ifndef LLVMEXPR_JIT_HPP
#define LLVMEXPR_JIT_HPP

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "llvm/ExecutionEngine/Orc/LLJIT.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/TargetParser/Triple.h"

using ProcessProc = void (*)(void* context, uint8_t** rwptrs,
                             const int* strides, float* props);

struct CompiledFunction {
    ProcessProc func_ptr = nullptr;
};

class OrcJit {
  private:
    std::unique_ptr<llvm::orc::LLJIT> lljit;

  public:
    explicit OrcJit(bool no_nans_fp_math);

    [[nodiscard]] const llvm::DataLayout& getDataLayout() const;

    [[nodiscard]] const llvm::Triple& getTargetTriple() const;

    void addModule(std::unique_ptr<llvm::Module> M,
                   std::unique_ptr<llvm::LLVMContext> Ctx);

    void* getFunctionAddress(const std::string& name);
};

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
// Global JIT instances
extern OrcJit global_jit_fast;
extern OrcJit global_jit_nan_safe;

// JIT cache
extern std::unordered_map<std::string, CompiledFunction> jit_cache;
extern std::mutex cache_mutex;
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

#endif // LLVMEXPR_JIT_HPP
