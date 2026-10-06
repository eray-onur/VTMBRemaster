// openbl-viewer: renders game assets with the OpenBL renderer.
//
// Step 1 test scene: a textured cube, to prove the window, input, Vulkan
// backend and texture uploads end to end.

#include "camera.hpp"

#include "openbl/formats/image_write.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/platform/window.hpp"
#include "openbl/render/texture_upload.hpp"
#include "openbl/rhi/vulkan.hpp"
#include "openbl/vfs/game_filesystem.hpp"

#include "openbl_textured_ps.h"
#include "openbl_textured_vs.h"

#include <glm/gtc/matrix_transform.hpp>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace openbl;

namespace {

struct Options {
    std::optional<std::filesystem::path> game_dir;
    std::optional<std::filesystem::path> screenshot;
    int frames = 0; // With --screenshot: frames to render before capturing.
    bool validation = false;
    bool vsync = true;
};

Options parse_args(int argc, char** argv) {
    Options o;
    if (const char* env = std::getenv("OPENBL_GAME_DIR"); env && *env) {
        o.game_dir = env;
    }
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                throw std::runtime_error("missing value for " + a);
            }
            return argv[++i];
        };
        if (a == "--game") {
            o.game_dir = next();
        } else if (a == "--screenshot") {
            o.screenshot = next();
        } else if (a == "--frames") {
            o.frames = std::stoi(next());
        } else if (a == "--validation") {
            o.validation = true;
        } else if (a == "--no-vsync") {
            o.vsync = false;
        } else {
            throw std::runtime_error("unknown argument " + a +
                                     "\nusage: openbl-viewer [--game DIR] [--screenshot out.png [--frames N]] "
                                     "[--validation] [--no-vsync]");
        }
    }
    return o;
}

struct Vertex {
    float position[3];
    float uv[2];
};

// A 128-unit cube standing on z = 0.
void build_cube(std::vector<Vertex>& vertices, std::vector<std::uint32_t>& indices) {
    const glm::vec3 normals[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    for (const glm::vec3& n : normals) {
        const glm::vec3 up = std::abs(n.z) > 0.5f ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);
        const glm::vec3 right = glm::cross(up, n);
        const auto base = static_cast<std::uint32_t>(vertices.size());
        const float corners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
        for (const auto& c : corners) {
            const glm::vec3 p = (n + right * c[0] + up * c[1]) * 64.0f + glm::vec3(0, 0, 64);
            vertices.push_back({{p.x, p.y, p.z}, {(c[0] + 1) * 0.5f, 1.0f - (c[1] + 1) * 0.5f}});
        }
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    }
}

std::unique_ptr<rhi::Texture> load_game_texture(rhi::Device& device, const vfs::GameFileSystem& gfs,
                                                const std::string& path) {
    const auto tth = gfs.read(path + ".tth");
    if (!tth) {
        return nullptr;
    }
    const auto ttz = gfs.read(path + ".ttz");
    auto assembled = formats::assemble_vtf(*tth, ttz ? std::optional(std::span<const std::byte>(*ttz)) : std::nullopt);
    return render::upload_vtf(device, formats::VtfTexture::parse(std::move(assembled.data)));
}

int run(const Options& options) {
    platform::Window window({"OpenBL Viewer"});
    auto device = rhi::create_vulkan_device(window, {options.validation, options.vsync});
    const auto info = device->info();
    std::cout << "renderer: " << info.backend << " on " << info.adapter << "\n";
    window.set_title("OpenBL Viewer - " + info.adapter);

    {
        std::unique_ptr<rhi::Texture> texture;
        if (options.game_dir) {
            const vfs::GameFileSystem gfs(*options.game_dir);
            texture = load_game_texture(*device, gfs, "materials/brick/bradwllb");
        }
        if (!texture) {
            std::cout << "game data not available; using a checkerboard texture\n";
            texture = render::create_checker_texture(*device);
        }

        std::vector<Vertex> vertices;
        std::vector<std::uint32_t> indices;
        build_cube(vertices, indices);
        const auto vbuf = device->create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                                std::as_bytes(std::span(vertices)));
        const auto ibuf = device->create_buffer({indices.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                                std::as_bytes(std::span(indices)));

        rhi::PipelineDesc pd;
        pd.vertex.spirv = openbl_textured_vs;
        pd.pixel.spirv = openbl_textured_ps;
        pd.vertex_stride = sizeof(Vertex);
        pd.attributes = {{0, rhi::Format::RGB32_Float, 0}, {1, rhi::Format::RG32_Float, 12}};
        pd.cull = rhi::CullMode::None;
        const auto pipeline = device->create_pipeline(pd);
        const auto sampler = device->create_sampler({});
        const rhi::Texture* textures[] = {texture.get()};
        const auto bind_group = device->create_bind_group(*pipeline, *sampler, textures);

        viewer::Camera camera;
        camera.position = {-260.0f, -140.0f, 180.0f};
        camera.yaw = 28.0f;
        camera.pitch = -22.0f;

        auto last = std::chrono::steady_clock::now();
        int frame = 0;
        while (window.poll_events()) {
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            if (window.input().pressed(platform::Key::Escape)) {
                break;
            }
            camera.update(window.input(), dt);

            const bool capture = options.screenshot && frame == options.frames;
            if (capture) {
                device->request_capture();
            }
            rhi::CommandList* cmd = device->begin_frame({});
            if (!cmd) {
                continue;
            }
            const float aspect = static_cast<float>(device->frame_width()) / static_cast<float>(device->frame_height());
            const glm::mat4 mvp = camera.projection(aspect) * camera.view();
            cmd->set_pipeline(*pipeline);
            cmd->set_bind_group(*bind_group);
            cmd->set_vertex_buffer(*vbuf);
            cmd->set_index_buffer(*ibuf);
            cmd->push_constants(&mvp, sizeof(mvp));
            cmd->draw_indexed(static_cast<std::uint32_t>(indices.size()));
            device->end_frame();

            if (capture) {
                const auto image = device->take_capture();
                if (!image) {
                    throw std::runtime_error("capture failed");
                }
                formats::write_png(*options.screenshot, image->rgba, image->width, image->height);
                std::cout << "wrote " << options.screenshot->string() << " (" << image->width << "x" << image->height
                          << ")\n";
                break;
            }
            ++frame;
        }
        device->wait_idle();
    } // GPU resources are released before the device.
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_args(argc, argv));
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
