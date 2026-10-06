add_library(openbl_platform window.cpp)
add_library(openbl::platform ALIAS openbl_platform)

target_include_directories(openbl_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_platform PRIVATE openbl_options SDL3::SDL3-static Vulkan::Headers)
