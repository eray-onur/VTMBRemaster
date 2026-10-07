#pragma once

// Rendering hardware interface: the thin layer between the renderer and the
// graphics API. Backends: Vulkan (vulkan.hpp); Direct3D 12 later. Concepts map
// directly onto both APIs: bind groups are descriptor sets / descriptor tables,
// push constants are push constants / root constants.

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace openbl::rhi {

enum class Format : std::uint8_t {
    Undefined,
    RGBA8_UNorm,
    RGBA8_sRGB,
    BGRA8_UNorm,
    BGRA8_sRGB,
    BC1_UNorm,
    BC1_sRGB,
    BC2_UNorm,
    BC2_sRGB,
    BC3_UNorm,
    BC3_sRGB,
    R32_Float,
    RG32_Float,
    RGB32_Float,
    RGBA32_Float,
    RGBA16_Float,
    RGBA16_UInt, // Vertex attribute only (e.g. bone indices).
    RGBA8_UInt,  // Vertex attribute only (e.g. light style indices).
    D32_Float,
};

bool format_is_block_compressed(Format format);
// Bytes for one mip level of the given size.
std::size_t format_image_size(Format format, std::uint32_t width, std::uint32_t height);

enum class BufferUsage : std::uint8_t { Vertex, Index };

struct BufferDesc {
    std::size_t size = 0;
    BufferUsage usage = BufferUsage::Vertex;
};

class Buffer {
public:
    virtual ~Buffer() = default;
    virtual std::size_t size() const = 0;
};

struct TextureDesc {
    std::uint32_t width = 1;
    std::uint32_t height = 1;
    std::uint32_t mip_levels = 1;
    Format format = Format::RGBA8_UNorm;
};

class Texture {
public:
    virtual ~Texture() = default;
    virtual const TextureDesc& desc() const = 0;
};

enum class Filter : std::uint8_t { Nearest, Linear };
enum class AddressMode : std::uint8_t { Repeat, Clamp };

struct SamplerDesc {
    Filter filter = Filter::Linear;
    AddressMode address = AddressMode::Repeat;
    float max_anisotropy = 16.0f; // 1 disables anisotropic filtering.
};

class Sampler {
public:
    virtual ~Sampler() = default;
};

// Compiled shader code for each backend; a backend uses the field it needs.
struct ShaderCode {
    std::span<const std::uint32_t> spirv;
};

struct VertexAttribute {
    std::uint32_t location = 0;
    Format format = Format::RGB32_Float;
    std::uint32_t offset = 0;
};

enum class CullMode : std::uint8_t { None, Back, Front };
// Modulate: destination * source * 2 (colour only; destination alpha kept).
enum class BlendMode : std::uint8_t { Opaque, Alpha, Additive, Modulate };

// Resource layout shared by all pipelines: set/table 0 holds one sampler at
// binding 0 and `texture_count` sampled 2D textures at bindings 1..N, visible
// to the pixel shader. Push constants are visible to all stages. Pipelines
// with `shader_data` also read per-draw data (CommandList::set_shader_data)
// as a read-only structured buffer at set/space 1, binding 0, in all stages.

// Largest block accepted by CommandList::set_shader_data.
constexpr std::size_t kMaxShaderDataSize = 64 * 1024;
struct PipelineDesc {
    ShaderCode vertex;
    ShaderCode pixel;
    std::string vertex_entry = "vs_main";
    std::string pixel_entry = "ps_main";
    std::uint32_t vertex_stride = 0;
    std::vector<VertexAttribute> attributes;
    std::uint32_t texture_count = 1;
    std::uint32_t push_constant_size = 64;
    bool shader_data = false;
    CullMode cull = CullMode::Back;
    bool front_counter_clockwise = false;
    bool depth_test = true;
    bool depth_write = true;
    BlendMode blend = BlendMode::Opaque;
};

class Pipeline {
public:
    virtual ~Pipeline() = default;
};

// A sampler and textures bound together for a pipeline's layout.
class BindGroup {
public:
    virtual ~BindGroup() = default;
};

class CommandList {
public:
    virtual ~CommandList() = default;
    virtual void set_pipeline(const Pipeline& pipeline) = 0;
    virtual void set_bind_group(const BindGroup& group) = 0;
    virtual void set_vertex_buffer(const Buffer& buffer, std::size_t offset = 0) = 0;
    virtual void set_index_buffer(const Buffer& buffer, std::size_t offset = 0) = 0; // 32-bit indices
    virtual void push_constants(const void* data, std::uint32_t size) = 0;
    // Copies `size` bytes (at most kMaxShaderDataSize) into per-frame memory and
    // binds them for the following draws. The bound pipeline must use shader_data.
    virtual void set_shader_data(const void* data, std::size_t size) = 0;
    virtual void draw_indexed(std::uint32_t index_count, std::uint32_t first_index = 0, std::int32_t vertex_offset = 0) = 0;
};

struct FrameDesc {
    float clear_color[4] = {0.05f, 0.05f, 0.07f, 1.0f};
};

struct CapturedImage {
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgba; // Tightly packed, top row first.
};

struct DeviceInfo {
    std::string backend;
    std::string adapter;
};

class Device {
public:
    virtual ~Device() = default;

    virtual DeviceInfo info() const = 0;

    // Device-local buffer filled with `data` (which may be shorter than the buffer).
    virtual std::unique_ptr<Buffer> create_buffer(const BufferDesc& desc, std::span<const std::byte> data) = 0;
    // `mips` holds the data for each mip level, largest first; missing levels stay undefined.
    virtual std::unique_ptr<Texture> create_texture(const TextureDesc& desc,
                                                    std::span<const std::span<const std::byte>> mips) = 0;
    virtual std::unique_ptr<Sampler> create_sampler(const SamplerDesc& desc) = 0;
    virtual std::unique_ptr<Pipeline> create_pipeline(const PipelineDesc& desc) = 0;
    // `textures.size()` must equal the pipeline's texture_count.
    virtual std::unique_ptr<BindGroup> create_bind_group(const Pipeline& pipeline, const Sampler& sampler,
                                                         std::span<const Texture* const> textures) = 0;

    // Begins a frame rendering to the window. Returns nullptr when there is
    // nothing to render to (e.g. a minimized window); skip the frame then.
    virtual CommandList* begin_frame(const FrameDesc& desc) = 0;
    virtual void end_frame() = 0;
    // Size of the current back buffer in pixels.
    virtual std::uint32_t frame_width() const = 0;
    virtual std::uint32_t frame_height() const = 0;
    virtual Format color_format() const = 0;
    virtual Format depth_format() const = 0;

    // Screenshots: the next end_frame() copies the back buffer, and
    // take_capture() waits for that frame and returns the pixels.
    virtual void request_capture() = 0;
    virtual std::optional<CapturedImage> take_capture() = 0;

    virtual void wait_idle() = 0;
};

} // namespace openbl::rhi
