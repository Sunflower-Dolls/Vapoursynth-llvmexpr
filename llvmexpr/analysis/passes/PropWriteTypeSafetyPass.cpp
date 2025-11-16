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

#include "PropWriteTypeSafetyPass.hpp"
#include "../../frontend/Tokenizer.hpp"
#include "../../utils/EnumName.hpp"
#include "../framework/AnalysisError.hpp"
#include "../framework/AnalysisManager.hpp"
#include <format>
#include <map>
#include <string>

namespace analysis {

PropWriteTypeSafetyPass::Result
PropWriteTypeSafetyPass::run(const std::vector<Token>& tokens,
                             [[maybe_unused]] AnalysisManager& am) {
    std::map<std::string, std::pair<PropWriteType, int>> prop_types;

    for (size_t i = 0; i < tokens.size(); ++i) {
        const auto& token = tokens[i];
        if (token.type == TokenType::PROP_STORE) {
            const auto& payload =
                std::get<TokenPayload_PropStore>(token.payload);
            const auto& prop_name = payload.prop_name;
            const auto& prop_type = payload.type;

            auto it = prop_types.find(prop_name);
            if (it != prop_types.end()) {
                if (it->second.first != prop_type) {
                    throw AnalysisError(
                        std::format(
                            "Inconsistent types used for property '{}'. "
                            "Previous type: {} (idx: {}), current type: {} "
                            "(idx: {}).",
                            prop_name, enum_name(it->second.first),
                            it->second.second, enum_name(prop_type),
                            static_cast<int>(i)),
                        static_cast<int>(i));
                }
            } else {
                prop_types[prop_name] =
                    std::make_pair(prop_type, static_cast<int>(i));
            }
        }
    }

    return {};
}

} // namespace analysis
