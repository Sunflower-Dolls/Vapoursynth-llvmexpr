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

#ifndef LLVMEXPR_RUNTIME_VULKAN_VKEXPREXECUTOR_HPP
#define LLVMEXPR_RUNTIME_VULKAN_VKEXPREXECUTOR_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "VapourSynth4.h"

namespace vkexpr {

class VkExprExecutor {
  public:
    VkExprExecutor(int device_id, int num_streams, int num_inputs,
                   std::array<std::vector<std::string>, 3> glsl_stages,
                   std::uint32_t num_props_floats);
    ~VkExprExecutor();

    VkExprExecutor(const VkExprExecutor&) = delete;
    VkExprExecutor& operator=(const VkExprExecutor&) = delete;
    VkExprExecutor(VkExprExecutor&&) = delete;
    VkExprExecutor& operator=(VkExprExecutor&&) = delete;

    void processPlane(int plane, int frame_number,
                      std::span<const VSFrame* const> inputs, VSFrame* output,
                      std::span<const float> props, const VSAPI* vsapi);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace vkexpr

#endif // LLVMEXPR_RUNTIME_VULKAN_VKEXPREXECUTOR_HPP
