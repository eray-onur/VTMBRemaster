include(FetchContent)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
    SYSTEM)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

add_executable(openbl_tests
    main.cpp
    test_ain.cpp
    test_bsp.cpp
    test_dlg.cpp
    test_expressions.cpp
    test_fnt.cpp
    test_path.cpp
    test_phy.cpp
    test_sav.cpp
    test_vpk.cpp
    test_game_filesystem.cpp
    test_image_decode.cpp
    test_image_io.cpp
    test_keyvalues.cpp
    test_lip.cpp
    test_mdl.cpp
    test_text_data.cpp
    test_tth.cpp
    test_vcd.cpp
    test_vmt.cpp
    test_vtf.cpp)
target_link_libraries(openbl_tests PRIVATE openbl::formats openbl::vfs doctest::doctest ZLIB::ZLIB openbl_options)
# Include real std headers instead of doctest's forward declarations, which newer MSVC rejects.
target_compile_definitions(openbl_tests PRIVATE DOCTEST_CONFIG_USE_STD_HEADERS)

# Tests that need real game data read OPENBL_GAME_DIR and are skipped when it is unset.
add_test(NAME openbl_tests COMMAND openbl_tests)
