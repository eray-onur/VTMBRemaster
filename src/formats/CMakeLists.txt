add_library(openbl_formats
    ain.cpp
    bik.cpp
    bsp.cpp
    console_script.cpp
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
    mp3.cpp
    particle.cpp
    path.cpp
    sav.cpp
    save_state.cpp
    save_write.cpp
    script_text.cpp
    phy.cpp
    pickle.cpp
    text_encoding.cpp
    tth.cpp
    vcd.cpp
    vdata.cpp
    vmt.cpp
    vmt_proxies.cpp
    vpk.cpp
    vtf.cpp
    vtx.cpp
    wav.cpp
    zip.cpp)
add_library(openbl::formats ALIAS openbl_formats)

target_include_directories(openbl_formats PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_formats PRIVATE openbl_options ZLIB::ZLIB openbl_minimp3)
