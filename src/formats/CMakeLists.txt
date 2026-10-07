add_library(openbl_formats
    ain.cpp
    bsp.cpp
    dlg.cpp
    expressions.cpp
    fnt.cpp
    image_decode.cpp
    image_read.cpp
    image_write.cpp
    keyvalues.cpp
    lip.cpp
    localization.cpp
    mdl.cpp
    path.cpp
    sav.cpp
    save_state.cpp
    phy.cpp
    text_encoding.cpp
    tth.cpp
    vcd.cpp
    vmt.cpp
    vpk.cpp
    vtf.cpp
    vtx.cpp
    zip.cpp)
add_library(openbl::formats ALIAS openbl_formats)

target_include_directories(openbl_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_formats PRIVATE openbl_options ZLIB::ZLIB)
