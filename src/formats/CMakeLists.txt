add_library(openbl_formats
    vpk.cpp
    path.cpp)
add_library(openbl::formats ALIAS openbl_formats)

target_include_directories(openbl_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_formats PRIVATE openbl_options)
