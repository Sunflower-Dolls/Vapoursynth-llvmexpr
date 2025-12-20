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

#include "VkExprExecutor.hpp"

#include "VulkanComputePipeline.hpp"
#include "VulkanContext.hpp"
#include "VulkanMemory.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <queue>
#include <semaphore>
#include <stdexcept>
#include <utility>
#include <vector>

namespace vkexpr {

namespace {

// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers)
float float_from_half_bits(std::uint16_t half_bits) {
    std::uint32_t sign = (half_bits >> 15) & 0x1;
    std::uint32_t exp = (half_bits >> 10) & 0x1F;
    std::uint32_t mant = half_bits & 0x3FF;
    std::uint32_t float_bits = 0;

    if (exp == 0) {
        if (mant == 0) {
            float_bits = sign << 31;
        } else {
            exp = 1;
            while ((mant & 0x400) == 0) {
                mant <<= 1;
                exp--;
            }
            mant &= 0x3FF;
            float_bits = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
        }
    } else if (exp == 31) {
        float_bits = (sign << 31) | 0x7F800000 | (mant << 13);
    } else {
        float_bits = (sign << 31) | ((exp + 127 - 15) << 23) | (mant << 13);
    }

    return std::bit_cast<float>(float_bits);
}

std::uint16_t half_bits_from_float(float value) {
    auto float_bits = std::bit_cast<std::uint32_t>(value);
    std::uint32_t sign = (float_bits >> 31) & 0x1;
    std::int32_t exp =
        static_cast<std::int32_t>((float_bits >> 23) & 0xFF) - 127 + 15;
    std::uint32_t mant = (float_bits >> 13) & 0x3FF;

    if (std::isnan(value)) {
        return static_cast<std::uint16_t>((sign << 15) | 0x7C00 |
                                          ((mant != 0U) ? mant : 1));
    }
    if (std::isinf(value)) {
        return static_cast<std::uint16_t>((sign << 15) | 0x7C00);
    }

    if (exp <= 0) {
        if (exp < -10) {
            return static_cast<std::uint16_t>(sign << 15);
        }
        mant = (mant | 0x400) >> (1 - exp);
        return static_cast<std::uint16_t>((sign << 15) | mant);
    }

    if (exp >= 31) {
        return static_cast<std::uint16_t>((sign << 15) | 0x7C00);
    }

    return static_cast<std::uint16_t>(
        (sign << 15) | (static_cast<std::uint32_t>(exp) << 10) | mant);
}
// NOLINTEND(cppcoreguidelines-avoid-magic-numbers)

// NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-reinterpret-cast)
void pack_plane_to_float(std::span<float> dst, const VSFrame* src, int plane,
                         const VSAPI* vsapi) {
    const int width = vsapi->getFrameWidth(src, plane);
    const int height = vsapi->getFrameHeight(src, plane);
    const VSVideoFormat* format = vsapi->getVideoFrameFormat(src);

    if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid plane dimensions.");
    }
    if (static_cast<size_t>(width) * static_cast<size_t>(height) !=
        dst.size()) {
        throw std::runtime_error("Plane buffer size mismatch.");
    }

    const std::uint8_t* src_data = vsapi->getReadPtr(src, plane);
    if (src_data == nullptr) {
        throw std::runtime_error("Null plane pointer.");
    }

    const std::ptrdiff_t stride = vsapi->getStride(src, plane);
    const int bpp = format->bytesPerSample;
    const bool is_float = (format->sampleType == stFloat);

    if (!is_float) {
        for (int row = 0; row < height; ++row) {
            const std::uint8_t* row_ptr =
                src_data + (static_cast<std::ptrdiff_t>(row) * stride);
            float* out_row = dst.data() + (static_cast<size_t>(row) *
                                           static_cast<size_t>(width));
            for (int col = 0; col < width; ++col) {
                if (bpp == 1) {
                    out_row[col] = static_cast<float>(row_ptr[col]);
                } else if (bpp == 2) {
                    out_row[col] = static_cast<float>(
                        reinterpret_cast<const std::uint16_t*>(row_ptr)[col]);
                } else if (bpp == 4) {
                    out_row[col] = static_cast<float>(
                        reinterpret_cast<const std::uint32_t*>(row_ptr)[col]);
                } else {
                    throw std::runtime_error(
                        "Unsupported integer sample size.");
                }
            }
        }
        return;
    }

    if (bpp == 4) {
        for (int row = 0; row < height; ++row) {
            const std::uint8_t* row_ptr =
                src_data + (static_cast<std::ptrdiff_t>(row) * stride);
            float* out_row = dst.data() + (static_cast<size_t>(row) *
                                           static_cast<size_t>(width));
            std::memcpy(out_row, row_ptr,
                        static_cast<size_t>(width) * sizeof(float));
        }
        return;
    }
    if (bpp == 2) {
        for (int row = 0; row < height; ++row) {
            const std::uint8_t* row_ptr =
                src_data + (static_cast<std::ptrdiff_t>(row) * stride);
            float* out_row = dst.data() + (static_cast<size_t>(row) *
                                           static_cast<size_t>(width));
            for (int col = 0; col < width; ++col) {
                std::uint16_t half_bits =
                    reinterpret_cast<const std::uint16_t*>(row_ptr)[col];
                out_row[col] = float_from_half_bits(half_bits);
            }
        }
        return;
    }

    throw std::runtime_error("Unsupported float sample size.");
}

void unpack_float_to_plane(const std::span<const float> src, VSFrame* dst,
                           int plane, const VSAPI* vsapi) {
    const int width = vsapi->getFrameWidth(dst, plane);
    const int height = vsapi->getFrameHeight(dst, plane);
    const VSVideoFormat* format = vsapi->getVideoFrameFormat(dst);

    [[unlikely]] if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid plane dimensions.");
    }
    [[unlikely]] if (static_cast<size_t>(width) * static_cast<size_t>(height) !=
                     src.size()) {
        throw std::runtime_error("Plane buffer size mismatch.");
    }

    std::uint8_t* dst_data = vsapi->getWritePtr(dst, plane);
    if (dst_data == nullptr) {
        throw std::runtime_error("Null plane pointer.");
    }

    const std::ptrdiff_t stride = vsapi->getStride(dst, plane);
    const int bpp = format->bytesPerSample;
    const bool is_float = (format->sampleType == stFloat);

    if (!is_float) {
        const int bits = format->bitsPerSample;
        if (bits <= 0 || bits > 31) {
            throw std::runtime_error("Invalid integer bitsPerSample.");
        }
        const int max_val = (1 << bits) - 1;

        for (int row = 0; row < height; ++row) {
            std::uint8_t* out_row_ptr =
                dst_data + (static_cast<std::ptrdiff_t>(row) * stride);
            const float* in_row = src.data() + (static_cast<size_t>(row) *
                                                static_cast<size_t>(width));

            for (int col = 0; col < width; ++col) {
                float value = in_row[col];
                float clamped =
                    std::clamp(value, 0.0F, static_cast<float>(max_val));
                int int_val = static_cast<int>(std::nearbyint(clamped));

                if (bpp == 1) {
                    out_row_ptr[col] = static_cast<std::uint8_t>(int_val);
                } else if (bpp == 2) {
                    reinterpret_cast<std::uint16_t*>(out_row_ptr)[col] =
                        static_cast<std::uint16_t>(int_val);
                } else if (bpp == 4) {
                    reinterpret_cast<std::uint32_t*>(out_row_ptr)[col] =
                        static_cast<std::uint32_t>(int_val);
                } else [[unlikely]] {
                    throw std::runtime_error(
                        "Unsupported integer sample size.");
                }
            }
        }
        return;
    }

    if (bpp == 4) {
        for (int row = 0; row < height; ++row) {
            std::uint8_t* out_row_ptr =
                dst_data + (static_cast<std::ptrdiff_t>(row) * stride);
            const float* in_row = src.data() + (static_cast<size_t>(row) *
                                                static_cast<size_t>(width));
            std::memcpy(out_row_ptr, in_row,
                        static_cast<size_t>(width) * sizeof(float));
        }
        return;
    }

    if (bpp == 2) {
        for (int row = 0; row < height; ++row) {
            std::uint8_t* out_row_ptr =
                dst_data + (static_cast<std::ptrdiff_t>(row) * stride);
            const float* in_row = src.data() + (static_cast<size_t>(row) *
                                                static_cast<size_t>(width));
            for (int col = 0; col < width; ++col) {
                float value = in_row[col];
                reinterpret_cast<std::uint16_t*>(out_row_ptr)[col] =
                    half_bits_from_float(value);
            }
        }
        return;
    }

    throw std::runtime_error("Unsupported float sample size.");
}
// NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic,cppcoreguidelines-pro-type-reinterpret-cast)

} // namespace

struct VkExprExecutor::Impl {
    struct PlaneResources {
        std::vector<VulkanBuffer> input_buffers;
        std::vector<VulkanBuffer> input_staging_buffers;
        VulkanBuffer output_buffer;
        VulkanBuffer output_staging_buffer;
        VulkanBuffer props_buffer;
        VulkanBuffer props_staging_buffer;
        std::vector<VulkanBuffer> intermediate_buffers;
        VkDeviceSize buffer_size = 0;
        VkDeviceSize props_size = 0;
        bool initialized = false;
    };

    struct Stream {
        std::unique_ptr<VulkanMemory> memory;
        std::array<std::vector<std::unique_ptr<VulkanComputePipeline>>, 3>
            pipelines;
        std::array<PlaneResources, 3> plane_resources;
        vk::raii::CommandPool command_pool = nullptr;
        vk::raii::CommandBuffer command_buffer = nullptr;
        vk::raii::Fence fence = nullptr;

        Stream() = default;
        ~Stream() {
            for (auto& res : plane_resources) {
                freePlaneResources(res);
            }
        }

        Stream(const Stream&) = delete;
        Stream& operator=(const Stream&) = delete;
        Stream(Stream&&) = delete;
        Stream& operator=(Stream&&) = delete;

        void freePlaneResources(PlaneResources& res) const {
            if (res.initialized) {
                for (auto& buf : res.input_buffers) {
                    memory->destroyBuffer(buf);
                }
                for (auto& buf : res.input_staging_buffers) {
                    memory->destroyBuffer(buf);
                }
                res.input_buffers.clear();
                res.input_staging_buffers.clear();

                memory->destroyBuffer(res.output_buffer);
                memory->destroyBuffer(res.output_staging_buffer);

                for (auto& buf : res.intermediate_buffers) {
                    memory->destroyBuffer(buf);
                }
                res.intermediate_buffers.clear();

                res.initialized = false;
                res.buffer_size = 0;
            }

            if (res.props_buffer.isValid()) {
                memory->destroyBuffer(res.props_buffer);
                memory->destroyBuffer(res.props_staging_buffer);
                res.props_size = 0;
            }
        }
    };

    VulkanContext* context = nullptr;
    int num_inputs = 0;
    std::uint32_t num_props_floats = 0;
    std::array<std::vector<std::string>, 3> glsl_stages;

    int num_streams = 0;
    std::vector<std::unique_ptr<Stream>> streams;
    std::counting_semaphore<> semaphore{0};
    std::queue<int> free_stream_indices;
    std::mutex stream_mutex;

    Impl(int device_id, int num_streams, int num_inputs,
         std::array<std::vector<std::string>, 3> glsl_stages,
         std::uint32_t num_props_floats)
        : context(&VulkanContext::getInstance(device_id)),
          num_inputs(num_inputs), num_props_floats(num_props_floats),
          glsl_stages(std::move(glsl_stages)), num_streams(num_streams),
          semaphore(num_streams) {
        [[unlikely]] if (num_inputs <= 0) {
            throw std::runtime_error("VkExprExecutor: num_inputs must be > 0.");
        }
        [[unlikely]] if (num_streams <= 0) {
            throw std::runtime_error(
                "VkExprExecutor: num_streams must be > 0.");
        }

        auto& ctx = *context;
        streams.resize(static_cast<size_t>(num_streams));
        for (int k = 0; k < num_streams; ++k) {
            streams[static_cast<size_t>(k)] = std::make_unique<Stream>();
            auto& stream = *streams[static_cast<size_t>(k)];
            stream.memory = std::make_unique<VulkanMemory>(ctx);
            free_stream_indices.push(k);

            vk::CommandPoolCreateInfo pool_info(
                vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
                ctx.getQueueFamilyIndex());
            stream.command_pool =
                vk::raii::CommandPool(ctx.getDevice(), pool_info);

            vk::CommandBufferAllocateInfo cmd_info(
                *stream.command_pool, vk::CommandBufferLevel::ePrimary, 1);
            auto cmd_buffers =
                vk::raii::CommandBuffers(ctx.getDevice(), cmd_info);
            stream.command_buffer = std::move(cmd_buffers[0]);

            vk::FenceCreateInfo fence_info;
            stream.fence = vk::raii::Fence(ctx.getDevice(), fence_info);

            for (int plane = 0; plane < 3; ++plane) {
                if (this->glsl_stages.at(plane).empty()) {
                    continue;
                }
                auto& plane_pipelines = stream.pipelines.at(plane);
                auto& stages = this->glsl_stages.at(plane);
                plane_pipelines.resize(stages.size());

                for (size_t s = 0; s < stages.size(); ++s) {
                    plane_pipelines[s] =
                        std::make_unique<VulkanComputePipeline>(
                            ctx, stages[s],
                            static_cast<std::uint32_t>(num_inputs +
                                                       static_cast<int>(s)),
                            num_props_floats);
                }
            }
        }
    }

    int acquireStreamIndex() {
        semaphore.acquire();
        std::lock_guard<std::mutex> lock(stream_mutex);
        int idx = free_stream_indices.front();
        free_stream_indices.pop();
        return idx;
    }

    void releaseStreamIndex(int idx) {
        {
            std::lock_guard<std::mutex> lock(stream_mutex);
            free_stream_indices.push(idx);
        }
        semaphore.release();
    }

    void drain() {
        for (int i = 0; i < num_streams; ++i) {
            semaphore.acquire();
        }
    }
};

VkExprExecutor::VkExprExecutor(
    int device_id, int num_streams, int num_inputs,
    std::array<std::vector<std::string>, 3> glsl_stages,
    std::uint32_t num_props_floats)
    : impl(std::make_unique<Impl>(device_id, num_streams, num_inputs,
                                  std::move(glsl_stages), num_props_floats)) {}

VkExprExecutor::~VkExprExecutor() {
    if (impl != nullptr) {
        impl->drain();
    }
}

void VkExprExecutor::processPlane(int plane, int frame_number,
                                  std::span<const VSFrame* const> inputs,
                                  VSFrame* output, std::span<const float> props,
                                  const VSAPI* vsapi) {
    [[unlikely]] if (impl == nullptr) {
        throw std::runtime_error("VkExprExecutor is not initialized.");
    }
    [[unlikely]] if (plane < 0 || plane > 2) {
        throw std::runtime_error("Invalid plane index.");
    }
    [[unlikely]] if (inputs.size() != static_cast<size_t>(impl->num_inputs)) {
        throw std::runtime_error(
            "VkExprExecutor: unexpected number of inputs.");
    }
    [[unlikely]] if (impl->glsl_stages.at(plane).empty()) {
        throw std::runtime_error(
            "VkExprExecutor: no shader for requested plane.");
    }
    const int width = vsapi->getFrameWidth(output, plane);
    const int height = vsapi->getFrameHeight(output, plane);
    [[unlikely]] if (width <= 0 || height <= 0) {
        throw std::runtime_error("Invalid output dimensions.");
    }

    for (const auto* in : inputs) {
        [[unlikely]] if (vsapi->getFrameWidth(in, plane) != width ||
                         vsapi->getFrameHeight(in, plane) != height) {
            throw std::runtime_error("Input/output plane dimension mismatch.");
        }
    }

    VkDeviceSize buffer_size = static_cast<VkDeviceSize>(width) *
                               static_cast<VkDeviceSize>(height) *
                               sizeof(float);

    const int stream_idx = impl->acquireStreamIndex();
    struct StreamReleaser {
        Impl& impl;
        int idx;
        StreamReleaser(Impl& i, int x) : impl(i), idx(x) {}
        ~StreamReleaser() { impl.releaseStreamIndex(idx); }
        StreamReleaser(const StreamReleaser&) = delete;
        StreamReleaser& operator=(const StreamReleaser&) = delete;
        StreamReleaser(StreamReleaser&&) = delete;
        StreamReleaser& operator=(StreamReleaser&&) = delete;
    } releaser(*impl, stream_idx);

    auto& stream = *impl->streams.at(static_cast<size_t>(stream_idx));
    auto& plane_res = stream.plane_resources.at(plane);
    auto& stage_sources = impl->glsl_stages.at(plane);

    // (Re)allocate per-plane resources if needed
    if (!plane_res.initialized || plane_res.buffer_size < buffer_size ||
        plane_res.input_buffers.size() != inputs.size() ||
        stream.pipelines.at(plane).size() != stage_sources.size()) {

        stream.freePlaneResources(plane_res);

        plane_res.input_buffers.resize(inputs.size());
        plane_res.input_staging_buffers.resize(inputs.size());

        for (size_t i = 0; i < inputs.size(); ++i) {
            plane_res.input_buffers[i] =
                stream.memory->createGPUBuffer(buffer_size);
            plane_res.input_staging_buffers[i] =
                stream.memory->createStagingBuffer(buffer_size, true);
        }

        plane_res.output_buffer = stream.memory->createGPUBuffer(buffer_size);
        plane_res.output_staging_buffer =
            stream.memory->createStagingBuffer(buffer_size, false);

        const size_t num_intermediates =
            (stage_sources.size() > 0) ? (stage_sources.size() - 1) : 0;
        plane_res.intermediate_buffers.resize(num_intermediates);
        for (size_t i = 0; i < num_intermediates; ++i) {
            plane_res.intermediate_buffers[i] =
                stream.memory->createGPUBuffer(buffer_size);
        }

        plane_res.buffer_size = buffer_size;
        plane_res.initialized = true;
    }

    // Props buffers (optional)
    VkDeviceSize props_size = props.size() * sizeof(float);
    if (props_size > 0) {
        if (!plane_res.props_buffer.isValid() ||
            plane_res.props_size < props_size) {
            if (plane_res.props_buffer.isValid()) {
                stream.memory->destroyBuffer(plane_res.props_buffer);
                stream.memory->destroyBuffer(plane_res.props_staging_buffer);
            }

            plane_res.props_buffer = stream.memory->createGPUBuffer(
                props_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            plane_res.props_staging_buffer =
                stream.memory->createStagingBuffer(props_size, true);
            plane_res.props_size = props_size;
        }
    }

    // Pack inputs to float32 staging buffers
    for (size_t i = 0; i < inputs.size(); ++i) {
        auto* mapped_data = static_cast<float*>(
            plane_res.input_staging_buffers[i].getMappedData());
        std::span<float> mapped_span(mapped_data,
                                     static_cast<size_t>(width) *
                                         static_cast<size_t>(height));

        pack_plane_to_float(mapped_span, inputs[i], plane, vsapi);
        stream.memory->flushBuffer(plane_res.input_staging_buffers[i],
                                   buffer_size);
    }

    // Upload props to staging
    if (plane_res.props_buffer.isValid() && props_size > 0) {
        std::memcpy(plane_res.props_staging_buffer.getMappedData(),
                    props.data(), props_size);
        stream.memory->flushBuffer(plane_res.props_staging_buffer, props_size);
    }

    stream.command_buffer.reset();
    vk::CommandBufferBeginInfo begin_info(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit);
    stream.command_buffer.begin(begin_info);

    // Upload input buffers
    for (size_t i = 0; i < inputs.size(); ++i) {
        vk::BufferCopy region(0, 0, buffer_size);
        stream.command_buffer.copyBuffer(
            vk::Buffer(plane_res.input_staging_buffers[i].buffer),
            vk::Buffer(plane_res.input_buffers[i].buffer), region);
    }

    // Upload props
    if (plane_res.props_buffer.isValid() && props_size > 0) {
        vk::BufferCopy region(0, 0, props_size);
        stream.command_buffer.copyBuffer(
            vk::Buffer(plane_res.props_staging_buffer.buffer),
            vk::Buffer(plane_res.props_buffer.buffer), region);
    }

    // Transfer -> compute barriers for inputs/props
    std::vector<vk::BufferMemoryBarrier> to_compute_barriers;
    to_compute_barriers.reserve(inputs.size() + 1);
    for (size_t i = 0; i < inputs.size(); ++i) {
        vk::BufferMemoryBarrier b;
        b.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = vk::Buffer(plane_res.input_buffers[i].buffer);
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;
        to_compute_barriers.push_back(b);
    }
    if (plane_res.props_buffer.isValid() && props_size > 0) {
        vk::BufferMemoryBarrier b;
        b.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.buffer = vk::Buffer(plane_res.props_buffer.buffer);
        b.offset = 0;
        b.size = VK_WHOLE_SIZE;
        to_compute_barriers.push_back(b);
    }
    if (!to_compute_barriers.empty()) {
        stream.command_buffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader, {}, {},
            to_compute_barriers, {});
    }

    const size_t num_stages = stage_sources.size();
    for (size_t s = 0; s < num_stages; ++s) {
        std::vector<VulkanBuffer*> dispatch_inputs;
        dispatch_inputs.reserve(inputs.size() + s);
        for (size_t i = 0; i < inputs.size(); ++i) {
            dispatch_inputs.push_back(&plane_res.input_buffers[i]);
        }
        for (size_t k = 0; k < s; ++k) {
            dispatch_inputs.push_back(&plane_res.intermediate_buffers[k]);
        }

        VulkanBuffer* dispatch_output =
            (s == num_stages - 1) ? &plane_res.output_buffer
                                  : &plane_res.intermediate_buffers[s];

        stream.pipelines.at(plane).at(s)->recordDispatch(
            stream.command_buffer, dispatch_inputs, *dispatch_output,
            (plane_res.props_buffer.isValid() && props_size > 0)
                ? &plane_res.props_buffer
                : nullptr,
            static_cast<std::uint32_t>(width),
            static_cast<std::uint32_t>(height), frame_number);

        if (s < num_stages - 1) {
            vk::BufferMemoryBarrier b;
            b.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
            b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.buffer = vk::Buffer(dispatch_output->buffer);
            b.offset = 0;
            b.size = VK_WHOLE_SIZE;
            std::array<vk::BufferMemoryBarrier, 1> bar = {b};

            stream.command_buffer.pipelineBarrier(
                vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eComputeShader, {}, {}, bar, {});
        }
    }

    // Compute -> transfer barrier for output, then download
    vk::BufferMemoryBarrier to_transfer;
    to_transfer.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    to_transfer.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    to_transfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    to_transfer.buffer = vk::Buffer(plane_res.output_buffer.buffer);
    to_transfer.offset = 0;
    to_transfer.size = VK_WHOLE_SIZE;
    std::array<vk::BufferMemoryBarrier, 1> to_transfer_barriers = {to_transfer};
    stream.command_buffer.pipelineBarrier(
        vk::PipelineStageFlagBits::eComputeShader,
        vk::PipelineStageFlagBits::eTransfer, {}, {}, to_transfer_barriers, {});

    vk::BufferCopy download_region(0, 0, buffer_size);
    stream.command_buffer.copyBuffer(
        vk::Buffer(plane_res.output_buffer.buffer),
        vk::Buffer(plane_res.output_staging_buffer.buffer), download_region);

    stream.command_buffer.end();

    vk::SubmitInfo submit_info;
    submit_info.setCommandBuffers(*stream.command_buffer);
    impl->context->submit(submit_info, *stream.fence);

    auto result = impl->context->getDevice().waitForFences(*stream.fence,
                                                           VK_TRUE, UINT64_MAX);
    if (result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to wait for VkExpr fence");
    }
    impl->context->getDevice().resetFences(*stream.fence);

    stream.memory->invalidateBuffer(plane_res.output_staging_buffer,
                                    buffer_size);
    const auto* mapped_out = static_cast<const float*>(
        plane_res.output_staging_buffer.getMappedData());

    std::span<const float> mapped_span(
        mapped_out, static_cast<size_t>(width) * static_cast<size_t>(height));
    unpack_float_to_plane(mapped_span, output, plane, vsapi);
}

} // namespace vkexpr
