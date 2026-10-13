add_executable(openbl-viewer main.cpp camera.cpp)
target_link_libraries(openbl-viewer PRIVATE openbl::render openbl::sim openbl::vfs openbl::platform openbl_glm openbl_options)

openbl_add_shader(openbl-viewer ${PROJECT_SOURCE_DIR}/shaders/textured.hlsl vert vs_main openbl_textured_vs)
openbl_add_shader(openbl-viewer ${PROJECT_SOURCE_DIR}/shaders/textured.hlsl frag ps_main openbl_textured_ps)

if(WIN32)
    # Keep a console for log output; SDL3 does not need SDL_main with a plain main().
    set_target_properties(openbl-viewer PROPERTIES WIN32_EXECUTABLE OFF)
endif()
