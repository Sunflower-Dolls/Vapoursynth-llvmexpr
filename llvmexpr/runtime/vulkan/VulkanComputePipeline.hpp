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

#ifndef LLVMEXPR_RUNTIME_VULKAN_VULKANCOMPUTEPIPELINE_HPP
#define LLVMEXPR_RUNTIME_VULKAN_VULKANCOMPUTEPIPELINE_HPP

#define VK_NO_PROTOTYPES

// NOLINTBEGIN(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1

// NOLINTEND(cppcoreguidelines-macro-usage,cppcoreguidelines-macro-to-enum,modernize-macro-to-enum)

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace llvmexpr {

class VulkanContext;
class VulkanMemory;
struct VulkanBuffer;

class VulkanComputePipeline {
  public:
    struct PushConstants {
        uint32_t width;
        uint32_t height;
        uint32_t num_inputs;
        int32_t frame_number;
    };

    VulkanComputePipeline(VulkanContext& ctx, const std::string& glsl_source,
                          uint32_t num_input_buffers = 1,
                          uint32_t num_props_floats = 1);
    ~VulkanComputePipeline();

    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline(VulkanComputePipeline&&) = delete;
    VulkanComputePipeline& operator=(VulkanComputePipeline&&) = delete;

    void dispatch(const std::vector<VulkanBuffer*>& input_buffers,
                  VulkanBuffer& output_buffer, VulkanBuffer* props_buffer,
                  uint32_t width, uint32_t height, int32_t frame_number);

    void recordDispatch(vk::raii::CommandBuffer& command_buffer,
                        const std::vector<VulkanBuffer*>& input_buffers,
                        VulkanBuffer& output_buffer, VulkanBuffer* props_buffer,
                        uint32_t width, uint32_t height, int32_t frame_number);

  private:
    void compileShader(const std::string& glsl_source);
    void createDescriptorSetLayout(uint32_t num_input_buffers,
                                   bool with_props_buffer);
    void createPipeline();
    void createCommandResources();
    void updateDescriptorSets(const std::vector<VulkanBuffer*>& input_buffers,
                              VulkanBuffer& output_buffer,
                              VulkanBuffer* props_buffer);

    VulkanContext& context;
    uint32_t num_inputs;
    bool has_props_buffer;

    std::vector<VkBuffer> cached_input_buffers;
    VkBuffer cached_output_buffer = VK_NULL_HANDLE;
    VkBuffer cached_props_buffer = VK_NULL_HANDLE;

    std::vector<uint32_t> spirv_code;
    vk::raii::ShaderModule shader_module = nullptr;
    vk::raii::DescriptorSetLayout descriptor_set_layout = nullptr;
    vk::raii::PipelineLayout pipeline_layout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    vk::raii::DescriptorPool descriptor_pool = nullptr;
    vk::raii::DescriptorSet descriptor_set = nullptr;

    vk::raii::CommandPool command_pool = nullptr;
    vk::raii::CommandBuffer command_buffer = nullptr;
    vk::raii::Fence fence = nullptr;
};

} // namespace llvmexpr

#endif // LLVMEXPR_RUNTIME_VULKAN_VULKANCOMPUTEPIPELINE_HPP
