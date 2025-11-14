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

#ifndef LLVMEXPR_INFIXCONVERTER_HPP
#define LLVMEXPR_INFIXCONVERTER_HPP

#include "infix2postfix/types.hpp"
#include <string>
#include <vector>

//TODO: Expose these to postfix
struct InfixConversionContext {
    int width = 0;
    int height = 0;
    int num_inputs = 0;
    int output_bitdepth = 0;
    std::vector<int> input_bitdepths;
    int subsample_w = 0;
    int subsample_h = 0;
    int plane_no = -1; // -1 = applicable
    int output_format = 0; // 1 for float, -1 for int
    std::vector<int> input_formats;
    std::vector<int> input_widths;
    std::vector<int> input_heights;
    std::vector<int> input_subsample_ws;
    std::vector<int> input_subsample_hs;
};

std::string
convertInfixToPostfix(const std::string& infix_expr, int num_inputs,
                      infix2postfix::Mode mode,
                      const InfixConversionContext* context = nullptr);

#endif
