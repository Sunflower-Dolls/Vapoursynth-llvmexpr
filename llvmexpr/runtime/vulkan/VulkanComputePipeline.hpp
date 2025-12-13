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
        uint32_t numInputs;
        int32_t frameNumber;
    };

    VulkanComputePipeline(VulkanContext& ctx, const std::string& glslSource,
                          uint32_t numInputBuffers = 1,
                          uint32_t numPropsFloats = 1);
    ~VulkanComputePipeline();

    VulkanComputePipeline(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline& operator=(const VulkanComputePipeline&) = delete;
    VulkanComputePipeline(VulkanComputePipeline&&) = delete;
    VulkanComputePipeline& operator=(VulkanComputePipeline&&) = delete;

    void dispatch(const std::vector<VulkanBuffer*>& inputBuffers,
                  VulkanBuffer& outputBuffer, VulkanBuffer* propsBuffer,
                  uint32_t width, uint32_t height, int32_t frameNumber);

  private:
    void compileShader(const std::string& glslSource);
    void createDescriptorSetLayout(uint32_t numInputBuffers,
                                   bool withPropsBuffer);
    void createPipeline();
    void createCommandResources();
    void updateDescriptorSets(const std::vector<VulkanBuffer*>& inputBuffers,
                              VulkanBuffer& outputBuffer,
                              VulkanBuffer* propsBuffer);

    VulkanContext& context;
    uint32_t numInputs;
    bool hasPropsBuffer;

    std::vector<VkBuffer> cachedInputBuffers;
    VkBuffer cachedOutputBuffer = VK_NULL_HANDLE;
    VkBuffer cachedPropsBuffer = VK_NULL_HANDLE;

    std::vector<uint32_t> spirvCode;
    vk::raii::ShaderModule shaderModule = nullptr;
    vk::raii::DescriptorSetLayout descriptorSetLayout = nullptr;
    vk::raii::PipelineLayout pipelineLayout = nullptr;
    vk::raii::Pipeline pipeline = nullptr;

    vk::raii::DescriptorPool descriptorPool = nullptr;
    vk::raii::DescriptorSet descriptorSet = nullptr;

    vk::raii::CommandPool commandPool = nullptr;
    vk::raii::CommandBuffer commandBuffer = nullptr;
    vk::raii::Fence fence = nullptr;
};

} // namespace llvmexpr

#endif // LLVMEXPR_RUNTIME_VULKAN_VULKANCOMPUTEPIPELINE_HPP
