add_library(openbl_render
    character_renderer.cpp
    detail_prop_renderer.cpp
    light_styles.cpp
    material_system.cpp
    particle_renderer.cpp
    prop_lighting.cpp
    prop_renderer.cpp
    rope_renderer.cpp
    sky_renderer.cpp
    sprite_renderer.cpp
    texture_upload.cpp
    world_renderer.cpp)
add_library(openbl::render ALIAS openbl_render)

target_include_directories(openbl_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_render PUBLIC openbl::formats openbl::vfs openbl::anim openbl::particles openbl::rhi PRIVATE openbl_options openbl_glm)

openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/world.hlsl vert vs_main openbl_world_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/world.hlsl frag ps_main openbl_world_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/detail.hlsl vert vs_main openbl_detail_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/detail.hlsl frag ps_main openbl_detail_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/prop.hlsl vert vs_main openbl_prop_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/prop.hlsl frag ps_main openbl_prop_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/textured.hlsl vert vs_main openbl_textured_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/textured.hlsl frag ps_main openbl_textured_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/water.hlsl vert vs_main openbl_water_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/water.hlsl frag ps_main openbl_water_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/skinned.hlsl vert vs_main openbl_skinned_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/skinned.hlsl frag ps_main openbl_skinned_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/particle.hlsl vert vs_main openbl_particle_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/particle.hlsl frag ps_main openbl_particle_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/rope.hlsl vert vs_main openbl_rope_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/rope.hlsl frag ps_main openbl_rope_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/sprite.hlsl vert vs_main openbl_sprite_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/sprite.hlsl frag ps_main openbl_sprite_ps)
