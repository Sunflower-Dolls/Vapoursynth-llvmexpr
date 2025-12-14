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

#include "RelAccessAnalysisPass.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "../framework/AnalysisManager.hpp"

#include <set>

namespace analysis {

RelAccessAnalysisResult
RelAccessAnalysisPass::run(const std::vector<Token>& tokens,
                           AnalysisManager& am) {
    RelAccessAnalysisResult result;
    result.mirror_boundary = am.getMirrorBoundary();

    std::set<RelYAccess> seen;
    for (const auto& token : tokens) {
        if (token.type == TokenType::ClipRel) {
            const auto& payload =
                std::get<TokenPayloadClipAccess>(token.payload);
            bool use_mirror =
                payload.has_mode ? payload.use_mirror : result.mirror_boundary;
            RelYAccess access{.clip_idx = payload.clip_idx,
                              .rel_y = payload.rel_y,
                              .use_mirror = use_mirror};
            if (!seen.contains(access)) {
                seen.insert(access);
                result.unique_rel_y_accesses.push_back(access);
            }
            result.min_rel_x = std::min(result.min_rel_x, payload.rel_x);
            result.max_rel_x = std::max(result.max_rel_x, payload.rel_x);
        } else if (token.type == TokenType::ClipCur) {
            const auto& payload =
                std::get<TokenPayloadClipAccess>(token.payload);
            RelYAccess access{.clip_idx = payload.clip_idx,
                              .rel_y = 0,
                              .use_mirror = result.mirror_boundary};
            if (!seen.contains(access)) {
                seen.insert(access);
                result.unique_rel_y_accesses.push_back(access);
            }
        }
    }
    return result;
}

} // namespace analysis
