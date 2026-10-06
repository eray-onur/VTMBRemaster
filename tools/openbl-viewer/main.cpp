// openbl-viewer: renders game assets with the OpenBL renderer.
//
//   openbl-viewer --map sm_pier_1     fly through a map
//   openbl-viewer                     textured test cube

#include "camera.hpp"

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/image_write.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/formats/zip.hpp"
#include "openbl/platform/window.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/render/texture_upload.hpp"
#include "openbl/render/world_renderer.hpp"
#include "openbl/rhi/vulkan.hpp"
#include "openbl/vfs/game_filesystem.hpp"

#include "openbl_textured_ps.h"
#include "openbl_textured_vs.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace openbl;

namespace {

constexpr const char* kUsage =
    "usage: openbl-viewer [--game DIR] [--map NAME] [--pos X Y Z] [--angles PITCH YAW]\n"
    "                     [--light-scale F] [--fullbright] [--screenshot out.png [--frames N]]\n"
    "                     [--validation] [--no-vsync]";

struct Options {
    std::optional<std::filesystem::path> game_dir;
    std::optional<std::string> map;
    std::optional<glm::vec3> position;
    std::optional<glm::vec2> angles; // pitch, yaw
    float light_scale = 2.3f;
    bool fullbright = false;
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
                throw std::runtime_error("missing value for " + a + "\n" + kUsage);
            }
            return argv[++i];
        };
        if (a == "--game") {
            o.game_dir = next();
        } else if (a == "--map") {
            o.map = next();
        } else if (a == "--pos") {
            glm::vec3 p;
            p.x = std::stof(next());
            p.y = std::stof(next());
            p.z = std::stof(next());
            o.position = p;
        } else if (a == "--angles") {
            glm::vec2 v;
            v.x = std::stof(next());
            v.y = std::stof(next());
            o.angles = v;
        } else if (a == "--light-scale") {
            o.light_scale = std::stof(next());
        } else if (a == "--fullbright") {
            o.fullbright = true;
        } else if (a == "--screenshot") {
            o.screenshot = next();
        } else if (a == "--frames") {
            o.frames = std::stoi(next());
        } else if (a == "--validation") {
            o.validation = true;
        } else if (a == "--no-vsync") {
            o.vsync = false;
        } else {
            throw std::runtime_error("unknown argument " + a + "\n" + kUsage);
        }
    }
    if (o.map && !o.game_dir) {
        throw std::runtime_error("--map needs game data: pass --game DIR or set OPENBL_GAME_DIR");
    }
    return o;
}

std::array<float, 16> to_array(const glm::mat4& m) {
    std::array<float, 16> out;
    std::memcpy(out.data(), glm::value_ptr(m), sizeof(out));
    return out;
}

class Scene {
public:
    virtual ~Scene() = default;
    virtual void draw(rhi::CommandList& cmd, const glm::mat4& view_projection) = 0;
};

// --- Test cube -------------------------------------------------------------

class CubeScene final : public Scene {
public:
    CubeScene(rhi::Device& device, const vfs::GameFileSystem* gfs) {
        if (gfs) {
            const render::AssetSource assets(*gfs);
            if (auto tth = assets.read("materials/brick/bradwllb.tth")) {
                const auto ttz = assets.read("materials/brick/bradwllb.ttz");
                auto vtf = formats::assemble_vtf(*tth, ttz ? std::optional(std::span<const std::byte>(*ttz)) : std::nullopt);
                texture_ = render::upload_vtf(device, formats::VtfTexture::parse(std::move(vtf.data)));
            }
        }
        if (!texture_) {
            std::cout << "game data not available; using a checkerboard texture\n";
            texture_ = render::create_checker_texture(device);
        }
        std::vector<Vertex> vertices;
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
            indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        vbuf_ = device.create_buffer({vertices.size() * sizeof(Vertex), rhi::BufferUsage::Vertex},
                                     std::as_bytes(std::span(vertices)));
        ibuf_ = device.create_buffer({indices_.size() * sizeof(std::uint32_t), rhi::BufferUsage::Index},
                                     std::as_bytes(std::span(indices_)));
        rhi::PipelineDesc pd;
        pd.vertex.spirv = openbl_textured_vs;
        pd.pixel.spirv = openbl_textured_ps;
        pd.vertex_stride = sizeof(Vertex);
        pd.attributes = {{0, rhi::Format::RGB32_Float, 0}, {1, rhi::Format::RG32_Float, 12}};
        pd.cull = rhi::CullMode::None;
        pipeline_ = device.create_pipeline(pd);
        sampler_ = device.create_sampler({});
        const rhi::Texture* textures[] = {texture_.get()};
        bind_group_ = device.create_bind_group(*pipeline_, *sampler_, textures);
    }

    void draw(rhi::CommandList& cmd, const glm::mat4& vp) override {
        cmd.set_pipeline(*pipeline_);
        cmd.set_bind_group(*bind_group_);
        cmd.set_vertex_buffer(*vbuf_);
        cmd.set_index_buffer(*ibuf_);
        cmd.push_constants(&vp, sizeof(vp));
        cmd.draw_indexed(static_cast<std::uint32_t>(indices_.size()));
    }

private:
    struct Vertex {
        float position[3];
        float uv[2];
    };
    std::vector<std::uint32_t> indices_;
    std::unique_ptr<rhi::Texture> texture_;
    std::unique_ptr<rhi::Buffer> vbuf_, ibuf_;
    std::unique_ptr<rhi::Pipeline> pipeline_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::unique_ptr<rhi::BindGroup> bind_group_;
};

// --- Map --------------------------------------------------------------------

class MapScene final : public Scene {
public:
    MapScene(rhi::Device& device, const vfs::GameFileSystem& gfs, const std::string& name, const Options& options)
        : settings_{options.light_scale, options.fullbright} {
        std::string path = formats::normalize_game_path(name);
        if (!path.starts_with("maps/")) {
            path.insert(0, "maps/");
        }
        if (!path.ends_with(".bsp")) {
            path += ".bsp";
        }
        const auto data = gfs.read(path);
        if (!data) {
            throw std::runtime_error("map not found: " + path);
        }
        const auto start = std::chrono::steady_clock::now();
        bsp_ = formats::parse_bsp(*data);
        pak_ = std::make_unique<formats::ZipArchive>(bsp_.pakfile);
        assets_ = std::make_unique<render::AssetSource>(gfs, pak_.get());
        materials_ = std::make_unique<render::MaterialSystem>(device, *assets_);
        world_ = std::make_unique<render::WorldRenderer>(device, *materials_, bsp_);
        const auto& s = world_->stats();
        std::cout << path << ": " << s.faces << " faces, " << s.triangles << " triangles, " << s.batches
                  << " batches, lightmap " << s.lightmap_width << "x" << s.lightmap_height << ", "
                  << s.missing_materials << " missing materials, " << materials_->missing_textures()
                  << " missing textures, " << s.skipped_faces << " faces skipped; loaded in "
                  << std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count() << " s\n";
    }

    // Player start, or the middle of the world bounds.
    void place_camera(viewer::Camera& camera) const {
        for (const auto& e : bsp_.entities) {
            auto cls = e.find("classname");
            if (cls == e.end() || cls->second != "info_player_start") {
                continue;
            }
            if (auto o = e.find("origin"); o != e.end()) {
                std::istringstream(o->second) >> camera.position.x >> camera.position.y >> camera.position.z;
                camera.position.z += 64.0f; // eye height
            }
            if (auto a = e.find("angles"); a != e.end()) {
                std::istringstream(a->second) >> camera.pitch >> camera.yaw;
            }
            return;
        }
        const auto& world = bsp_.models.at(0);
        for (int k = 0; k < 3; ++k) {
            camera.position[k] = (world.mins[k] + world.maxs[k]) * 0.5f;
        }
    }

    void draw(rhi::CommandList& cmd, const glm::mat4& vp) override { world_->draw(cmd, to_array(vp), settings_); }

private:
    render::WorldDrawSettings settings_;
    formats::BspFile bsp_;
    std::unique_ptr<formats::ZipArchive> pak_;
    std::unique_ptr<render::AssetSource> assets_;
    std::unique_ptr<render::MaterialSystem> materials_;
    std::unique_ptr<render::WorldRenderer> world_;
};

int run(const Options& options) {
    platform::Window window({"OpenBL Viewer"});
    auto device = rhi::create_vulkan_device(window, {options.validation, options.vsync});
    const auto info = device->info();
    std::cout << "renderer: " << info.backend << " on " << info.adapter << "\n";

    {
        std::unique_ptr<vfs::GameFileSystem> gfs;
        if (options.game_dir) {
            gfs = std::make_unique<vfs::GameFileSystem>(*options.game_dir);
        }
        viewer::Camera camera;
        std::unique_ptr<Scene> scene;
        if (options.map) {
            auto map = std::make_unique<MapScene>(*device, *gfs, *options.map, options);
            map->place_camera(camera);
            scene = std::move(map);
            window.set_title("OpenBL Viewer - " + *options.map + " - " + info.adapter);
        } else {
            scene = std::make_unique<CubeScene>(*device, gfs.get());
            camera.position = {-260.0f, -140.0f, 180.0f};
            camera.yaw = 28.0f;
            camera.pitch = -22.0f;
            window.set_title("OpenBL Viewer - " + info.adapter);
        }
        if (options.position) {
            camera.position = *options.position;
        }
        if (options.angles) {
            camera.pitch = options.angles->x;
            camera.yaw = options.angles->y;
        }

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
            scene->draw(*cmd, camera.projection(aspect) * camera.view());
            device->end_frame();

            if (capture) {
                const auto image = device->take_capture();
                if (!image) {
                    throw std::runtime_error("capture failed");
                }
                formats::write_png(*options.screenshot, image->rgba, image->width, image->height);
                std::cout << "wrote " << options.screenshot->string() << " (" << image->width << "x" << image->height
                          << ") at pos " << camera.position.x << " " << camera.position.y << " " << camera.position.z
                          << " angles " << camera.pitch << " " << camera.yaw << "\n";
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
