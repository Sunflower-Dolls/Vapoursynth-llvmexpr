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

#ifndef LLVMEXPR_UTILS_DIAGNOSTICS_HPP
#define LLVMEXPR_UTILS_DIAGNOSTICS_HPP

#include <atomic>

#include "llvm/IR/DiagnosticHandler.h"
#include "llvm/IR/DiagnosticInfo.h"

class VectorizationDiagnosticHandler {
  public:
    VectorizationDiagnosticHandler();

    void
    setOriginalHandler(llvm::DiagnosticHandler::DiagnosticHandlerTy handler,
                       void* context);

    void handleDiagnostic(const llvm::DiagnosticInfo& DI);

    [[nodiscard]] bool hasVectorizationFailed() const;

    void reset();

    static void diagnosticHandlerCallback(const llvm::DiagnosticInfo* DI,
                                          void* Context);

  private:
    std::atomic<bool> vectorization_failed;
    llvm::DiagnosticHandler::DiagnosticHandlerTy original_handler;
    void* original_context;
};

#endif // LLVMEXPR_UTILS_DIAGNOSTICS_HPP
