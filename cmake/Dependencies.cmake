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

# minimp3 (CC0, public domain): header-only MP3 decoder for the game's voice lines.
FetchContent_Declare(minimp3
    GIT_REPOSITORY https://github.com/lieff/minimp3.git
    GIT_TAG ea99364f61c14656440e8d77e9c233ccf3124633
    SOURCE_SUBDIR openbl-no-cmake)
FetchContent_MakeAvailable(minimp3)
add_library(openbl_minimp3 INTERFACE)
target_include_directories(openbl_minimp3 SYSTEM INTERFACE ${minimp3_SOURCE_DIR})

if(NOT OPENBL_BUILD_VIEWER)
    return()
endif()

# --- Renderer and viewer dependencies (all GPLv3-compatible licenses) ---

# SDL3 (zlib license): window, input, Vulkan surface creation.
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG release-3.4.18
    GIT_SHALLOW TRUE
    SYSTEM)

# Vulkan headers (Apache-2.0) and volk (MIT) to load Vulkan at runtime; no SDK needed.
FetchContent_Declare(VulkanHeaders
    GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers.git
    GIT_TAG vulkan-sdk-1.4.363.0
    GIT_SHALLOW TRUE
    SYSTEM)
FetchContent_Declare(volk
    GIT_REPOSITORY https://github.com/zeux/volk.git
    GIT_TAG vulkan-sdk-1.4.363.0
    GIT_SHALLOW TRUE
    SYSTEM)

# Header-only libraries; their own CMake projects are skipped.
FetchContent_Declare(vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG v3.4.0
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR openbl-no-cmake)
FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.3
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR openbl-no-cmake)

# glslang (BSD/Apache): used only at build time to compile HLSL shaders to SPIR-V.
set(ENABLE_OPT OFF CACHE BOOL "" FORCE)
set(GLSLANG_TESTS OFF CACHE BOOL "" FORCE)
set(GLSLANG_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(ENABLE_SPVREMAPPER OFF CACHE BOOL "" FORCE)
set(ENABLE_HLSL ON CACHE BOOL "" FORCE)
FetchContent_Declare(glslang
    GIT_REPOSITORY https://github.com/KhronosGroup/glslang.git
    GIT_TAG vulkan-sdk-1.4.363.0
    GIT_SHALLOW TRUE
    SYSTEM)

FetchContent_MakeAvailable(SDL3 VulkanHeaders volk vma glm glslang)

add_library(openbl_vma INTERFACE)
target_include_directories(openbl_vma SYSTEM INTERFACE ${vma_SOURCE_DIR}/include)

add_library(openbl_glm INTERFACE)
target_include_directories(openbl_glm SYSTEM INTERFACE ${glm_SOURCE_DIR})
target_compile_definitions(openbl_glm INTERFACE GLM_FORCE_DEPTH_ZERO_TO_ONE GLM_FORCE_RADIANS GLM_ENABLE_EXPERIMENTAL)
