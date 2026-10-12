// Vulkan 1.3 backend: dynamic rendering, synchronization2, VMA allocations.

#include "openbl/rhi/vulkan.hpp"

#include "openbl/platform/window.hpp"

#include <volk.h>
#include <vk_mem_alloc.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace openbl::rhi {

namespace {

constexpr std::uint32_t kFramesInFlight = 2;
constexpr VkFormat kDepthFormat = VK_FORMAT_D32_SFLOAT;

void check(VkResult result, const char* what) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string("Vulkan: ") + what + " failed (VkResult " + std::to_string(result) + ")");
    }
}

VkFormat to_vk(Format f) {
    switch (f) {
    case Format::RGBA8_UNorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case Format::RGBA8_sRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case Format::BGRA8_UNorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case Format::BGRA8_sRGB: return VK_FORMAT_B8G8R8A8_SRGB;
    case Format::BC1_UNorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case Format::BC1_sRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
    case Format::BC2_UNorm: return VK_FORMAT_BC2_UNORM_BLOCK;
    case Format::BC2_sRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
    case Format::BC3_UNorm: return VK_FORMAT_BC3_UNORM_BLOCK;
    case Format::BC3_sRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
    case Format::R32_Float: return VK_FORMAT_R32_SFLOAT;
    case Format::RG32_Float: return VK_FORMAT_R32G32_SFLOAT;
    case Format::RGB32_Float: return VK_FORMAT_R32G32B32_SFLOAT;
    case Format::RGBA32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case Format::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case Format::RGBA16_UInt: return VK_FORMAT_R16G16B16A16_UINT;
    case Format::RGBA8_UInt: return VK_FORMAT_R8G8B8A8_UINT;
    case Format::D32_Float: return VK_FORMAT_D32_SFLOAT;
    case Format::Undefined: break;
    }
    return VK_FORMAT_UNDEFINED;
}

Format from_vk(VkFormat f) {
    switch (f) {
    case VK_FORMAT_B8G8R8A8_SRGB: return Format::BGRA8_sRGB;
    case VK_FORMAT_B8G8R8A8_UNORM: return Format::BGRA8_UNorm;
    case VK_FORMAT_R8G8B8A8_SRGB: return Format::RGBA8_sRGB;
    case VK_FORMAT_R8G8B8A8_UNORM: return Format::RGBA8_UNorm;
    default: return Format::Undefined;
    }
}

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void*) {
    const char* level = (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? "error" : "warning";
    std::fprintf(stderr, "[vulkan %s] %s\n", level, data->pMessage);
    return VK_FALSE;
}

class VulkanDevice;

class VulkanBuffer final : public Buffer {
public:
    VulkanBuffer(VulkanDevice& device, VkBuffer buffer, VmaAllocation allocation, std::size_t size)
        : device_(device), buffer_(buffer), allocation_(allocation), size_(size) {}
    ~VulkanBuffer() override;
    std::size_t size() const override { return size_; }
    VkBuffer handle() const { return buffer_; }

private:
    VulkanDevice& device_;
    VkBuffer buffer_;
    VmaAllocation allocation_;
    std::size_t size_;
};

class VulkanTexture final : public Texture {
public:
    VulkanTexture(VulkanDevice& device, const TextureDesc& desc, VkImage image, VkImageView view, VmaAllocation allocation)
        : device_(device), desc_(desc), image_(image), view_(view), allocation_(allocation) {}
    ~VulkanTexture() override;
    const TextureDesc& desc() const override { return desc_; }
    VkImageView view() const { return view_; }
    VkImage image() const { return image_; }

private:
    VulkanDevice& device_;
    TextureDesc desc_;
    VkImage image_;
    VkImageView view_;
    VmaAllocation allocation_;
};

class VulkanRenderTarget final : public RenderTarget {
public:
    VulkanRenderTarget(VulkanDevice& device, std::unique_ptr<VulkanTexture> color, VkImageView attachment_view,
                       VkImage depth, VkImageView depth_view, VmaAllocation depth_allocation)
        : device_(device), color_(std::move(color)), attachment_view_(attachment_view), depth_(depth),
          depth_view_(depth_view), depth_allocation_(depth_allocation) {}
    ~VulkanRenderTarget() override;
    const Texture& texture() const override { return *color_; }
    const VulkanTexture& color() const { return *color_; }
    // Drawn through a plain view (encoded values, like the window); sampled
    // through the texture's sRGB view, so shaders read linear colour.
    VkImageView attachment_view() const { return attachment_view_; }
    VkImage depth() const { return depth_; }
    VkImageView depth_view() const { return depth_view_; }

private:
    VulkanDevice& device_;
    std::unique_ptr<VulkanTexture> color_;
    VkImageView attachment_view_;
    VkImage depth_;
    VkImageView depth_view_;
    VmaAllocation depth_allocation_;
};

class VulkanSampler final : public Sampler {
public:
    VulkanSampler(VulkanDevice& device, VkSampler sampler) : device_(device), sampler_(sampler) {}
    ~VulkanSampler() override;
    VkSampler handle() const { return sampler_; }

private:
    VulkanDevice& device_;
    VkSampler sampler_;
};

class VulkanPipeline final : public Pipeline {
public:
    VulkanPipeline(VulkanDevice& device, VkPipeline pipeline, VkPipelineLayout layout, VkDescriptorSetLayout set_layout,
                   std::uint32_t texture_count, std::uint32_t push_size, bool shader_data)
        : device_(device), pipeline_(pipeline), layout_(layout), set_layout_(set_layout), texture_count_(texture_count),
          push_size_(push_size), shader_data_(shader_data) {}
    ~VulkanPipeline() override;
    VkPipeline handle() const { return pipeline_; }
    VkPipelineLayout layout() const { return layout_; }
    VkDescriptorSetLayout set_layout() const { return set_layout_; }
    std::uint32_t texture_count() const { return texture_count_; }
    std::uint32_t push_size() const { return push_size_; }
    bool shader_data() const { return shader_data_; }

private:
    VulkanDevice& device_;
    VkPipeline pipeline_;
    VkPipelineLayout layout_;
    VkDescriptorSetLayout set_layout_;
    std::uint32_t texture_count_;
    std::uint32_t push_size_;
    bool shader_data_;
};

class VulkanBindGroup final : public BindGroup {
public:
    VulkanBindGroup(VulkanDevice& device, VkDescriptorPool pool, VkDescriptorSet set)
        : device_(device), pool_(pool), set_(set) {}
    ~VulkanBindGroup() override;
    VkDescriptorSet handle() const { return set_; }

private:
    VulkanDevice& device_;
    VkDescriptorPool pool_;
    VkDescriptorSet set_;
};

class VulkanCommandList final : public CommandList {
public:
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    const VulkanPipeline* pipeline = nullptr;

    void set_pipeline(const Pipeline& p) override {
        ensure_pass();
        pipeline = static_cast<const VulkanPipeline*>(&p);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline->handle());
    }
    void set_bind_group(const BindGroup& group) override {
        const VkDescriptorSet set = static_cast<const VulkanBindGroup&>(group).handle();
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, require_pipeline().layout(), 0, 1, &set, 0, nullptr);
    }
    void set_vertex_buffer(const Buffer& buffer, std::size_t offset) override {
        ensure_pass();
        const VkBuffer b = static_cast<const VulkanBuffer&>(buffer).handle();
        const VkDeviceSize o = offset;
        vkCmdBindVertexBuffers(cmd, 0, 1, &b, &o);
    }
    void set_index_buffer(const Buffer& buffer, std::size_t offset) override {
        ensure_pass();
        vkCmdBindIndexBuffer(cmd, static_cast<const VulkanBuffer&>(buffer).handle(), offset, VK_INDEX_TYPE_UINT32);
    }
    void push_constants(const void* data, std::uint32_t size) override {
        const VulkanPipeline& p = require_pipeline();
        if (size > p.push_size()) {
            throw std::runtime_error("push constants larger than the pipeline declares");
        }
        vkCmdPushConstants(cmd, p.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, size, data);
    }
    void draw_indexed(std::uint32_t index_count, std::uint32_t first_index, std::int32_t vertex_offset) override {
        ensure_pass();
        vkCmdDrawIndexed(cmd, index_count, 1, first_index, vertex_offset, 0);
    }
    ShaderDataRef set_shader_data(const void* data, std::size_t size) override {
        const VulkanPipeline& p = require_pipeline();
        if (!p.shader_data()) {
            throw std::runtime_error("bound pipeline does not use shader data");
        }
        if (size == 0 || size > kMaxShaderDataSize) {
            throw std::invalid_argument("bad shader data size");
        }
        const VkDeviceSize offset = (data_cursor + data_alignment - 1) / data_alignment * data_alignment;
        if (offset + kMaxShaderDataSize > data_capacity) {
            throw std::runtime_error("per-frame shader data exhausted");
        }
        std::memcpy(data_base + offset, data, size);
        data_cursor = offset + size;
        const auto dynamic_offset = static_cast<std::uint32_t>(offset);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout(), 1, 1, &data_set, 1, &dynamic_offset);
        return {offset};
    }
    void bind_shader_data(ShaderDataRef ref) override {
        const VulkanPipeline& p = require_pipeline();
        if (!p.shader_data()) {
            throw std::runtime_error("bound pipeline does not use shader data");
        }
        // Every block was placed with a full kMaxShaderDataSize range left after it.
        if (ref.offset >= data_cursor) {
            throw std::invalid_argument("shader data not placed in this frame");
        }
        const auto dynamic_offset = static_cast<std::uint32_t>(ref.offset);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, p.layout(), 1, 1, &data_set, 1, &dynamic_offset);
    }

    void begin_render_target(const RenderTarget& rt, const float clear_color[4]) override {
        if (main_begun) {
            throw std::logic_error("render targets must be drawn before the window in a frame");
        }
        if (target) {
            throw std::logic_error("render target already active");
        }
        target = static_cast<const VulkanRenderTarget*>(&rt);
        image_barrier_fn(cmd, target->color().image(), VK_IMAGE_ASPECT_COLOR_BIT, 1,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        image_barrier_fn(cmd, target->depth(), VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                         VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
        const VkExtent2D extent{target->texture().desc().width, target->texture().desc().height};
        VkRenderingAttachmentInfo color_info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color_info.imageView = target->attachment_view();
        color_info.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_info.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::memcpy(color_info.clearValue.color.float32, clear_color, 4 * sizeof(float));
        VkRenderingAttachmentInfo depth_info{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth_info.imageView = target->depth_view();
        depth_info.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth_info.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_info.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_info.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, extent};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color_info;
        rendering.pDepthAttachment = &depth_info;
        vkCmdBeginRendering(cmd, &rendering);
        const VkViewport viewport{0, 0, static_cast<float>(extent.width), static_cast<float>(extent.height), 0, 1};
        const VkRect2D scissor{{0, 0}, extent};
        vkCmdSetViewport(cmd, 0, 1, &viewport);
        vkCmdSetScissor(cmd, 0, 1, &scissor);
        pipeline = nullptr;
    }

    void end_render_target() override {
        if (!target) {
            throw std::logic_error("no render target active");
        }
        vkCmdEndRendering(cmd);
        image_barrier_fn(cmd, target->color().image(), VK_IMAGE_ASPECT_COLOR_BIT, 1,
                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                         VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        target = nullptr;
        pipeline = nullptr;
    }

    void clear_depth() override {
        ensure_pass();
        const VkExtent2D extent = target ? VkExtent2D{target->texture().desc().width, target->texture().desc().height}
                                         : window_extent;
        VkClearAttachment clear{};
        clear.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        clear.clearValue.depthStencil = {1.0f, 0};
        VkClearRect rect{};
        rect.rect = {{0, 0}, extent};
        rect.layerCount = 1;
        vkCmdClearAttachments(cmd, 1, &clear, 1, &rect);
    }

    // Starts the window pass on first use, unless a render target is active.
    void ensure_pass() {
        if (!target && !main_begun) {
            begin_main();
            main_begun = true;
        }
    }

    std::function<void()> begin_main; // Begins rendering to the window (set by begin_frame).
    VkExtent2D window_extent{};
    bool main_begun = false;
    const VulkanRenderTarget* target = nullptr;
    void (*image_barrier_fn)(VkCommandBuffer, VkImage, VkImageAspectFlags, std::uint32_t, VkImageLayout, VkImageLayout,
                             VkPipelineStageFlags2, VkAccessFlags2, VkPipelineStageFlags2, VkAccessFlags2) = nullptr;

    // Per-frame shader data ring, set up by begin_frame().
    std::byte* data_base = nullptr;
    VkDeviceSize data_capacity = 0;
    VkDeviceSize data_cursor = 0;
    VkDeviceSize data_alignment = 256;
    VkDescriptorSet data_set = VK_NULL_HANDLE;

private:
    const VulkanPipeline& require_pipeline() const {
        if (!pipeline) {
            throw std::runtime_error("no pipeline bound");
        }
        return *pipeline;
    }
};

class VulkanDevice final : public Device {
public:
    VulkanDevice(platform::Window& window, const VulkanOptions& options) : window_(window), options_(options) {
        check(volkInitialize(), "loading the Vulkan library");
        create_instance();
        surface_ = reinterpret_cast<VkSurfaceKHR>(window_.create_vulkan_surface(instance_));
        pick_physical_device();
        create_device();
        create_allocator();
        create_frame_resources();
        create_swapchain();
    }

    ~VulkanDevice() override {
        if (device_) {
            vkDeviceWaitIdle(device_);
            run_deletions(true);
            if (capture_.buffer) {
                vmaDestroyBuffer(allocator_, capture_.buffer, capture_.allocation);
            }
            destroy_swapchain();
            for (auto& f : frames_) {
                vmaDestroyBuffer(allocator_, f.data_buffer, f.data_allocation);
                vkDestroyFence(device_, f.fence, nullptr);
                vkDestroySemaphore(device_, f.image_available, nullptr);
                vkDestroyCommandPool(device_, f.pool, nullptr);
            }
            vkDestroyCommandPool(device_, upload_pool_, nullptr);
            vkDestroyDescriptorPool(device_, shader_data_pool_, nullptr);
            vkDestroyDescriptorSetLayout(device_, shader_data_layout_, nullptr);
            vkDestroyFence(device_, upload_fence_, nullptr);
            for (VkDescriptorPool pool : descriptor_pools_) {
                vkDestroyDescriptorPool(device_, pool, nullptr);
            }
            vmaDestroyAllocator(allocator_);
            vkDestroyDevice(device_, nullptr);
        }
        if (surface_) {
            vkDestroySurfaceKHR(instance_, surface_, nullptr);
        }
        if (messenger_) {
            vkDestroyDebugUtilsMessengerEXT(instance_, messenger_, nullptr);
        }
        if (instance_) {
            vkDestroyInstance(instance_, nullptr);
        }
    }

    VkDevice handle() const { return device_; }
    VmaAllocator allocator() const { return allocator_; }

    // Destroys GPU objects once no in-flight frame can still use them.
    void defer(std::function<void()> fn) { deletions_.emplace_back(frame_counter_, std::move(fn)); }

    DeviceInfo info() const override { return {"Vulkan 1.3", adapter_name_}; }
    std::uint32_t frame_width() const override { return extent_.width; }
    std::uint32_t frame_height() const override { return extent_.height; }
    Format color_format() const override { return from_vk(swapchain_format_); }
    Format depth_format() const override { return Format::D32_Float; }
    void wait_idle() override { vkDeviceWaitIdle(device_); }

    std::unique_ptr<Buffer> create_buffer(const BufferDesc& desc, std::span<const std::byte> data) override {
        if (desc.size == 0 || data.size() > desc.size) {
            throw std::invalid_argument("bad buffer size");
        }
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = desc.size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                     (desc.usage == BufferUsage::Vertex ? VK_BUFFER_USAGE_VERTEX_BUFFER_BIT : VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        check(vmaCreateBuffer(allocator_, &info, &alloc, &buffer, &allocation, nullptr), "vmaCreateBuffer");
        auto result = std::make_unique<VulkanBuffer>(*this, buffer, allocation, desc.size);
        if (!data.empty()) {
            Staging staging = create_staging(data.size());
            std::memcpy(staging.mapped, data.data(), data.size());
            submit_upload([&](VkCommandBuffer cmd) {
                const VkBufferCopy region{0, 0, data.size()};
                vkCmdCopyBuffer(cmd, staging.buffer, buffer, 1, &region);
            });
            vmaDestroyBuffer(allocator_, staging.buffer, staging.allocation);
        }
        return result;
    }

    std::unique_ptr<Texture> create_texture(const TextureDesc& desc,
                                            std::span<const std::span<const std::byte>> mips) override {
        if (desc.width == 0 || desc.height == 0 || desc.mip_levels == 0 || mips.size() > desc.mip_levels) {
            throw std::invalid_argument("bad texture description");
        }
        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = to_vk(desc.format);
        info.extent = {desc.width, desc.height, 1};
        const std::uint32_t layers = desc.cube ? 6 : 1;
        if (desc.cube && desc.width != desc.height) {
            throw std::invalid_argument("cube map faces must be square");
        }
        info.mipLevels = desc.mip_levels;
        info.arrayLayers = layers;
        info.flags = desc.cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        check(vmaCreateImage(allocator_, &info, &alloc, &image, &allocation, nullptr), "vmaCreateImage");

        VkImageViewCreateInfo view_info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view_info.image = image;
        view_info.viewType = desc.cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D;
        view_info.format = info.format;
        view_info.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, desc.mip_levels, 0, layers};
        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device_, &view_info, nullptr, &view), "vkCreateImageView");
        auto result = std::make_unique<VulkanTexture>(*this, desc, image, view, allocation);

        std::size_t total = 0;
        for (std::size_t m = 0; m < mips.size(); ++m) {
            const std::size_t expected =
                format_image_size(desc.format, std::max(1u, desc.width >> m), std::max(1u, desc.height >> m)) * layers;
            if (mips[m].size() < expected) {
                throw std::invalid_argument("texture mip data too small");
            }
            total += (expected + 15) & ~std::size_t{15};
        }
        Staging staging = create_staging(std::max<std::size_t>(total, 16));
        std::vector<VkBufferImageCopy> regions;
        std::size_t offset = 0;
        for (std::size_t m = 0; m < mips.size(); ++m) {
            const std::uint32_t w = std::max(1u, desc.width >> m);
            const std::uint32_t h = std::max(1u, desc.height >> m);
            const std::size_t face = format_image_size(desc.format, w, h);
            std::memcpy(static_cast<std::byte*>(staging.mapped) + offset, mips[m].data(), face * layers);
            for (std::uint32_t l = 0; l < layers; ++l) {
                VkBufferImageCopy region{};
                region.bufferOffset = offset + face * l;
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, static_cast<std::uint32_t>(m), l, 1};
                region.imageExtent = {w, h, 1};
                regions.push_back(region);
            }
            offset += (face * layers + 15) & ~std::size_t{15};
        }
        submit_upload([&](VkCommandBuffer cmd) {
            image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, desc.mip_levels, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0,
                          VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT);
            if (!regions.empty()) {
                vkCmdCopyBufferToImage(cmd, staging.buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                       static_cast<std::uint32_t>(regions.size()), regions.data());
            }
            image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, desc.mip_levels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_COPY_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });
        vmaDestroyBuffer(allocator_, staging.buffer, staging.allocation);
        return result;
    }

    std::unique_ptr<Sampler> create_sampler(const SamplerDesc& desc) override {
        VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        const VkFilter filter = desc.filter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        info.magFilter = filter;
        info.minFilter = filter;
        info.mipmapMode = desc.filter == Filter::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        const VkSamplerAddressMode mode =
            desc.address == AddressMode::Repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeU = info.addressModeV = info.addressModeW = mode;
        info.anisotropyEnable = desc.max_anisotropy > 1.0f;
        info.maxAnisotropy = std::min(desc.max_anisotropy, max_anisotropy_);
        info.maxLod = VK_LOD_CLAMP_NONE;
        VkSampler sampler = VK_NULL_HANDLE;
        check(vkCreateSampler(device_, &info, nullptr, &sampler), "vkCreateSampler");
        return std::make_unique<VulkanSampler>(*this, sampler);
    }

    std::unique_ptr<RenderTarget> create_render_target(std::uint32_t width, std::uint32_t height) override {
        if (width == 0 || height == 0) {
            throw std::invalid_argument("bad render target size");
        }
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

        VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        info.imageType = VK_IMAGE_TYPE_2D;
        info.format = swapchain_format_;
        info.extent = {width, height, 1};
        info.mipLevels = 1;
        info.arrayLayers = 1;
        info.samples = VK_SAMPLE_COUNT_1_BIT;
        info.tiling = VK_IMAGE_TILING_OPTIMAL;
        info.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        info.flags = VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        check(vmaCreateImage(allocator_, &info, &alloc, &image, &allocation, nullptr), "render target image");
        const VkFormat sampled = swapchain_format_ == VK_FORMAT_B8G8R8A8_UNORM   ? VK_FORMAT_B8G8R8A8_SRGB
                                 : swapchain_format_ == VK_FORMAT_R8G8B8A8_UNORM ? VK_FORMAT_R8G8B8A8_SRGB
                                                                                 : swapchain_format_;
        const VkImageView attachment_view = create_view(image, swapchain_format_, VK_IMAGE_ASPECT_COLOR_BIT);
        const VkImageView view = create_view(image, sampled, VK_IMAGE_ASPECT_COLOR_BIT);
        TextureDesc desc{width, height, 1, from_vk(sampled)};
        auto color = std::make_unique<VulkanTexture>(*this, desc, image, view, allocation);
        info.flags = 0;

        info.format = kDepthFormat;
        info.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VkImage depth = VK_NULL_HANDLE;
        VmaAllocation depth_allocation = nullptr;
        check(vmaCreateImage(allocator_, &info, &alloc, &depth, &depth_allocation, nullptr), "render target depth");
        const VkImageView depth_view = create_view(depth, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);

        // Start black and ready to sample.
        submit_upload([&](VkCommandBuffer cmd) {
            image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT);
            const VkClearColorValue black{{0, 0, 0, 1}};
            const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdClearColorImage(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &black, 1, &range);
            image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_PIPELINE_STAGE_2_CLEAR_BIT,
                          VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                          VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        });
        return std::make_unique<VulkanRenderTarget>(*this, std::move(color), attachment_view, depth, depth_view,
                                                    depth_allocation);
    }

    std::unique_ptr<Pipeline> create_pipeline(const PipelineDesc& desc) override {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        bindings.push_back({0, VK_DESCRIPTOR_TYPE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        for (std::uint32_t t = 0; t < desc.texture_count; ++t) {
            bindings.push_back({t + 1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
        }
        VkDescriptorSetLayoutCreateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        set_info.bindingCount = static_cast<std::uint32_t>(bindings.size());
        set_info.pBindings = bindings.data();
        VkDescriptorSetLayout set_layout = VK_NULL_HANDLE;
        check(vkCreateDescriptorSetLayout(device_, &set_info, nullptr, &set_layout), "vkCreateDescriptorSetLayout");

        const VkPushConstantRange push{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                       desc.push_constant_size};
        const VkDescriptorSetLayout set_layouts[] = {set_layout, shader_data_layout_};
        VkPipelineLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layout_info.setLayoutCount = desc.shader_data ? 2 : 1;
        layout_info.pSetLayouts = set_layouts;
        layout_info.pushConstantRangeCount = desc.push_constant_size ? 1 : 0;
        layout_info.pPushConstantRanges = &push;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        check(vkCreatePipelineLayout(device_, &layout_info, nullptr, &layout), "vkCreatePipelineLayout");

        const VkShaderModule vs = create_shader(desc.vertex);
        const VkShaderModule ps = create_shader(desc.pixel);
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, vs,
                     desc.vertex_entry.c_str(), nullptr};
        stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, ps,
                     desc.pixel_entry.c_str(), nullptr};

        const VkVertexInputBindingDescription vertex_binding{0, desc.vertex_stride, VK_VERTEX_INPUT_RATE_VERTEX};
        std::vector<VkVertexInputAttributeDescription> attributes;
        for (const auto& a : desc.attributes) {
            attributes.push_back({a.location, 0, to_vk(a.format), a.offset});
        }
        VkPipelineVertexInputStateCreateInfo vertex_input{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vertex_input.vertexBindingDescriptionCount = 1;
        vertex_input.pVertexBindingDescriptions = &vertex_binding;
        vertex_input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertex_input.pVertexAttributeDescriptions = attributes.data();

        VkPipelineInputAssemblyStateCreateInfo assembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = desc.cull == CullMode::None ? VK_CULL_MODE_NONE
                          : desc.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT
                                                        : VK_CULL_MODE_FRONT_BIT;
        raster.frontFace = desc.front_counter_clockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
        depth.depthTestEnable = desc.depth_test;
        depth.depthWriteEnable = desc.depth_write;
        depth.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blend{};
        blend.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                               VK_COLOR_COMPONENT_A_BIT;
        if (desc.blend == BlendMode::Modulate) {
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_SRC_COLOR;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
        } else if (desc.blend == BlendMode::Premultiplied) {
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
        } else if (desc.blend != BlendMode::Opaque) {
            blend.blendEnable = VK_TRUE;
            blend.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blend.dstColorBlendFactor =
                desc.blend == BlendMode::Alpha ? VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA : VK_BLEND_FACTOR_ONE;
            blend.colorBlendOp = VK_BLEND_OP_ADD;
            blend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blend.alphaBlendOp = VK_BLEND_OP_ADD;
        }
        VkPipelineColorBlendStateCreateInfo blend_state{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        blend_state.attachmentCount = 1;
        blend_state.pAttachments = &blend;

        const VkDynamicState dynamic_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamic_states;

        VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachmentFormats = &swapchain_format_;
        rendering.depthAttachmentFormat = kDepthFormat;

        VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        info.pNext = &rendering;
        info.stageCount = 2;
        info.pStages = stages;
        info.pVertexInputState = &vertex_input;
        info.pInputAssemblyState = &assembly;
        info.pViewportState = &viewport;
        info.pRasterizationState = &raster;
        info.pMultisampleState = &multisample;
        info.pDepthStencilState = &depth;
        info.pColorBlendState = &blend_state;
        info.pDynamicState = &dynamic;
        info.layout = layout;
        VkPipeline pipeline = VK_NULL_HANDLE;
        const VkResult result = vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline);
        vkDestroyShaderModule(device_, vs, nullptr);
        vkDestroyShaderModule(device_, ps, nullptr);
        check(result, "vkCreateGraphicsPipelines");
        return std::make_unique<VulkanPipeline>(*this, pipeline, layout, set_layout, desc.texture_count,
                                                desc.push_constant_size, desc.shader_data);
    }

    std::unique_ptr<BindGroup> create_bind_group(const Pipeline& pipeline, const Sampler& sampler,
                                                 std::span<const Texture* const> textures) override {
        const auto& p = static_cast<const VulkanPipeline&>(pipeline);
        if (textures.size() != p.texture_count()) {
            throw std::invalid_argument("bind group texture count does not match the pipeline");
        }
        VkDescriptorPool pool = VK_NULL_HANDLE;
        const VkDescriptorSet set = allocate_set(p.set_layout(), pool);

        const VkDescriptorImageInfo sampler_info{static_cast<const VulkanSampler&>(sampler).handle(), VK_NULL_HANDLE,
                                                 VK_IMAGE_LAYOUT_UNDEFINED};
        std::vector<VkDescriptorImageInfo> image_infos;
        for (const Texture* t : textures) {
            image_infos.push_back({VK_NULL_HANDLE, static_cast<const VulkanTexture*>(t)->view(),
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
        }
        std::vector<VkWriteDescriptorSet> writes;
        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
        w.dstSet = set;
        w.dstBinding = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        w.pImageInfo = &sampler_info;
        writes.push_back(w);
        for (std::uint32_t t = 0; t < image_infos.size(); ++t) {
            w.dstBinding = t + 1;
            w.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            w.pImageInfo = &image_infos[t];
            writes.push_back(w);
        }
        vkUpdateDescriptorSets(device_, static_cast<std::uint32_t>(writes.size()), writes.data(), 0, nullptr);
        return std::make_unique<VulkanBindGroup>(*this, pool, set);
    }

    CommandList* begin_frame(const FrameDesc& desc) override {
        if (window_.minimized()) {
            return nullptr;
        }
        Frame& frame = frames_[frame_index_];
        check(vkWaitForFences(device_, 1, &frame.fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
        run_deletions(false);

        const auto [w, h] = window_.framebuffer_size();
        if (w == 0 || h == 0) {
            return nullptr;
        }
        if (swapchain_dirty_ || w != extent_.width || h != extent_.height) {
            recreate_swapchain();
        }
        VkResult acquired = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, frame.image_available,
                                                  VK_NULL_HANDLE, &image_index_);
        if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
            swapchain_dirty_ = true;
            return nullptr;
        }
        if (acquired != VK_SUBOPTIMAL_KHR) {
            check(acquired, "vkAcquireNextImageKHR");
        }
        check(vkResetFences(device_, 1, &frame.fence), "vkResetFences");
        check(vkResetCommandPool(device_, frame.pool, 0), "vkResetCommandPool");

        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(frame.cmd, &begin), "vkBeginCommandBuffer");

        command_list_.cmd = frame.cmd;
        command_list_.pipeline = nullptr;
        command_list_.data_base = frame.data_mapped;
        command_list_.data_capacity = kShaderDataPerFrame;
        command_list_.data_cursor = 0;
        command_list_.data_alignment = storage_alignment_;
        command_list_.data_set = frame.data_set;
        command_list_.target = nullptr;
        command_list_.main_begun = false;
        command_list_.window_extent = extent_;
        command_list_.image_barrier_fn = &VulkanDevice::image_barrier;
        const FrameDesc frame_desc = desc;
        command_list_.begin_main = [this, frame_desc] { begin_window_pass(frame_desc); };
        in_frame_ = true;
        return &command_list_;
    }

    // Starts rendering to the acquired swapchain image (called lazily, so
    // render targets can be drawn first).
    void begin_window_pass(const FrameDesc& desc) {
        Frame& frame = frames_[frame_index_];
        const VkImage image = swapchain_images_[image_index_];
        image_barrier(frame.cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
                      VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        image_barrier(frame.cmd, depth_image_, VK_IMAGE_ASPECT_DEPTH_BIT, 1, VK_IMAGE_LAYOUT_UNDEFINED,
                      VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                      VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                      VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

        VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        color.imageView = swapchain_views_[image_index_];
        color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        std::memcpy(color.clearValue.color.float32, desc.clear_color, sizeof(desc.clear_color));
        VkRenderingAttachmentInfo depth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
        depth.imageView = depth_view_;
        depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth.clearValue.depthStencil = {1.0f, 0};
        VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
        rendering.renderArea = {{0, 0}, extent_};
        rendering.layerCount = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments = &color;
        rendering.pDepthAttachment = &depth;
        vkCmdBeginRendering(frame.cmd, &rendering);

        const VkViewport viewport{0, 0, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0, 1};
        const VkRect2D scissor{{0, 0}, extent_};
        vkCmdSetViewport(frame.cmd, 0, 1, &viewport);
        vkCmdSetScissor(frame.cmd, 0, 1, &scissor);
    }

    void end_frame() override {
        if (!in_frame_) {
            return;
        }
        in_frame_ = false;
        Frame& frame = frames_[frame_index_];
        if (command_list_.target) {
            command_list_.end_render_target();
        }
        command_list_.ensure_pass(); // Clears the window even if nothing was drawn to it.
        vkCmdEndRendering(frame.cmd);
        const VkImage image = swapchain_images_[image_index_];
        if (capture_requested_) {
            record_capture(frame.cmd, image);
        } else {
            image_barrier(frame.cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                          VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                          VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_NONE, 0);
        }
        check(vkEndCommandBuffer(frame.cmd), "vkEndCommandBuffer");
        if (command_list_.data_cursor > 0) {
            check(vmaFlushAllocation(allocator_, frame.data_allocation, 0, command_list_.data_cursor), "flush shader data");
        }

        VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        wait.semaphore = frame.image_available;
        wait.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSemaphoreSubmitInfo signal{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
        signal.semaphore = render_finished_[image_index_];
        signal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_info.commandBuffer = frame.cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.waitSemaphoreInfoCount = 1;
        submit.pWaitSemaphoreInfos = &wait;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        submit.signalSemaphoreInfoCount = 1;
        submit.pSignalSemaphoreInfos = &signal;
        check(vkQueueSubmit2(queue_, 1, &submit, frame.fence), "vkQueueSubmit2");

        VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores = &render_finished_[image_index_];
        present.swapchainCount = 1;
        present.pSwapchains = &swapchain_;
        present.pImageIndices = &image_index_;
        const VkResult presented = vkQueuePresentKHR(queue_, &present);
        if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
            swapchain_dirty_ = true;
        } else {
            check(presented, "vkQueuePresentKHR");
        }
        frame_index_ = (frame_index_ + 1) % kFramesInFlight;
        ++frame_counter_;
    }

    void request_capture() override { capture_requested_ = true; }

    std::optional<CapturedImage> take_capture() override {
        if (!capture_.buffer) {
            return std::nullopt;
        }
        vkDeviceWaitIdle(device_);
        check(vmaInvalidateAllocation(allocator_, capture_.allocation, 0, VK_WHOLE_SIZE), "vmaInvalidateAllocation");
        CapturedImage out;
        out.width = capture_extent_.width;
        out.height = capture_extent_.height;
        const auto* src = static_cast<const std::uint8_t*>(capture_.mapped);
        out.rgba.assign(src, src + std::size_t{out.width} * out.height * 4);
        if (capture_format_ == VK_FORMAT_B8G8R8A8_SRGB || capture_format_ == VK_FORMAT_B8G8R8A8_UNORM) {
            for (std::size_t i = 0; i < out.rgba.size(); i += 4) {
                std::swap(out.rgba[i], out.rgba[i + 2]);
            }
        }
        vmaDestroyBuffer(allocator_, capture_.buffer, capture_.allocation);
        capture_ = {};
        return out;
    }

private:
    // Copies the back buffer into a host-readable buffer and leaves it ready to present.
    void record_capture(VkCommandBuffer cmd, VkImage image) {
        capture_requested_ = false;
        if (capture_.buffer) {
            vmaDestroyBuffer(allocator_, capture_.buffer, capture_.allocation);
        }
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = std::size_t{extent_.width} * extent_.height * 4;
        info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO;
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VmaAllocationInfo out{};
        check(vmaCreateBuffer(allocator_, &info, &alloc, &capture_.buffer, &capture_.allocation, &out), "capture buffer");
        capture_.mapped = out.pMappedData;
        capture_extent_ = extent_;
        capture_format_ = swapchain_format_;

        image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                      VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, capture_.buffer, 1, &region);
        image_barrier(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                      VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COPY_BIT, 0, VK_PIPELINE_STAGE_2_NONE, 0);
    }

    struct Frame {
        VkCommandPool pool = VK_NULL_HANDLE;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkSemaphore image_available = VK_NULL_HANDLE;
        // Per-draw shader data written by the CPU during the frame.
        VkBuffer data_buffer = VK_NULL_HANDLE;
        VmaAllocation data_allocation = nullptr;
        std::byte* data_mapped = nullptr;
        VkDescriptorSet data_set = VK_NULL_HANDLE;
    };
    static constexpr VkDeviceSize kShaderDataPerFrame = 8 * 1024 * 1024;

    struct Staging {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        void* mapped = nullptr;
    };

    void create_instance() {
        std::uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, layers.data());
        std::uint32_t ext_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, nullptr);
        std::vector<VkExtensionProperties> available(ext_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &ext_count, available.data());
        auto has_extension = [&](const char* name) {
            return std::any_of(available.begin(), available.end(),
                               [&](const VkExtensionProperties& e) { return std::strcmp(e.extensionName, name) == 0; });
        };

        std::vector<const char*> extensions = window_.vulkan_instance_extensions();
        std::vector<const char*> enabled_layers;
        bool debug_utils = false;
        if (options_.validation) {
            const bool has_layer = std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& l) {
                return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
            });
            if (has_layer) {
                enabled_layers.push_back("VK_LAYER_KHRONOS_validation");
            } else {
                std::fprintf(stderr, "Vulkan validation layer not installed; continuing without it\n");
            }
            if (has_extension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
                extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                debug_utils = true;
            }
        }

        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName = "OpenBL";
        app.pEngineName = "OpenBL";
        app.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &app;
        info.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        info.ppEnabledExtensionNames = extensions.data();
        info.enabledLayerCount = static_cast<std::uint32_t>(enabled_layers.size());
        info.ppEnabledLayerNames = enabled_layers.data();
        check(vkCreateInstance(&info, nullptr, &instance_), "vkCreateInstance");
        volkLoadInstance(instance_);

        if (debug_utils) {
            VkDebugUtilsMessengerCreateInfoEXT messenger{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
            messenger.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            messenger.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            messenger.pfnUserCallback = debug_callback;
            check(vkCreateDebugUtilsMessengerEXT(instance_, &messenger, nullptr, &messenger_), "debug messenger");
        }
    }

    void pick_physical_device() {
        std::uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance_, &count, nullptr);
        std::vector<VkPhysicalDevice> devices(count);
        vkEnumeratePhysicalDevices(instance_, &count, devices.data());

        int best_score = -1;
        for (VkPhysicalDevice pd : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(pd, &props);
            if (props.apiVersion < VK_API_VERSION_1_3) {
                continue;
            }
            VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
            VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
            f2.pNext = &f13;
            vkGetPhysicalDeviceFeatures2(pd, &f2);
            if (!f13.dynamicRendering || !f13.synchronization2 || !f2.features.textureCompressionBC) {
                continue;
            }
            std::uint32_t ext_count = 0;
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, nullptr);
            std::vector<VkExtensionProperties> exts(ext_count);
            vkEnumerateDeviceExtensionProperties(pd, nullptr, &ext_count, exts.data());
            const bool swapchain = std::any_of(exts.begin(), exts.end(), [](const VkExtensionProperties& e) {
                return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
            if (!swapchain) {
                continue;
            }
            std::uint32_t family_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &family_count, nullptr);
            std::vector<VkQueueFamilyProperties> families(family_count);
            vkGetPhysicalDeviceQueueFamilyProperties(pd, &family_count, families.data());
            for (std::uint32_t q = 0; q < family_count; ++q) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(pd, q, surface_, &present);
                if (!(families[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) || !present) {
                    continue;
                }
                const int score = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 1000 : 0) +
                                  (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 100 : 0);
                if (score > best_score) {
                    best_score = score;
                    physical_ = pd;
                    queue_family_ = q;
                    adapter_name_ = props.deviceName;
                    max_anisotropy_ = f2.features.samplerAnisotropy ? props.limits.maxSamplerAnisotropy : 1.0f;
                    storage_alignment_ = std::max<VkDeviceSize>(props.limits.minStorageBufferOffsetAlignment, 16);
                    anisotropy_supported_ = f2.features.samplerAnisotropy;
                }
                break;
            }
        }
        if (!physical_) {
            throw std::runtime_error("no GPU with Vulkan 1.3, dynamic rendering and BC texture support found");
        }
    }

    void create_device() {
        const float priority = 1.0f;
        VkDeviceQueueCreateInfo queue_info{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue_info.queueFamilyIndex = queue_family_;
        queue_info.queueCount = 1;
        queue_info.pQueuePriorities = &priority;

        VkPhysicalDeviceVulkan13Features f13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
        f13.dynamicRendering = VK_TRUE;
        f13.synchronization2 = VK_TRUE;
        VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        f2.pNext = &f13;
        f2.features.textureCompressionBC = VK_TRUE;
        f2.features.samplerAnisotropy = anisotropy_supported_;

        const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkDeviceCreateInfo info{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        info.pNext = &f2;
        info.queueCreateInfoCount = 1;
        info.pQueueCreateInfos = &queue_info;
        info.enabledExtensionCount = 1;
        info.ppEnabledExtensionNames = extensions;
        check(vkCreateDevice(physical_, &info, nullptr, &device_), "vkCreateDevice");
        volkLoadDevice(device_);
        vkGetDeviceQueue(device_, queue_family_, 0, &queue_);
    }

    void create_allocator() {
        VmaAllocatorCreateInfo info{};
        info.vulkanApiVersion = VK_API_VERSION_1_3;
        info.physicalDevice = physical_;
        info.device = device_;
        info.instance = instance_;
        VmaVulkanFunctions functions{};
        check(vmaImportVulkanFunctionsFromVolk(&info, &functions), "vmaImportVulkanFunctionsFromVolk");
        info.pVulkanFunctions = &functions;
        check(vmaCreateAllocator(&info, &allocator_), "vmaCreateAllocator");
    }

    void create_frame_resources() {
        VkCommandPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool_info.queueFamilyIndex = queue_family_;
        VkFenceCreateInfo fence_info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fence_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (Frame& f : frames_) {
            check(vkCreateCommandPool(device_, &pool_info, nullptr, &f.pool), "vkCreateCommandPool");
            VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            alloc.commandPool = f.pool;
            alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            alloc.commandBufferCount = 1;
            check(vkAllocateCommandBuffers(device_, &alloc, &f.cmd), "vkAllocateCommandBuffers");
            check(vkCreateFence(device_, &fence_info, nullptr, &f.fence), "vkCreateFence");
            check(vkCreateSemaphore(device_, &sem_info, nullptr, &f.image_available), "vkCreateSemaphore");
        }
        pool_info.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        check(vkCreateCommandPool(device_, &pool_info, nullptr, &upload_pool_), "vkCreateCommandPool");
        create_shader_data_resources();
        fence_info.flags = 0;
        check(vkCreateFence(device_, &fence_info, nullptr, &upload_fence_), "vkCreateFence");
    }

    void create_shader_data_resources() {
        const VkDescriptorSetLayoutBinding binding{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1,
                                                   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo layout_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layout_info.bindingCount = 1;
        layout_info.pBindings = &binding;
        check(vkCreateDescriptorSetLayout(device_, &layout_info, nullptr, &shader_data_layout_), "shader data layout");

        const VkDescriptorPoolSize size{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, kFramesInFlight};
        VkDescriptorPoolCreateInfo pool_info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        pool_info.maxSets = kFramesInFlight;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &size;
        check(vkCreateDescriptorPool(device_, &pool_info, nullptr, &shader_data_pool_), "shader data pool");

        for (Frame& f : frames_) {
            VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
            info.size = kShaderDataPerFrame;
            info.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            // Shaders read this for every vertex (bones) and pixel (material
            // params), so keep it in video memory the CPU can write to rather
            // than in system memory across the bus.
            VmaAllocationCreateInfo alloc{};
            alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
            alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
            VmaAllocationInfo out{};
            check(vmaCreateBuffer(allocator_, &info, &alloc, &f.data_buffer, &f.data_allocation, &out),
                  "shader data buffer");
            f.data_mapped = static_cast<std::byte*>(out.pMappedData);

            VkDescriptorSetAllocateInfo set_info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            set_info.descriptorPool = shader_data_pool_;
            set_info.descriptorSetCount = 1;
            set_info.pSetLayouts = &shader_data_layout_;
            check(vkAllocateDescriptorSets(device_, &set_info, &f.data_set), "shader data set");
            const VkDescriptorBufferInfo buffer_info{f.data_buffer, 0, kMaxShaderDataSize};
            VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            write.dstSet = f.data_set;
            write.descriptorCount = 1;
            write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
            write.pBufferInfo = &buffer_info;
            vkUpdateDescriptorSets(device_, 1, &write, 0, nullptr);
        }
    }

    void create_swapchain() {
        VkSurfaceCapabilitiesKHR caps;
        check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &caps), "surface capabilities");
        std::uint32_t count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data());
        VkSurfaceFormatKHR chosen = formats.at(0);
        for (const auto& f : formats) {
            // A plain format: shaders encode sRGB themselves, so blending works
            // on encoded values (gamma space), as the original's did.
            if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                chosen = f;
                break;
            }
        }
        swapchain_format_ = chosen.format;

        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, nullptr);
        std::vector<VkPresentModeKHR> modes(count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_, surface_, &count, modes.data());
        VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
        if (!options_.vsync) {
            for (VkPresentModeKHR m : {VK_PRESENT_MODE_MAILBOX_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR}) {
                if (std::find(modes.begin(), modes.end(), m) != modes.end()) {
                    mode = m;
                    break;
                }
            }
        }

        const auto [w, h] = window_.framebuffer_size();
        extent_ = caps.currentExtent.width != UINT32_MAX ? caps.currentExtent : VkExtent2D{w, h};
        extent_.width = std::clamp(extent_.width, caps.minImageExtent.width, caps.maxImageExtent.width);
        extent_.height = std::clamp(extent_.height, caps.minImageExtent.height, caps.maxImageExtent.height);
        std::uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0) {
            image_count = std::min(image_count, caps.maxImageCount);
        }

        VkSwapchainCreateInfoKHR info{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        info.surface = surface_;
        info.minImageCount = image_count;
        info.imageFormat = chosen.format;
        info.imageColorSpace = chosen.colorSpace;
        info.imageExtent = extent_;
        info.imageArrayLayers = 1;
        info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                          (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT); // for captures
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        info.preTransform = caps.currentTransform;
        info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        info.presentMode = mode;
        info.clipped = VK_TRUE;
        check(vkCreateSwapchainKHR(device_, &info, nullptr, &swapchain_), "vkCreateSwapchainKHR");

        vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr);
        swapchain_images_.resize(count);
        vkGetSwapchainImagesKHR(device_, swapchain_, &count, swapchain_images_.data());
        VkSemaphoreCreateInfo sem_info{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkImage image : swapchain_images_) {
            swapchain_views_.push_back(create_view(image, swapchain_format_, VK_IMAGE_ASPECT_COLOR_BIT));
            VkSemaphore s = VK_NULL_HANDLE;
            check(vkCreateSemaphore(device_, &sem_info, nullptr, &s), "vkCreateSemaphore");
            render_finished_.push_back(s);
        }

        VkImageCreateInfo depth{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        depth.imageType = VK_IMAGE_TYPE_2D;
        depth.format = kDepthFormat;
        depth.extent = {extent_.width, extent_.height, 1};
        depth.mipLevels = 1;
        depth.arrayLayers = 1;
        depth.samples = VK_SAMPLE_COUNT_1_BIT;
        depth.tiling = VK_IMAGE_TILING_OPTIMAL;
        depth.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        check(vmaCreateImage(allocator_, &depth, &alloc, &depth_image_, &depth_allocation_, nullptr), "depth image");
        depth_view_ = create_view(depth_image_, kDepthFormat, VK_IMAGE_ASPECT_DEPTH_BIT);
        swapchain_dirty_ = false;
    }

    void destroy_swapchain() {
        for (VkImageView v : swapchain_views_) {
            vkDestroyImageView(device_, v, nullptr);
        }
        for (VkSemaphore s : render_finished_) {
            vkDestroySemaphore(device_, s, nullptr);
        }
        swapchain_views_.clear();
        render_finished_.clear();
        swapchain_images_.clear();
        if (depth_view_) {
            vkDestroyImageView(device_, depth_view_, nullptr);
            vmaDestroyImage(allocator_, depth_image_, depth_allocation_);
            depth_view_ = VK_NULL_HANDLE;
        }
        if (swapchain_) {
            vkDestroySwapchainKHR(device_, swapchain_, nullptr);
            swapchain_ = VK_NULL_HANDLE;
        }
    }

    void recreate_swapchain() {
        vkDeviceWaitIdle(device_);
        destroy_swapchain();
        create_swapchain();
    }

    VkImageView create_view(VkImage image, VkFormat format, VkImageAspectFlags aspect) {
        VkImageViewCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = image;
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = format;
        info.subresourceRange = {aspect, 0, 1, 0, 1};
        VkImageView view = VK_NULL_HANDLE;
        check(vkCreateImageView(device_, &info, nullptr, &view), "vkCreateImageView");
        return view;
    }

    VkShaderModule create_shader(const ShaderCode& code) {
        if (code.spirv.empty()) {
            throw std::invalid_argument("pipeline is missing SPIR-V code");
        }
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.spirv.size_bytes();
        info.pCode = code.spirv.data();
        VkShaderModule module = VK_NULL_HANDLE;
        check(vkCreateShaderModule(device_, &info, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    static void image_barrier(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect, std::uint32_t mips,
                              VkImageLayout from, VkImageLayout to, VkPipelineStageFlags2 src_stage,
                              VkAccessFlags2 src_access, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 dst_access) {
        VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
        barrier.srcStageMask = src_stage;
        barrier.srcAccessMask = src_access;
        barrier.dstStageMask = dst_stage;
        barrier.dstAccessMask = dst_access;
        barrier.oldLayout = from;
        barrier.newLayout = to;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = image;
        barrier.subresourceRange = {aspect, 0, mips, 0, VK_REMAINING_ARRAY_LAYERS};
        VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dep.imageMemoryBarrierCount = 1;
        dep.pImageMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dep);
    }

    Staging create_staging(std::size_t size) {
        VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        info.size = size;
        info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        VmaAllocationCreateInfo alloc{};
        alloc.usage = VMA_MEMORY_USAGE_AUTO;
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        Staging s;
        VmaAllocationInfo out{};
        check(vmaCreateBuffer(allocator_, &info, &alloc, &s.buffer, &s.allocation, &out), "staging buffer");
        s.mapped = out.pMappedData;
        return s;
    }

    // Records and runs a one-off transfer, waiting for it to finish.
    void submit_upload(const std::function<void(VkCommandBuffer)>& record) {
        VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        alloc.commandPool = upload_pool_;
        alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        alloc.commandBufferCount = 1;
        VkCommandBuffer cmd = VK_NULL_HANDLE;
        check(vkAllocateCommandBuffers(device_, &alloc, &cmd), "vkAllocateCommandBuffers");
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(cmd, &begin), "vkBeginCommandBuffer");
        record(cmd);
        check(vkEndCommandBuffer(cmd), "vkEndCommandBuffer");
        VkCommandBufferSubmitInfo cmd_info{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
        cmd_info.commandBuffer = cmd;
        VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &cmd_info;
        check(vkQueueSubmit2(queue_, 1, &submit, upload_fence_), "vkQueueSubmit2 (upload)");
        check(vkWaitForFences(device_, 1, &upload_fence_, VK_TRUE, UINT64_MAX), "vkWaitForFences (upload)");
        check(vkResetFences(device_, 1, &upload_fence_), "vkResetFences");
        vkFreeCommandBuffers(device_, upload_pool_, 1, &cmd);
    }

    VkDescriptorSet allocate_set(VkDescriptorSetLayout layout, VkDescriptorPool& pool_out) {
        for (int attempt = 0; attempt < 2; ++attempt) {
            if (!descriptor_pools_.empty()) {
                VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
                info.descriptorPool = descriptor_pools_.back();
                info.descriptorSetCount = 1;
                info.pSetLayouts = &layout;
                VkDescriptorSet set = VK_NULL_HANDLE;
                const VkResult r = vkAllocateDescriptorSets(device_, &info, &set);
                if (r == VK_SUCCESS) {
                    pool_out = descriptor_pools_.back();
                    return set;
                }
                if (r != VK_ERROR_OUT_OF_POOL_MEMORY && r != VK_ERROR_FRAGMENTED_POOL) {
                    check(r, "vkAllocateDescriptorSets");
                }
            }
            const VkDescriptorPoolSize sizes[] = {{VK_DESCRIPTOR_TYPE_SAMPLER, 4096},
                                                  {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 16384}};
            VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
            info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
            info.maxSets = 4096;
            info.poolSizeCount = 2;
            info.pPoolSizes = sizes;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            check(vkCreateDescriptorPool(device_, &info, nullptr, &pool), "vkCreateDescriptorPool");
            descriptor_pools_.push_back(pool);
        }
        throw std::runtime_error("descriptor set allocation failed");
    }

    void run_deletions(bool all) {
        auto it = deletions_.begin();
        while (it != deletions_.end()) {
            if (all || it->first + kFramesInFlight <= frame_counter_) {
                it->second();
                it = deletions_.erase(it);
            } else {
                ++it;
            }
        }
    }

    platform::Window& window_;
    VulkanOptions options_;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    std::uint32_t queue_family_ = 0;
    std::string adapter_name_;
    float max_anisotropy_ = 1.0f;
    VkBool32 anisotropy_supported_ = VK_FALSE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = nullptr;

    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchain_format_ = VK_FORMAT_UNDEFINED;
    VkExtent2D extent_{};
    std::vector<VkImage> swapchain_images_;
    std::vector<VkImageView> swapchain_views_;
    std::vector<VkSemaphore> render_finished_;
    VkImage depth_image_ = VK_NULL_HANDLE;
    VmaAllocation depth_allocation_ = nullptr;
    VkImageView depth_view_ = VK_NULL_HANDLE;
    bool swapchain_dirty_ = false;

    std::array<Frame, kFramesInFlight> frames_{};
    std::uint32_t frame_index_ = 0;
    std::uint64_t frame_counter_ = 0;
    std::uint32_t image_index_ = 0;
    bool in_frame_ = false;
    VulkanCommandList command_list_;

    bool capture_requested_ = false;
    Staging capture_;
    VkExtent2D capture_extent_{};
    VkFormat capture_format_ = VK_FORMAT_UNDEFINED;

    VkCommandPool upload_pool_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout shader_data_layout_ = VK_NULL_HANDLE;
    VkDescriptorPool shader_data_pool_ = VK_NULL_HANDLE;
    VkDeviceSize storage_alignment_ = 256;
    VkFence upload_fence_ = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> descriptor_pools_;
    std::vector<std::pair<std::uint64_t, std::function<void()>>> deletions_;
};

VulkanBuffer::~VulkanBuffer() {
    VmaAllocator a = device_.allocator();
    VkBuffer b = buffer_;
    VmaAllocation m = allocation_;
    device_.defer([a, b, m] { vmaDestroyBuffer(a, b, m); });
}

VulkanRenderTarget::~VulkanRenderTarget() {
    VkDevice d = device_.handle();
    VmaAllocator a = device_.allocator();
    VkImage i = depth_;
    VkImageView v = depth_view_;
    VkImageView av = attachment_view_;
    VmaAllocation m = depth_allocation_;
    device_.defer([d, a, i, v, av, m] {
        vkDestroyImageView(d, av, nullptr);
        vkDestroyImageView(d, v, nullptr);
        vmaDestroyImage(a, i, m);
    });
}

VulkanTexture::~VulkanTexture() {
    VkDevice d = device_.handle();
    VmaAllocator a = device_.allocator();
    VkImage i = image_;
    VkImageView v = view_;
    VmaAllocation m = allocation_;
    device_.defer([d, a, i, v, m] {
        vkDestroyImageView(d, v, nullptr);
        vmaDestroyImage(a, i, m);
    });
}

VulkanSampler::~VulkanSampler() {
    VkDevice d = device_.handle();
    VkSampler s = sampler_;
    device_.defer([d, s] { vkDestroySampler(d, s, nullptr); });
}

VulkanPipeline::~VulkanPipeline() {
    VkDevice d = device_.handle();
    VkPipeline p = pipeline_;
    VkPipelineLayout l = layout_;
    VkDescriptorSetLayout s = set_layout_;
    device_.defer([d, p, l, s] {
        vkDestroyPipeline(d, p, nullptr);
        vkDestroyPipelineLayout(d, l, nullptr);
        vkDestroyDescriptorSetLayout(d, s, nullptr);
    });
}

VulkanBindGroup::~VulkanBindGroup() {
    VkDevice d = device_.handle();
    VkDescriptorPool p = pool_;
    VkDescriptorSet s = set_;
    device_.defer([d, p, s] { vkFreeDescriptorSets(d, p, 1, &s); });
}

} // namespace

std::unique_ptr<Device> create_vulkan_device(platform::Window& window, const VulkanOptions& options) {
    return std::make_unique<VulkanDevice>(window, options);
}

} // namespace openbl::rhi
