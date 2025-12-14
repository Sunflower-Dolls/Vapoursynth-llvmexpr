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

#include "VulkanComputePipeline.hpp"
#include "VulkanContext.hpp"
#include "VulkanMemory.hpp"

#include <mutex>
#include <shaderc/shaderc.hpp>
#include <stdexcept>
#include <unordered_map>

namespace llvmexpr {

namespace {
constexpr uint32_t WORKGROUP_SIZE = 256;

// Key: GLSL source code, Value: SPIR-V binary
std::unordered_map<std::string, std::vector<uint32_t>>
    g_shader_cache; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::mutex
    g_shader_cache_mutex; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

} // namespace

VulkanComputePipeline::VulkanComputePipeline(VulkanContext& ctx,
                                             const std::string& glsl_source,
                                             uint32_t num_input_buffers,
                                             uint32_t num_props_floats)
    : context(ctx), num_inputs(num_input_buffers),
      has_props_buffer(num_props_floats > 0) {
    compileShader(glsl_source);
    createDescriptorSetLayout(num_input_buffers, has_props_buffer);
    createPipeline();
    createCommandResources();
}

VulkanComputePipeline::~VulkanComputePipeline() = default;

void VulkanComputePipeline::compileShader(const std::string& glsl_source) {
    std::lock_guard<std::mutex> lock(g_shader_cache_mutex);

    if (g_shader_cache.contains(glsl_source)) {
        spirv_code = g_shader_cache[glsl_source];
    } else {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                     shaderc_env_version_vulkan_1_2);
        options.SetTargetSpirv(shaderc_spirv_version_1_5);

        auto result = compiler.CompileGlslToSpv(
            glsl_source, shaderc_glsl_compute_shader, "compute.glsl", options);

        if (result.GetCompilationStatus() !=
            shaderc_compilation_status_success) {
            throw std::runtime_error("Shader compilation failed: " +
                                     std::string(result.GetErrorMessage()));
        }

        spirv_code = {result.cbegin(), result.cend()};
        g_shader_cache[glsl_source] = spirv_code;
    }

    vk::ShaderModuleCreateInfo module_info;
    module_info.setCode(spirv_code);
    shader_module = vk::raii::ShaderModule(context.getDevice(), module_info);
}

void VulkanComputePipeline::createDescriptorSetLayout(
    uint32_t num_input_buffers, bool with_props_buffer) {
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    uint32_t binding_index = 0;

    // Input buffer(s) - bindings 0 to numInputs-1
    for (uint32_t i = 0; i < num_input_buffers; ++i) {
        vk::DescriptorSetLayoutBinding input_binding;
        input_binding.binding = binding_index++;
        input_binding.descriptorType = vk::DescriptorType::eStorageBuffer;
        input_binding.descriptorCount = 1;
        input_binding.stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings.push_back(input_binding);
    }

    // Output buffer
    vk::DescriptorSetLayoutBinding output_binding;
    output_binding.binding = binding_index++;
    output_binding.descriptorType = vk::DescriptorType::eStorageBuffer;
    output_binding.descriptorCount = 1;
    output_binding.stageFlags = vk::ShaderStageFlagBits::eCompute;
    bindings.push_back(output_binding);

    // Props buffer (if needed)
    if (with_props_buffer) {
        vk::DescriptorSetLayoutBinding props_binding;
        props_binding.binding = binding_index++;
        props_binding.descriptorType = vk::DescriptorType::eStorageBuffer;
        props_binding.descriptorCount = 1;
        props_binding.stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings.push_back(props_binding);
    }

    vk::DescriptorSetLayoutCreateInfo layout_info;
    layout_info.setBindings(bindings);
    descriptor_set_layout =
        vk::raii::DescriptorSetLayout(context.getDevice(), layout_info);

    // Create pipeline layout with push constants
    vk::PushConstantRange push_const_range;
    push_const_range.stageFlags = vk::ShaderStageFlagBits::eCompute;
    push_const_range.offset = 0;
    push_const_range.size = sizeof(PushConstants);

    vk::PipelineLayoutCreateInfo pipeline_layout_info;
    pipeline_layout_info.setSetLayouts(*descriptor_set_layout);
    pipeline_layout_info.setPushConstantRanges(push_const_range);
    pipeline_layout =
        vk::raii::PipelineLayout(context.getDevice(), pipeline_layout_info);

    // Create descriptor pool
    vk::DescriptorPoolSize pool_size;
    pool_size.type = vk::DescriptorType::eStorageBuffer;
    // inputs + output + optional props
    pool_size.descriptorCount =
        num_input_buffers + 1 + (with_props_buffer ? 1 : 0);

    vk::DescriptorPoolCreateInfo pool_info;
    pool_info.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    pool_info.maxSets = 1;
    pool_info.setPoolSizes(pool_size);
    descriptor_pool = vk::raii::DescriptorPool(context.getDevice(), pool_info);

    // Allocate descriptor set
    vk::DescriptorSetAllocateInfo alloc_info;
    alloc_info.descriptorPool = *descriptor_pool;
    alloc_info.setSetLayouts(*descriptor_set_layout);
    auto sets = vk::raii::DescriptorSets(context.getDevice(), alloc_info);
    descriptor_set = std::move(sets[0]);
}

void VulkanComputePipeline::createPipeline() {
    vk::PipelineShaderStageCreateInfo stage_info;
    stage_info.stage = vk::ShaderStageFlagBits::eCompute;
    stage_info.module = *shader_module;
    stage_info.pName = "main";

    vk::ComputePipelineCreateInfo pipeline_info;
    pipeline_info.stage = stage_info;
    pipeline_info.layout = *pipeline_layout;

    pipeline = vk::raii::Pipeline(context.getDevice(), nullptr, pipeline_info);
}

void VulkanComputePipeline::createCommandResources() {
    vk::CommandPoolCreateInfo pool_info(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        context.getQueueFamilyIndex());
    command_pool = vk::raii::CommandPool(context.getDevice(), pool_info);

    vk::CommandBufferAllocateInfo cmd_info(*command_pool,
                                           vk::CommandBufferLevel::ePrimary, 1);
    auto cmd_buffers = vk::raii::CommandBuffers(context.getDevice(), cmd_info);
    command_buffer = std::move(cmd_buffers[0]);

    vk::FenceCreateInfo fence_info;
    fence = vk::raii::Fence(context.getDevice(), fence_info);
}

void VulkanComputePipeline::updateDescriptorSets(
    const std::vector<VulkanBuffer*>& input_buffers,
    VulkanBuffer& output_buffer, VulkanBuffer* props_buffer) {

    // Check if we can skip update
    bool inputs_changed = false;
    if (cached_input_buffers.size() != input_buffers.size()) {
        inputs_changed = true;
    } else {
        for (size_t i = 0; i < input_buffers.size(); ++i) {
            if (cached_input_buffers[i] != input_buffers[i]->buffer) {
                inputs_changed = true;
                break;
            }
        }
    }

    VkBuffer new_props_buffer_handle =
        (props_buffer != nullptr) ? props_buffer->buffer : VK_NULL_HANDLE;

    if (!inputs_changed && cached_output_buffer == output_buffer.buffer &&
        cached_props_buffer == new_props_buffer_handle) {
        return;
    }

    std::vector<vk::WriteDescriptorSet> writes;
    std::vector<vk::DescriptorBufferInfo> buffer_infos;

    size_t num_buffers =
        input_buffers.size() + 1 + (props_buffer != nullptr ? 1 : 0);
    buffer_infos.reserve(num_buffers);

    // Update cache
    cached_input_buffers.clear();
    cached_input_buffers.reserve(input_buffers.size());
    for (auto* buf : input_buffers) {
        cached_input_buffers.push_back(buf->buffer);
    }
    cached_output_buffer = output_buffer.buffer;
    cached_props_buffer = new_props_buffer_handle;

    // Input buffers
    for (auto* input_buffer : input_buffers) {
        vk::DescriptorBufferInfo buf_info;
        buf_info.buffer = input_buffer->buffer;
        buf_info.offset = 0;
        buf_info.range = VK_WHOLE_SIZE;
        buffer_infos.push_back(buf_info);
    }

    // Output buffer
    vk::DescriptorBufferInfo out_buf_info;
    out_buf_info.buffer = output_buffer.buffer;
    out_buf_info.offset = 0;
    out_buf_info.range = VK_WHOLE_SIZE;
    buffer_infos.push_back(out_buf_info);

    // Props buffer
    if (props_buffer != nullptr) {
        vk::DescriptorBufferInfo props_buf_info;
        props_buf_info.buffer = props_buffer->buffer;
        props_buf_info.offset = 0;
        props_buf_info.range = VK_WHOLE_SIZE;
        buffer_infos.push_back(props_buf_info);
    }

    // Create write descriptor sets
    for (size_t i = 0; i < buffer_infos.size(); ++i) {
        vk::WriteDescriptorSet write;
        write.dstSet = *descriptor_set;
        write.dstBinding = static_cast<uint32_t>(i);
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.setBufferInfo(buffer_infos[i]);
        writes.push_back(write);
    }

    context.getDevice().updateDescriptorSets(writes, {});
}

void VulkanComputePipeline::dispatch(
    const std::vector<VulkanBuffer*>& input_buffers,
    VulkanBuffer& output_buffer, VulkanBuffer* props_buffer, uint32_t width,
    uint32_t height, int32_t frame_number) {

    updateDescriptorSets(input_buffers, output_buffer, props_buffer);

    // Record command buffer
    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    command_buffer.begin(begin_info);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                      *pipeline_layout, 0, *descriptor_set, {});

    // Set push constants
    PushConstants pc = {.width = width,
                        .height = height,
                        .num_inputs = num_inputs,
                        .frame_number = frame_number};
    command_buffer.pushConstants<PushConstants>(
        *pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, pc);

    // Dispatch
    uint32_t total_pixels = width * height;
    uint32_t num_workgroups =
        (total_pixels + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    command_buffer.dispatch(num_workgroups, 1, 1);

    command_buffer.end();

    // Submit and wait
    vk::SubmitInfo submit_info;
    submit_info.setCommandBuffers(*command_buffer);
    context.submit(submit_info, *fence);

    auto result =
        context.getDevice().waitForFences(*fence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for compute fence");
    }
    context.getDevice().resetFences(*fence);
}

} // namespace llvmexpr
