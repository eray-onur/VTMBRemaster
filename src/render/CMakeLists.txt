add_library(openbl_render
    light_probe.cpp
    material_system.cpp
    prop_renderer.cpp
    sky_renderer.cpp
    texture_upload.cpp
    world_renderer.cpp)
add_library(openbl::render ALIAS openbl_render)

target_include_directories(openbl_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_render PUBLIC openbl::formats openbl::vfs openbl::rhi PRIVATE openbl_options openbl_glm)

openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/world.hlsl vert vs_main openbl_world_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/world.hlsl frag ps_main openbl_world_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/prop.hlsl vert vs_main openbl_prop_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/prop.hlsl frag ps_main openbl_prop_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/textured.hlsl vert vs_main openbl_textured_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/textured.hlsl frag ps_main openbl_textured_ps)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/water.hlsl vert vs_main openbl_water_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/water.hlsl frag ps_main openbl_water_ps)
