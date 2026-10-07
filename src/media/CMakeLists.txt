add_library(openbl_media
    video.cpp)
add_library(openbl::media ALIAS openbl_media)

target_include_directories(openbl_media PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_media PRIVATE openbl_options openbl_ffmpeg)
