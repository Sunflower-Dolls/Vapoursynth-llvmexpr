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

#ifndef LLVMEXPR_ANALYSIS_FRAMEWORK_ANALYSIS_MANAGER_HPP
#define LLVMEXPR_ANALYSIS_FRAMEWORK_ANALYSIS_MANAGER_HPP

#include "../../frontend/Tokenizer.hpp"
#include "AnalysisResults.hpp"
#include "Pass.hpp"
#include "PreservedAnalyses.hpp"

#include <vector>

namespace analysis {

class AnalysisManager {
  public:
    AnalysisManager(const std::vector<Token>& tokens_in,
                    bool mirror_boundary_in, int expected_final_depth_in = 1);

    template <typename PassT> typename PassT::Result& getResult() {
        if (!results.template hasResult<PassT>()) {
            PassT pass;
            auto result = pass.run(tokens, *this);
            results.template setResult<PassT>(std::move(result));
        }
        return results.template getResult<PassT>();
    }

    template <typename PassT> const typename PassT::Result& getResult() const {
        return results.template getResult<PassT>();
    }

    template <typename PassT> [[nodiscard]] bool hasResult() const {
        return results.template hasResult<PassT>();
    }

    void invalidate(const PreservedAnalyses& pa);

    [[nodiscard]] const std::vector<Token>& getTokens() const { return tokens; }

    [[nodiscard]] bool getMirrorBoundary() const { return mirror_boundary; }

    [[nodiscard]] int getExpectedFinalDepth() const {
        return expected_final_depth;
    }

  private:
    const std::vector<Token>& tokens;
    AnalysisResults results;
    bool mirror_boundary;
    int expected_final_depth;
};

} // namespace analysis

#endif // LLVMEXPR_ANALYSIS_FRAMEWORK_ANALYSIS_MANAGER_HPP
