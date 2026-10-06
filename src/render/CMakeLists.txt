add_library(openbl_render
    material_system.cpp
    texture_upload.cpp
    world_renderer.cpp)
add_library(openbl::render ALIAS openbl_render)

target_include_directories(openbl_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_render PUBLIC openbl::formats openbl::vfs openbl::rhi PRIVATE openbl_options)

openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/world.hlsl vert vs_main openbl_world_vs)
openbl_add_shader(openbl_render ${PROJECT_SOURCE_DIR}/shaders/world.hlsl frag ps_main openbl_world_ps)
