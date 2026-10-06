add_subdirectory(formats)
add_subdirectory(vfs)

if(OPENBL_BUILD_VIEWER)
    add_subdirectory(platform)
    add_subdirectory(rhi)
    add_subdirectory(render)
endif()
