#pragma once

// 2D skybox: six textures (materials/skybox/<name>{rt,lf,bk,ft,up,dn}) on a
// cube centred on the camera, drawn behind everything else.

#include "openbl/render/material_system.hpp"
#include "openbl/rhi/rhi.hpp"

#include <array>
#include <memory>
#include <string>

namespace openbl::render {

class SkyRenderer {
public:
    // Throws std::runtime_error if none of the six faces can be found.
    SkyRenderer(rhi::Device& device, MaterialSystem& materials, const std::string& sky_name);
    ~SkyRenderer();

    // `rotation_projection` is projection * view with the translation removed (column-major).
    void draw(rhi::CommandList& cmd, const std::array<float, 16>& rotation_projection);

private:
    std::unique_ptr<rhi::Buffer> vertices_;
    std::unique_ptr<rhi::Buffer> indices_;
    std::unique_ptr<rhi::Pipeline> pipeline_;
    std::unique_ptr<rhi::Sampler> sampler_;
    std::array<std::unique_ptr<rhi::BindGroup>, 6> faces_;
};

} // namespace openbl::render
