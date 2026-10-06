add_library(openbl_rhi
    format.cpp
    vulkan/vulkan_device.cpp
    vulkan/vma_impl.cpp)
add_library(openbl::rhi ALIAS openbl_rhi)

target_include_directories(openbl_rhi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_rhi
    PUBLIC openbl::platform
    PRIVATE openbl_options volk openbl_vma)
# Vulkan functions come from volk; VMA must not link or fetch them itself.
target_compile_definitions(openbl_rhi PRIVATE VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=0)

if(MSVC)
    set_source_files_properties(vulkan/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(vulkan/vma_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
