add_library(openbl_render texture_upload.cpp)
add_library(openbl::render ALIAS openbl_render)

target_include_directories(openbl_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_render PUBLIC openbl::formats openbl::rhi PRIVATE openbl_options)
