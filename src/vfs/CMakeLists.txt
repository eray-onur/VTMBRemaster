add_library(openbl_vfs
    game_filesystem.cpp)
add_library(openbl::vfs ALIAS openbl_vfs)

target_include_directories(openbl_vfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_vfs PUBLIC openbl::formats PRIVATE openbl_options)
