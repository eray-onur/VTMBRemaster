// openbl-viewer: renders game assets with the OpenBL renderer.
//
//   openbl-viewer --map sm_pier_1     fly through a map
//   openbl-viewer                     textured test cube

#include "camera.hpp"

#include "openbl/formats/bsp.hpp"
#include "openbl/formats/visibility.hpp"
#include "openbl/formats/image_write.hpp"
#include "openbl/formats/path.hpp"
#include "openbl/formats/tth.hpp"
#include "openbl/formats/vtf.hpp"
#include "openbl/formats/zip.hpp"
#include "openbl/platform/window.hpp"
#include "openbl/render/asset_source.hpp"
#include "openbl/render/character_renderer.hpp"
#include "openbl/render/material_system.hpp"
#include "openbl/render/particle_renderer.hpp"
#include "openbl/render/texture_upload.hpp"
#include "openbl/render/prop_renderer.hpp"
#include "openbl/render/sky_renderer.hpp"
#include "openbl/render/detail_prop_renderer.hpp"
#include "openbl/render/rope_renderer.hpp"
#include "openbl/render/sprite_renderer.hpp"
#include "openbl/render/world_renderer.hpp"
#include "openbl/rhi/vulkan.hpp"
#include "openbl/vfs/game_filesystem.hpp"

#include "openbl_textured_ps.h"
#include "openbl_textured_vs.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

using namespace openbl;

namespace {

constexpr const char* kUsage =
    "usage: openbl-viewer [--game DIR] [--mod NAME]... [--map NAME] [--show TARGETNAME]... [--npc-sequence NAME] [--pose NAME=VALUE]... [--flex CONTROLLER=VALUE]... [--pos X Y Z] [--angles PITCH YAW] [--fov DEG] [--size WxH]\n"
    "                     [--light-scale F] [--fullbright] [--no-fog] [--no-water] [--no-envmap] [--no-bump] [--no-sprites] [--no-ropes] [--no-detail-props] [--no-cull] [--no-area-portals] [--no-prop-fade] [--no-cloth] [--no-shadows] [--no-light-styles] [--no-props] [--no-characters] [--screenshot out.png [--frames N]] [--time T] [--sequence N [--fps F]] [--shots FILE --out DIR]\n"
    "                     [--validation] [--no-vsync]";

struct Options {
    std::optional<std::filesystem::path> game_dir;
    std::vector<std::string> mods; // Mod folders mounted over the game, e.g. Unofficial_Patch.
    std::vector<std::string> shown; // Targetnames of StartHidden brush entities to draw anyway.
    render::CharacterOptions characters_options; // --npc-sequence, --pose.
    std::optional<std::string> map;
    std::optional<glm::vec3> position;
    std::optional<glm::vec2> angles; // pitch, yaw
    float light_scale = 1.0f;
    float fov = 75.0f;
    std::uint32_t width = 1600;
    std::uint32_t height = 900;
    bool fullbright = false;
    bool props = true;
    bool light_styles = true;
    bool fog = true; // --no-fog: ignore the map's distance fog.
    bool water = true; // --no-water: no reflection and refraction passes (water shows its fog colour).
    bool envmaps = true; // --no-envmap: no environment-map reflections.
    bool bumpmaps = true; // --no-bump: normal maps ignored (world: flat lightmap; models: vertex lighting).
    bool sprites = true; // --no-sprites: no env_sprite glows and cones.
    bool ropes = true;   // --no-ropes: no keyframe_rope / move_rope cables.
    bool detail_props = true; // --no-detail-props: no grass and debris from the detail-prop lump.
    bool cull = true;    // --no-cull: draw the whole map in every view.
    bool area_portals = true; // --no-area-portals: every area portal open (closed doors do not hide areas).
    bool prop_fade = true; // --no-prop-fade: static props never fade with distance.
    bool characters = true;
    std::optional<std::filesystem::path> screenshot;
    std::optional<std::filesystem::path> shots;     // Shot list: "name x y z pitch yaw" per line.
    std::filesystem::path shots_dir = ".";
    int frames = 0; // With --screenshot: frames to render before capturing.
    std::optional<float> time; // Fixed scene clock in seconds (animated materials, light styles).
    // With --screenshot: capture this many consecutive frames, `sequence_fps`
    // apart in scene time starting at --time (or 0), as out_000.png, out_001.png, ...
    int sequence = 0;
    float sequence_fps = 30.0f;
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
        } else if (a == "--mod") {
            o.mods.push_back(next());
        } else if (a == "--show") {
            o.shown.push_back(next());
            o.characters_options.shown.push_back(o.shown.back());
        } else if (a == "--flex") {
            const std::string spec = next();
            const auto eq = spec.find('=');
            o.characters_options.flex_controllers.emplace_back(spec.substr(0, eq),
                                                              eq == std::string::npos ? 1.0f : std::stof(spec.substr(eq + 1)));
        } else if (a == "--npc-sequence") {
            o.characters_options.sequence = next();
        } else if (a == "--pose") {
            const std::string spec = next();
            const auto eq = spec.find('=');
            if (eq == std::string::npos) {
                throw std::runtime_error("--pose expects NAME=VALUE");
            }
            o.characters_options.pose_parameters.emplace_back(spec.substr(0, eq), std::stof(spec.substr(eq + 1)));
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
        } else if (a == "--fov") {
            o.fov = std::stof(next());
        } else if (a == "--size") {
            const std::string size = next();
            const auto x = size.find('x');
            if (x == std::string::npos) {
                throw std::runtime_error("--size expects WIDTHxHEIGHT");
            }
            o.width = static_cast<std::uint32_t>(std::stoul(size.substr(0, x)));
            o.height = static_cast<std::uint32_t>(std::stoul(size.substr(x + 1)));
        } else if (a == "--light-scale") {
            o.light_scale = std::stof(next());
        } else if (a == "--fullbright") {
            o.fullbright = true;
        } else if (a == "--no-fog") {
            o.fog = false;
        } else if (a == "--no-cloth") {
            o.characters_options.cloth = false;
        } else if (a == "--no-shadows") {
            o.characters_options.shadows = false;
        } else if (a == "--no-sprites") {
            o.sprites = false;
        } else if (a == "--no-ropes") {
            o.ropes = false;
        } else if (a == "--no-detail-props") {
            o.detail_props = false;
        } else if (a == "--no-cull") {
            o.cull = false;
        } else if (a == "--no-area-portals") {
            o.area_portals = false;
        } else if (a == "--no-prop-fade") {
            o.prop_fade = false;
        } else if (a == "--no-envmap") {
            o.envmaps = false;
        } else if (a == "--no-bump") {
            o.bumpmaps = false;
        } else if (a == "--no-water") {
            o.water = false;
        } else if (a == "--no-light-styles") {
            o.light_styles = false;
        } else if (a == "--no-props") {
            o.props = false;
        } else if (a == "--no-characters") {
            o.characters = false;
        } else if (a == "--screenshot") {
            o.screenshot = next();
        } else if (a == "--shots") {
            o.shots = next();
        } else if (a == "--out") {
            o.shots_dir = next();
        } else if (a == "--frames") {
            o.frames = std::stoi(next());
        } else if (a == "--time") {
            o.time = std::stof(next());
        } else if (a == "--sequence") {
            o.sequence = std::stoi(next());
        } else if (a == "--fps") {
            o.sequence_fps = std::stof(next());
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

// Moves a projection's near plane onto `plane` (view space; points with
// dot(plane, (p, 1)) >= 0 are kept) while keeping its far plane: oblique
// near-plane clipping (Lengyel), for depth 0..1. Used to draw only what is
// above or below a water surface.
glm::mat4 oblique_projection(glm::mat4 projection, const glm::vec4& plane) {
    const glm::mat4 inverse = glm::inverse(projection);
    const glm::vec4 clip_plane = glm::transpose(inverse) * plane;
    const glm::vec4 corner = inverse * glm::vec4(clip_plane.x >= 0 ? 1.0f : -1.0f, clip_plane.y >= 0 ? 1.0f : -1.0f, 1.0f, 1.0f);
    const glm::vec4 w_row(projection[0][3], projection[1][3], projection[2][3], projection[3][3]);
    const glm::vec4 z_row = plane * (glm::dot(w_row, corner) / glm::dot(plane, corner));
    for (int c = 0; c < 4; ++c) {
        projection[c][2] = z_row[c];
    }
    return projection;
}

// A world-space plane (normal, offset) in the space of `view`.
glm::vec4 plane_in_view(const glm::mat4& view, const glm::vec4& plane) {
    return glm::transpose(glm::inverse(view)) * plane;
}

// False if the box is certainly outside the view.
bool box_visible(const glm::mat4& view_projection, const std::array<float, 3>& lo, const std::array<float, 3>& hi) {
    std::array<int, 5> outside{}; // Left, right, top, bottom, behind.
    for (int i = 0; i < 8; ++i) {
        const glm::vec4 c = view_projection * glm::vec4(i & 1 ? hi[0] : lo[0], i & 2 ? hi[1] : lo[1], i & 4 ? hi[2] : lo[2], 1.0f);
        outside[0] += c.x < -c.w;
        outside[1] += c.x > c.w;
        outside[2] += c.y < -c.w;
        outside[3] += c.y > c.w;
        outside[4] += c.w <= 0.0f;
    }
    return std::find(outside.begin(), outside.end(), 8) == outside.end();
}

class Scene {
public:
    virtual ~Scene() = default;
    virtual void draw(rhi::CommandList& cmd, const glm::mat4& projection, const glm::mat4& view,
                      const viewer::Camera& camera, float time) = 0;
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

    void draw(rhi::CommandList& cmd, const glm::mat4& projection, const glm::mat4& view, const viewer::Camera&,
              float) override {
        const glm::mat4 vp = projection * view;
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
        : settings_{options.light_scale, options.fullbright, {}, 0.0f, options.light_styles}, device_(device) {
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
        // Cubemaps for environment maps (docs/RENDERING.md).
        materials_->set_map(std::filesystem::path(path).stem().string(), bsp_.cubemaps);
        world_ = std::make_unique<render::WorldRenderer>(device, *materials_, bsp_, options.shown);
        world_fog_ = render::Fog::from_entity(bsp_.entities.at(0));
        world_fog_.enabled = world_fog_.enabled && options.fog;
        water_targets_ = options.water;
        settings_.envmaps = options.envmaps;
        settings_.bumpmaps = options.bumpmaps;
        if (options.props) {
            props_ = std::make_unique<render::PropRenderer>(device, *materials_, *assets_, bsp_, options.shown);
        }
        if (options.characters) {
            characters_ = std::make_unique<render::CharacterRenderer>(device, *materials_, *assets_, bsp_, options.characters_options);
            const auto& c = characters_->stats();
            std::cout << "characters: " << c.instances << " instances of " << c.models << " models ("
                      << c.missing_models << " missing), " << c.without_sequence << " without an idle sequence, "
                      << c.with_cloth << " with cloth, " << c.with_envmap << " with envmaps, " << c.with_shadow
                      << " with shadows\n";
        }
        particles_ = std::make_unique<render::ParticleRenderer>(device, *assets_, bsp_, options.shown);
        sprites_ = std::make_unique<render::SpriteRenderer>(device, *materials_, bsp_, options.shown);
        draw_sprites_ = options.sprites;
        if (options.detail_props) {
            detail_props_ = std::make_unique<render::DetailPropRenderer>(device, *materials_, *assets_, bsp_);
            const auto& d = detail_props_->stats();
            std::cout << "detail props: " << d.props << " (" << d.triangles << " triangles, " << d.missing_models
                      << " missing models)\n";
        }
        if (options.ropes) {
            ropes_ = std::make_unique<render::RopeRenderer>(device, *materials_, bsp_);
            std::cout << "ropes: " << ropes_->stats().ropes << " (" << ropes_->stats().missing_materials
                      << " with a missing material)\n";
        }
        prop_fade_ = options.prop_fade;
        if (options.cull) {
            visibility_ = std::make_unique<formats::ViewVisibility>(bsp_);
            main_visibility_ = std::make_unique<formats::ViewVisibility>(bsp_);
            if (!options.area_portals) {
                for (const formats::BspAreaPortal& portal : bsp_.area_portals) {
                    visibility_->set_portal_open(portal.key, true);
                    main_visibility_->set_portal_open(portal.key, true);
                }
            }
        }
        std::cout << "sprites: " << sprites_->stats().sprites << " on, " << sprites_->stats().off << " off, "
                  << sprites_->stats().missing_materials << " with a missing material\n";
        std::cout << "particles: " << particles_->stats().systems << " active effects ("
                  << particles_->stats().missing_definitions << " with a missing definition)\n";
        if (auto sky = bsp_.entities.at(0).find("skyname"); sky != bsp_.entities.at(0).end()) {
            try {
                sky_ = std::make_unique<render::SkyRenderer>(device, *materials_, sky->second);
            } catch (const std::exception& e) {
                std::cerr << "sky: " << e.what() << "\n";
            }
        }
        const auto& s = world_->stats();
        std::cout << path << ": " << s.faces << " faces, " << s.triangles << " triangles, " << s.batches
                  << " batches, lightmap " << s.lightmap_width << "x" << s.lightmap_height << ", "
                  << s.missing_materials << " missing materials, " << materials_->missing_textures()
                  << " missing textures, " << s.skipped_faces << " faces skipped, " << s.hidden_entities
                  << " hidden brush entities, " << s.monitors << " monitors, " << s.skybox_faces << " skybox faces, "
                  << s.water_planes << " water planes\n";
        std::cout << "materials: " << materials_->proxy_materials() << " with proxies ("
                  << materials_->animated_materials() << " animated textures), " << materials_->params().size() - 1
                  << " with non-default colour or transform\n";
        if (props_) {
            const auto& p = props_->stats();
            std::cout << "props: " << p.instances << " instances of " << p.models << " models ("
                      << p.missing_models << " missing, " << p.triangles << " triangles), "
                      << p.generated_normals << " models with generated normals; " << bsp_.world_lights.size()
                      << " world lights; " << p.decals << " decals on props, " << p.unplaced_decals
                      << " decals placed nowhere\n";
        }
        std::cout << "loaded in " << std::chrono::duration<float>(std::chrono::steady_clock::now() - start).count()
                  << " s\n";
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

    void draw(rhi::CommandList& cmd, const glm::mat4& projection, const glm::mat4& view, const viewer::Camera& camera,
              float time) override {
        settings_.time = time;
        materials_->update({.time = time});
        settings_.material_params = &materials_->params();
        if (main_visibility_) {
            main_visibility_->update({camera.position.x, camera.position.y, camera.position.z},
                                     to_array(projection * view));
        }
        if (characters_) {
            characters_->update(time, main_visibility_.get());
        }
        particles_->update(time);
        // The static props' fade follows the main camera in every view, as in the original.
        if (props_ && prop_fade_) {
            props_->update_fades({camera.position.x, camera.position.y, camera.position.z});
        }
        // Character shadows' silhouettes, for the shadows the main view can see.
        if (characters_) {
            render::CharacterDrawSettings shadow_settings{settings_.light_scale, settings_.fullbright,
                                                          &materials_->params()};
            shadow_settings.visibility = main_visibility_.get();
            characters_->draw_shadow_textures(cmd, shadow_settings);
        }
        // Monitor pictures first: each camera's view into its monitor's render
        // target, without monitor screens (a target is never drawn while sampled).
        for (const render::MonitorView& monitor : world_->monitors()) {
            const glm::vec3 eye(monitor.origin[0], monitor.origin[1], monitor.origin[2]);
            const glm::vec3 at(monitor.look_at[0], monitor.look_at[1], monitor.look_at[2]);
            glm::mat4 monitor_projection = glm::perspective(glm::radians(monitor.fov), 1.0f, 2.0f, 65536.0f);
            monitor_projection[1][1] *= -1.0f; // Vulkan clip space has +Y down.
            const float black[4] = {0, 0, 0, 1};
            cmd.begin_render_target(*monitor.target, black);
            View v{monitor_projection, monitor_projection, glm::lookAt(eye, at, glm::vec3(0, 0, 1)), eye};
            v.monitors = v.water = false;
            draw_view(cmd, v);
            cmd.end_render_target();
        }
        settings_.water_targets = water_targets_ && draw_water_targets(cmd, projection, view, camera.position);
        View main{projection, projection, view, camera.position};
        // An eye under water sees everything through the water's fog.
        if (const auto water = underwater(camera.position)) {
            const render::RenderMaterial& m = *water->material;
            main.fog = render::Fog::underwater(m.fog_color, m.fog_start, m.fog_end, water->z, camera.position.z);
        }
        draw_view(cmd, main);
    }

private:
    struct View {
        glm::mat4 projection;      // For the sky and the 3D skybox.
        glm::mat4 clip_projection; // For the scene (oblique: clipped at a water surface).
        glm::mat4 view;
        glm::vec3 eye;
        bool monitors = true;
        bool water = true;
        bool sky = true;
        std::optional<render::Fog> fog; // Instead of the world's.
        std::optional<glm::vec3> pvs_eye; // Whose PVS limits what is drawn, if not the eye's.
    };

    // The water's reflection (the scene mirrored about the surface, only what
    // is above it, drawn mirrored left to right so the triangle winding is
    // unchanged) and refraction (the scene below the surface, in the water's
    // fog), drawn without water or monitors. As in the original, one pair of
    // targets serves all water: it is drawn for the nearest plane in view.
    // Returns false if no water is in view.
    // The water the eye is in, from its BSP leaf: the surface height and the
    // water material (its fog). Empty above water.
    struct Underwater {
        float z = 0;
        const render::RenderMaterial* material = nullptr;
    };
    std::optional<Underwater> underwater(const glm::vec3& eye) {
        if (bsp_.leafs.empty() || bsp_.nodes.empty()) {
            return std::nullopt;
        }
        const formats::BspLeaf& leaf = bsp_.leafs[bsp_.leaf_at({eye.x, eye.y, eye.z})];
        if (!(leaf.contents & formats::contents::Water) || leaf.water_data < 0 ||
            static_cast<std::size_t>(leaf.water_data) >= bsp_.leaf_water.size()) {
            return std::nullopt;
        }
        const formats::BspLeafWater& w = bsp_.leaf_water[static_cast<std::size_t>(leaf.water_data)];
        if (w.texinfo < 0 || static_cast<std::size_t>(w.texinfo) >= bsp_.texinfos.size()) {
            return std::nullopt;
        }
        const std::int32_t texdata = bsp_.texinfos[static_cast<std::size_t>(w.texinfo)].texdata;
        if (texdata < 0 || static_cast<std::size_t>(texdata) >= bsp_.texdatas.size()) {
            return std::nullopt;
        }
        const render::RenderMaterial& m = materials_->material(bsp_.texdatas[static_cast<std::size_t>(texdata)].material);
        if (!m.water || eye.z >= w.surface_z) {
            return std::nullopt;
        }
        return Underwater{w.surface_z, &m};
    }

    bool draw_water_targets(rhi::CommandList& cmd, const glm::mat4& projection, const glm::mat4& view,
                            const glm::vec3& eye) {
        world_->create_water_targets(device_.frame_width(), device_.frame_height());
        if (!world_->water_reflection()) {
            return false;
        }
        const glm::mat4 view_projection = projection * view;
        const render::WaterPlane* nearest = nullptr;
        float nearest_distance = 0.0f;
        for (const render::WaterPlane& plane : world_->water_planes()) {
            if (plane.skybox || !box_visible(view_projection, plane.mins, plane.maxs)) {
                continue;
            }
            float d2 = 0.0f;
            for (int k = 0; k < 3; ++k) {
                const float d = eye[k] - std::clamp(eye[k], plane.mins[k], plane.maxs[k]);
                d2 += d * d;
            }
            if (!nearest || d2 < nearest_distance) {
                nearest = &plane;
                nearest_distance = d2;
            }
        }
        if (!nearest) {
            return false;
        }
        const render::WaterPlane& plane = *nearest;
        const bool above = eye.z >= plane.z;
        const float black[4] = {0, 0, 0, 1};
        cmd.begin_render_target(*world_->water_reflection(), black);
        {
            // Mirror about z = plane.z; the flip of x restores the winding.
            // From above: what is above the surface. From below: what is below
            // it (seen at grazing angles), all of it under water.
            glm::mat4 mirror(1.0f);
            mirror[2][2] = -1.0f;
            mirror[3][2] = 2.0f * plane.z;
            const glm::mat4 flipped = glm::scale(glm::mat4(1.0f), glm::vec3(-1, 1, 1)) * projection;
            const glm::mat4 mirrored_view = view * mirror;
            const glm::vec4 keep = above ? glm::vec4(0, 0, 1, -plane.z) : glm::vec4(0, 0, -1, plane.z);
            View v{flipped, oblique_projection(flipped, plane_in_view(mirrored_view, keep)), mirrored_view,
                   glm::vec3(eye.x, eye.y, 2.0f * plane.z - eye.z)};
            v.monitors = v.water = false;
            v.sky = above;
            v.pvs_eye = eye; // The mirrored eye is often inside solid; the reflection shows what the eye sees.
            if (!above && plane.fog_material) {
                // The whole mirrored path is under water: a surface far above counts every point as below it.
                const render::RenderMaterial& m = *plane.fog_material;
                v.fog = render::Fog::underwater(m.fog_color, m.fog_start, m.fog_end, 1e9f, v.eye.z);
            }
            draw_view(cmd, v);
        }
        cmd.end_render_target();

        cmd.begin_render_target(*world_->water_refraction(), black);
        // What lies past the surface: below it from above, above it from below.
        // A unit of overlap keeps the shore line closed.
        const glm::vec4 keep = above ? glm::vec4(0, 0, -1, plane.z + 1.0f) : glm::vec4(0, 0, 1, 1.0f - plane.z);
        View v{projection, oblique_projection(projection, plane_in_view(view, keep)), view, eye};
        v.monitors = v.water = false;
        v.sky = !above;
        if (above && plane.fog_material) {
            const render::RenderMaterial& m = *plane.fog_material;
            v.fog = render::Fog::underwater(m.fog_color, m.fog_start, m.fog_end, plane.z, eye.z);
        }
        draw_view(cmd, v);
        cmd.end_render_target();
        return true;
    }

    void draw_view(rhi::CommandList& cmd, const View& v) {
        const glm::mat4& projection = v.projection;
        const glm::mat4& view = v.view;
        const glm::vec3& eye = v.eye;
        settings_.draw_monitors = v.monitors;
        settings_.draw_water = v.water;
        if (sky_ && v.sky) {
            sky_->draw(cmd, to_array(projection * glm::mat4(glm::mat3(view))));
        }
        render::PropDrawSettings prop_settings{settings_.light_scale, settings_.fullbright, &materials_->params()};
        // The 3D skybox: its area seen from the sky camera plus the eye / scale,
        // same orientation, with its own fog; then the depth is cleared so the
        // world draws over it.
        if (const auto& sky = world_->sky_camera(); sky && v.sky) {
            const glm::vec3 sky_eye = glm::vec3(sky->origin[0], sky->origin[1], sky->origin[2]) + eye / sky->scale;
            const auto sm = to_array(projection * glm::mat4(glm::mat3(view)) * glm::translate(glm::mat4(1.0f), -sky_eye));
            settings_.camera_position = {sky_eye.x, sky_eye.y, sky_eye.z};
            settings_.skybox = true;
            settings_.visibility = view_visibility({sky_eye.x, sky_eye.y, sky_eye.z}, sm);
            prop_settings.visibility = settings_.visibility;
            // Fog distances are world units: the skybox is 1/scale size.
            render::Fog sky_fog = sky->fog;
            sky_fog.start /= sky->scale;
            sky_fog.end /= sky->scale;
            settings_.fog = sky_fog;
            prop_settings.skybox = true;
            prop_settings.fog = sky_fog;
            prop_settings.eye = {sky_eye.x, sky_eye.y, sky_eye.z};
            prop_settings.envmaps = settings_.envmaps;
            prop_settings.bumpmaps = settings_.bumpmaps;
            world_->draw_opaque(cmd, sm, settings_);
            if (props_) {
                props_->draw_opaque(cmd, sm, prop_settings);
            }
            if (ropes_) {
                ropes_->draw_opaque(cmd, sm, {sky_eye.x, sky_eye.y, sky_eye.z}, sky_fog, true);
            }
            draw_translucent(cmd, sm, to_array(view), sky_eye, sky_fog, true, sky->scale, prop_settings, nullptr);
            cmd.clear_depth();
            settings_.skybox = false;
            prop_settings.skybox = false;
        }
        const render::Fog fog = v.fog.value_or(world_fog_);
        settings_.camera_position = {eye.x, eye.y, eye.z};
        settings_.fog = fog;
        prop_settings.fog = fog;
        prop_settings.eye = {eye.x, eye.y, eye.z};
        prop_settings.envmaps = settings_.envmaps;
        prop_settings.bumpmaps = settings_.bumpmaps;
        const auto m = to_array(v.clip_projection * view);
        const glm::vec3 pvs_eye = v.pvs_eye.value_or(eye);
        settings_.visibility = view_visibility({pvs_eye.x, pvs_eye.y, pvs_eye.z}, m);
        prop_settings.visibility = settings_.visibility;
        render::CharacterDrawSettings character_settings{settings_.light_scale, settings_.fullbright,
                                                         &materials_->params()};
        character_settings.visibility = settings_.visibility;
        character_settings.fog = fog;
        character_settings.eye = {eye.x, eye.y, eye.z};
        character_settings.envmaps = settings_.envmaps;
        character_settings.bumpmaps = settings_.bumpmaps;
        world_->draw_opaque(cmd, m, settings_);
        if (characters_) {
            characters_->draw_shadows(cmd, m, character_settings); // On the world only, under props and characters.
        }
        if (props_) {
            props_->draw_opaque(cmd, m, prop_settings);
        }
        if (characters_) {
            characters_->draw_opaque(cmd, m, character_settings);
        }
        if (ropes_) {
            ropes_->draw_opaque(cmd, m, {eye.x, eye.y, eye.z}, fog, false);
        }
        if (detail_props_) {
            detail_props_->draw(cmd, m, {eye.x, eye.y, eye.z}, fog, settings_.visibility);
        }
        draw_translucent(cmd, m, to_array(view), eye, fog, false, 1.0f, prop_settings, &character_settings);
    }

    // Water, then everything translucent in the original's order: back to
    // front through the BSP, world surfaces before the entities in each leaf
    // (render/translucent.hpp). settings_ must already be set for this view.
    void draw_translucent(rhi::CommandList& cmd, const std::array<float, 16>& m, const std::array<float, 16>& view,
                          const glm::vec3& eye, const render::Fog& fog, bool skybox, float world_scale,
                          const render::PropDrawSettings& prop_settings,
                          const render::CharacterDrawSettings* character_settings) {
        enum Source : std::uint32_t { kWorld, kProps, kCharacters, kSprites, kParticles };
        world_->draw_water_surfaces(cmd, m, settings_);
        draw_order_.update({eye.x, eye.y, eye.z});
        translucent_.clear();
        world_->collect_translucent(draw_order_, settings_, kWorld, translucent_);
        if (props_) {
            props_->collect_translucent(draw_order_, prop_settings, kProps, translucent_);
        }
        if (characters_ && character_settings) {
            characters_->collect_translucent(draw_order_, *character_settings, kCharacters, translucent_);
        }
        if (draw_sprites_) {
            sprites_->collect_translucent(draw_order_, {eye.x, eye.y, eye.z}, skybox, kSprites, translucent_,
                                          settings_.visibility);
        }
        particles_->collect_translucent(draw_order_, {eye.x, eye.y, eye.z}, skybox, kParticles, translucent_,
                                        settings_.visibility);
        render::sort_back_to_front(translucent_);
        world_->begin_translucent(cmd, m, settings_);
        if (props_) {
            props_->begin_translucent(cmd, m, prop_settings);
        }
        if (characters_ && character_settings) {
            characters_->begin_translucent(m, *character_settings);
        }
        if (draw_sprites_) {
            sprites_->begin_translucent(cmd, m, view, {eye.x, eye.y, eye.z}, settings_.time, fog, skybox, world_scale);
        }
        // Camera axes in world space are the rows of the view rotation.
        particles_->begin_translucent(m, {view[0], view[4], view[8]}, {view[1], view[5], view[9]}, {eye.x, eye.y, eye.z},
                                      fog);
        std::uint32_t last = UINT32_MAX;
        for (const render::TranslucentItem& item : translucent_) {
            const bool rebind = item.source != last;
            last = item.source;
            switch (item.source) {
            case kWorld:
                world_->draw_translucent_item(cmd, item.index, rebind);
                break;
            case kProps:
                props_->draw_translucent_item(cmd, item.index, rebind);
                break;
            case kCharacters:
                characters_->draw_translucent_item(cmd, item.index);
                break;
            case kSprites:
                sprites_->draw_translucent_item(cmd, item.index, rebind);
                break;
            case kParticles:
                particles_->draw_translucent_item(cmd, item.index);
                break;
            }
        }
    }

    render::WorldDrawSettings settings_;
    // What the view being drawn can see (null with --no-cull), and what the
    // main camera sees (for animating characters).
    std::unique_ptr<formats::ViewVisibility> visibility_, main_visibility_;
    const formats::ViewVisibility* view_visibility(const formats::Vec3& eye, const std::array<float, 16>& m) {
        if (!visibility_) {
            return nullptr;
        }
        visibility_->update(eye, m);
        return visibility_.get();
    }
    rhi::Device& device_;
    bool water_targets_ = true; // false: water without reflection and refraction (--no-water).
    render::Fog world_fog_; // From worldspawn.
    formats::BspFile bsp_;
    std::unique_ptr<formats::ZipArchive> pak_;
    std::unique_ptr<render::AssetSource> assets_;
    std::unique_ptr<render::MaterialSystem> materials_;
    std::unique_ptr<render::WorldRenderer> world_;
    std::unique_ptr<render::PropRenderer> props_;
    std::unique_ptr<render::SkyRenderer> sky_;
    std::unique_ptr<render::CharacterRenderer> characters_;
    std::unique_ptr<render::ParticleRenderer> particles_;
    std::unique_ptr<render::SpriteRenderer> sprites_;
    std::unique_ptr<render::RopeRenderer> ropes_;
    std::unique_ptr<render::DetailPropRenderer> detail_props_;
    bool draw_sprites_ = true; // false: --no-sprites.
    bool prop_fade_ = true;    // false: --no-prop-fade.
    formats::BspDrawOrder draw_order_{bsp_};
    std::vector<render::TranslucentItem> translucent_;
};

int run(const Options& options) {
    platform::Window window({"OpenBL Viewer", options.width, options.height});
    auto device = rhi::create_vulkan_device(window, {options.validation, options.vsync});
    const auto info = device->info();
    std::cout << "renderer: " << info.backend << " on " << info.adapter << "\n";

    {
        std::unique_ptr<vfs::GameFileSystem> gfs;
        if (options.game_dir) {
            gfs = std::make_unique<vfs::GameFileSystem>(*options.game_dir, options.mods);
        }
        viewer::Camera camera;
        // --fov follows the game's convention (default_fov 75): horizontal at a
        // 4:3 aspect, whatever the window shape. The camera takes the vertical
        // angle (59.86 degrees for 75), confirmed against original screenshots.
        camera.fov = glm::degrees(2.0f * std::atan(std::tan(glm::radians(options.fov) * 0.5f) * 0.75f));
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

        const auto start_time = std::chrono::steady_clock::now();
        struct Shot {
            std::string name;
            glm::vec3 position;
            float pitch, yaw;
        };
        std::vector<Shot> shots;
        if (options.shots) {
            std::ifstream list(*options.shots);
            if (!list) {
                throw std::runtime_error("cannot read shot list " + options.shots->string());
            }
            std::string line;
            while (std::getline(list, line)) {
                std::istringstream in(line);
                Shot s;
                if (line.empty() || line[0] == '#' || !(in >> s.name >> s.position.x >> s.position.y >> s.position.z >> s.pitch >> s.yaw)) {
                    continue;
                }
                shots.push_back(s);
            }
            std::filesystem::create_directories(options.shots_dir);
        }
        std::size_t shot_index = 0;
        int shot_frame = 0;
        constexpr int kShotSettleFrames = 3;

        auto last = start_time;
        int frame = 0;
        while (window.poll_events()) {
            const auto now = std::chrono::steady_clock::now();
            const float dt = std::chrono::duration<float>(now - last).count();
            last = now;
            if (window.input().pressed(platform::Key::Escape)) {
                break;
            }
            camera.update(window.input(), dt);
            bool shot_capture = false;
            if (shot_index < shots.size()) {
                const Shot& s = shots[shot_index];
                camera.position = s.position;
                camera.pitch = s.pitch;
                camera.yaw = s.yaw;
                shot_capture = ++shot_frame >= kShotSettleFrames;
            }

            const bool in_sequence = options.screenshot && options.sequence > 0 && frame >= options.frames;
            const bool capture =
                (options.screenshot && (in_sequence || (options.sequence == 0 && frame == options.frames))) || shot_capture;
            if (capture) {
                device->request_capture();
            }
            rhi::CommandList* cmd = device->begin_frame({});
            if (!cmd) {
                continue;
            }
            const float aspect = static_cast<float>(device->frame_width()) / static_cast<float>(device->frame_height());
            const float time =
                in_sequence ? options.time.value_or(0.0f) + static_cast<float>(frame - options.frames) / options.sequence_fps
                            : options.time.value_or(std::chrono::duration<float>(now - start_time).count());
            scene->draw(*cmd, camera.projection(aspect), camera.view(), camera, time);
            device->end_frame();

            if (capture) {
                const auto image = device->take_capture();
                if (!image) {
                    throw std::runtime_error("capture failed");
                }
                std::filesystem::path out =
                    shot_capture ? options.shots_dir / (shots[shot_index].name + ".png") : *options.screenshot;
                if (in_sequence) {
                    char index[16];
                    std::snprintf(index, sizeof(index), "_%03d", frame - options.frames);
                    out.replace_filename(out.stem().string() + index + out.extension().string());
                }
                formats::write_png(out, image->rgba, image->width, image->height);
                std::cout << "wrote " << out.string() << " (" << image->width << "x" << image->height << ") at pos "
                          << camera.position.x << " " << camera.position.y << " " << camera.position.z << " angles "
                          << camera.pitch << " " << camera.yaw << "\n";
                if (!shot_capture && (!in_sequence || frame + 1 >= options.frames + options.sequence)) {
                    break;
                }
                if (!shot_capture) {
                    ++frame;
                    continue;
                }
                shot_frame = 0;
                if (++shot_index == shots.size()) {
                    break;
                }
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
