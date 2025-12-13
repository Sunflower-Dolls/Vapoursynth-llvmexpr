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
                                             const std::string& glslSource,
                                             uint32_t numInputBuffers,
                                             uint32_t numPropsFloats)
    : context(ctx), numInputs(numInputBuffers),
      hasPropsBuffer(numPropsFloats > 0) {
    compileShader(glslSource);
    createDescriptorSetLayout(numInputBuffers, hasPropsBuffer);
    createPipeline();
    createCommandResources();
}

VulkanComputePipeline::~VulkanComputePipeline() = default;

void VulkanComputePipeline::compileShader(const std::string& glslSource) {
    std::lock_guard<std::mutex> lock(g_shader_cache_mutex);

    if (g_shader_cache.contains(glslSource)) {
        spirvCode = g_shader_cache[glslSource];
    } else {
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;

        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        options.SetTargetEnvironment(shaderc_target_env_vulkan,
                                     shaderc_env_version_vulkan_1_3);
        options.SetTargetSpirv(shaderc_spirv_version_1_6);

        auto result = compiler.CompileGlslToSpv(
            glslSource, shaderc_glsl_compute_shader, "compute.glsl", options);

        if (result.GetCompilationStatus() !=
            shaderc_compilation_status_success) {
            throw std::runtime_error("Shader compilation failed: " +
                                     std::string(result.GetErrorMessage()));
        }

        spirvCode = {result.cbegin(), result.cend()};
        g_shader_cache[glslSource] = spirvCode;
    }

    vk::ShaderModuleCreateInfo moduleInfo;
    moduleInfo.setCode(spirvCode);
    shaderModule = vk::raii::ShaderModule(context.getDevice(), moduleInfo);
}

void VulkanComputePipeline::createDescriptorSetLayout(uint32_t numInputBuffers,
                                                      bool withPropsBuffer) {
    std::vector<vk::DescriptorSetLayoutBinding> bindings;
    uint32_t bindingIndex = 0;

    // Input buffer(s) - bindings 0 to numInputs-1
    for (uint32_t i = 0; i < numInputBuffers; ++i) {
        vk::DescriptorSetLayoutBinding inputBinding;
        inputBinding.binding = bindingIndex++;
        inputBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        inputBinding.descriptorCount = 1;
        inputBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings.push_back(inputBinding);
    }

    // Output buffer
    vk::DescriptorSetLayoutBinding outputBinding;
    outputBinding.binding = bindingIndex++;
    outputBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
    outputBinding.descriptorCount = 1;
    outputBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;
    bindings.push_back(outputBinding);

    // Props buffer (if needed)
    if (withPropsBuffer) {
        vk::DescriptorSetLayoutBinding propsBinding;
        propsBinding.binding = bindingIndex++;
        propsBinding.descriptorType = vk::DescriptorType::eStorageBuffer;
        propsBinding.descriptorCount = 1;
        propsBinding.stageFlags = vk::ShaderStageFlagBits::eCompute;
        bindings.push_back(propsBinding);
    }

    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.setBindings(bindings);
    descriptorSetLayout =
        vk::raii::DescriptorSetLayout(context.getDevice(), layoutInfo);

    // Create pipeline layout with push constants
    vk::PushConstantRange pushConstRange;
    pushConstRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
    pushConstRange.offset = 0;
    pushConstRange.size = sizeof(PushConstants);

    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setSetLayouts(*descriptorSetLayout);
    pipelineLayoutInfo.setPushConstantRanges(pushConstRange);
    pipelineLayout =
        vk::raii::PipelineLayout(context.getDevice(), pipelineLayoutInfo);

    // Create descriptor pool
    vk::DescriptorPoolSize poolSize;
    poolSize.type = vk::DescriptorType::eStorageBuffer;
    // inputs + output + optional props
    poolSize.descriptorCount = numInputBuffers + 1 + (withPropsBuffer ? 1 : 0);

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet;
    poolInfo.maxSets = 1;
    poolInfo.setPoolSizes(poolSize);
    descriptorPool = vk::raii::DescriptorPool(context.getDevice(), poolInfo);

    // Allocate descriptor set
    vk::DescriptorSetAllocateInfo allocInfo;
    allocInfo.descriptorPool = *descriptorPool;
    allocInfo.setSetLayouts(*descriptorSetLayout);
    auto sets = vk::raii::DescriptorSets(context.getDevice(), allocInfo);
    descriptorSet = std::move(sets[0]);
}

void VulkanComputePipeline::createPipeline() {
    vk::PipelineShaderStageCreateInfo stageInfo;
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = *shaderModule;
    stageInfo.pName = "main";

    vk::ComputePipelineCreateInfo pipelineInfo;
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = *pipelineLayout;

    pipeline = vk::raii::Pipeline(context.getDevice(), nullptr, pipelineInfo);
}

void VulkanComputePipeline::createCommandResources() {
    vk::CommandPoolCreateInfo poolInfo(
        vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        context.getQueueFamilyIndex());
    commandPool = vk::raii::CommandPool(context.getDevice(), poolInfo);

    vk::CommandBufferAllocateInfo cmdInfo(*commandPool,
                                          vk::CommandBufferLevel::ePrimary, 1);
    auto cmdBuffers = vk::raii::CommandBuffers(context.getDevice(), cmdInfo);
    commandBuffer = std::move(cmdBuffers[0]);

    vk::FenceCreateInfo fenceInfo;
    fence = vk::raii::Fence(context.getDevice(), fenceInfo);
}

void VulkanComputePipeline::updateDescriptorSets(
    const std::vector<VulkanBuffer*>& inputBuffers, VulkanBuffer& outputBuffer,
    VulkanBuffer* propsBuffer) {

    // Check if we can skip update
    bool inputsChanged = false;
    if (cachedInputBuffers.size() != inputBuffers.size()) {
        inputsChanged = true;
    } else {
        for (size_t i = 0; i < inputBuffers.size(); ++i) {
            if (cachedInputBuffers[i] != inputBuffers[i]->buffer) {
                inputsChanged = true;
                break;
            }
        }
    }

    VkBuffer newPropsBufferHandle =
        (propsBuffer != nullptr) ? propsBuffer->buffer : VK_NULL_HANDLE;

    if (!inputsChanged && cachedOutputBuffer == outputBuffer.buffer &&
        cachedPropsBuffer == newPropsBufferHandle) {
        return;
    }

    std::vector<vk::WriteDescriptorSet> writes;
    std::vector<vk::DescriptorBufferInfo> bufferInfos;

    size_t numBuffers =
        inputBuffers.size() + 1 + (propsBuffer != nullptr ? 1 : 0);
    bufferInfos.reserve(numBuffers);

    // Update cache
    cachedInputBuffers.clear();
    cachedInputBuffers.reserve(inputBuffers.size());
    for (auto* buf : inputBuffers) {
        cachedInputBuffers.push_back(buf->buffer);
    }
    cachedOutputBuffer = outputBuffer.buffer;
    cachedPropsBuffer = newPropsBufferHandle;

    // Input buffers
    for (auto *inputBuffer : inputBuffers) {
        vk::DescriptorBufferInfo bufInfo;
        bufInfo.buffer = inputBuffer->buffer;
        bufInfo.offset = 0;
        bufInfo.range = VK_WHOLE_SIZE;
        bufferInfos.push_back(bufInfo);
    }

    // Output buffer
    vk::DescriptorBufferInfo outBufInfo;
    outBufInfo.buffer = outputBuffer.buffer;
    outBufInfo.offset = 0;
    outBufInfo.range = VK_WHOLE_SIZE;
    bufferInfos.push_back(outBufInfo);

    // Props buffer
    if (propsBuffer != nullptr) {
        vk::DescriptorBufferInfo propsBufInfo;
        propsBufInfo.buffer = propsBuffer->buffer;
        propsBufInfo.offset = 0;
        propsBufInfo.range = VK_WHOLE_SIZE;
        bufferInfos.push_back(propsBufInfo);
    }

    // Create write descriptor sets
    for (size_t i = 0; i < bufferInfos.size(); ++i) {
        vk::WriteDescriptorSet write;
        write.dstSet = *descriptorSet;
        write.dstBinding = static_cast<uint32_t>(i);
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eStorageBuffer;
        write.setBufferInfo(bufferInfos[i]);
        writes.push_back(write);
    }

    context.getDevice().updateDescriptorSets(writes, {});
}

void VulkanComputePipeline::dispatch(
    const std::vector<VulkanBuffer*>& inputBuffers,
    VulkanBuffer& outputBuffer, VulkanBuffer* propsBuffer, uint32_t width,
    uint32_t height, int32_t frameNumber) {

    updateDescriptorSets(inputBuffers, outputBuffer, propsBuffer);

    // Record command buffer
    vk::CommandBufferBeginInfo beginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    commandBuffer.begin(beginInfo);

    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, *pipeline);
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                                     *pipelineLayout, 0, *descriptorSet, {});

    // Set push constants
    PushConstants pc = {.width = width,
                        .height = height,
                        .numInputs = numInputs,
                        .frameNumber = frameNumber};
    commandBuffer.pushConstants<PushConstants>(
        *pipelineLayout, vk::ShaderStageFlagBits::eCompute, 0, pc);

    // Dispatch
    uint32_t totalPixels = width * height;
    uint32_t numWorkgroups =
        (totalPixels + WORKGROUP_SIZE - 1) / WORKGROUP_SIZE;
    commandBuffer.dispatch(numWorkgroups, 1, 1);

    commandBuffer.end();

    // Submit and wait
    vk::SubmitInfo submitInfo;
    submitInfo.setCommandBuffers(*commandBuffer);
    context.getComputeQueue().submit(submitInfo, *fence);

    auto result =
        context.getDevice().waitForFences(*fence, VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for compute fence");
    }
    context.getDevice().resetFences(*fence);
}

} // namespace llvmexpr
