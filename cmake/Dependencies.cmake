include(FetchContent)

# zlib: TTZ texture data is zlib-compressed.
option(OPENBL_USE_SYSTEM_ZLIB "Link the system zlib instead of building the pinned release" OFF)
if(OPENBL_USE_SYSTEM_ZLIB)
    find_package(ZLIB REQUIRED)
else()
    # SOURCE_SUBDIR points at a missing directory so zlib's own CMakeLists (which
    # predates CMake 3.5 and installs shared libs) is skipped; we build the sources ourselves.
    FetchContent_Declare(zlib
        URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
        URL_HASH SHA256=9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23
        SOURCE_SUBDIR openbl-no-cmake)
    FetchContent_MakeAvailable(zlib)

    add_library(openbl_zlib STATIC
        ${zlib_SOURCE_DIR}/adler32.c
        ${zlib_SOURCE_DIR}/compress.c
        ${zlib_SOURCE_DIR}/crc32.c
        ${zlib_SOURCE_DIR}/deflate.c
        ${zlib_SOURCE_DIR}/infback.c
        ${zlib_SOURCE_DIR}/inffast.c
        ${zlib_SOURCE_DIR}/inflate.c
        ${zlib_SOURCE_DIR}/inftrees.c
        ${zlib_SOURCE_DIR}/trees.c
        ${zlib_SOURCE_DIR}/uncompr.c
        ${zlib_SOURCE_DIR}/zutil.c)
    target_include_directories(openbl_zlib SYSTEM PUBLIC ${zlib_SOURCE_DIR})
    if(MSVC)
        target_compile_definitions(openbl_zlib PRIVATE _CRT_SECURE_NO_DEPRECATE _CRT_NONSTDC_NO_DEPRECATE)
        target_compile_options(openbl_zlib PRIVATE /W0)
    endif()
    add_library(ZLIB::ZLIB ALIAS openbl_zlib)
endif()
