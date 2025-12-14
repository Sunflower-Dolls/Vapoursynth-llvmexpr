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

#ifndef LLVMEXPR_ANALYSIS_FRAMEWORK_PASS_HPP
#define LLVMEXPR_ANALYSIS_FRAMEWORK_PASS_HPP

#include "../../frontend/Tokenizer.hpp"
#include "PreservedAnalyses.hpp"

#include <vector>

namespace analysis {

// Forward declaration
class AnalysisManager;

class Pass {
  public:
    Pass() = default;
    virtual ~Pass() = default;
    Pass(const Pass&) = delete;
    Pass& operator=(const Pass&) = delete;
    Pass(Pass&&) = delete;
    Pass& operator=(Pass&&) = delete;

    [[nodiscard]] virtual const char* getName() const = 0;
};

template <typename Derived, typename ResultT> class AnalysisPass : public Pass {
  public:
    using Result = ResultT;

    virtual Result run(const std::vector<Token>& tokens,
                       AnalysisManager& am) = 0;
};

class TransformationPass : public Pass {
  public:
    virtual PreservedAnalyses run(std::vector<Token>& tokens,
                                  AnalysisManager& am) = 0;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_FRAMEWORK_PASS_HPP
