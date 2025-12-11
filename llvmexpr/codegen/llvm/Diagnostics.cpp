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

#include "Diagnostics.hpp"

#include <string>

#include "llvm/IR/DiagnosticPrinter.h"
#include "llvm/Support/raw_ostream.h"

VectorizationDiagnosticHandler::VectorizationDiagnosticHandler()
    : vectorization_failed(false), original_handler(nullptr),
      original_context(nullptr) {}

void VectorizationDiagnosticHandler::setOriginalHandler(
    llvm::DiagnosticHandler::DiagnosticHandlerTy handler, void* context) {
    original_handler = handler;
    original_context = context;
}

void VectorizationDiagnosticHandler::handleDiagnostic(
    const llvm::DiagnosticInfo& DI) {
    bool should_suppress = false;

    if (DI.getSeverity() == llvm::DS_Remark ||
        DI.getSeverity() == llvm::DS_Warning) {
        std::string msg;
        llvm::raw_string_ostream stream(msg);
        llvm::DiagnosticPrinterRawOStream printer(stream);
        DI.print(printer);

        if (msg.find("loop not vectorized") != std::string::npos) {
            vectorization_failed.store(true);
            should_suppress = true;
        }
    }

    // Call original handler for all diagnostics except "loop not vectorized"
    if (!should_suppress && (original_handler != nullptr)) {
        original_handler(&DI, original_context);
    }
}

bool VectorizationDiagnosticHandler::hasVectorizationFailed() const {
    return vectorization_failed.load();
}

void VectorizationDiagnosticHandler::reset() {
    vectorization_failed.store(false);
}

void VectorizationDiagnosticHandler::diagnosticHandlerCallback(
    const llvm::DiagnosticInfo* DI, void* Context) {
    static_cast<VectorizationDiagnosticHandler*>(Context)->handleDiagnostic(
        *DI);
}
