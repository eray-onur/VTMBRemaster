add_library(openbl_formats
    image_decode.cpp
    keyvalues.cpp
    path.cpp
    tth.cpp
    vmt.cpp
    vpk.cpp
    vtf.cpp)
add_library(openbl::formats ALIAS openbl_formats)

target_include_directories(openbl_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_formats PRIVATE openbl_options ZLIB::ZLIB)
