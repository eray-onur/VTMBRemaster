# Compiles HLSL shaders to SPIR-V headers at build time with glslang.
#
#   openbl_add_shader(<target> <file.hlsl> <stage> <entry point> <symbol>)
#
# Generates <symbol>.h in the target's binary directory, defining
# `const uint32_t <symbol>[]`, and adds that directory to the target's include path.
function(openbl_add_shader target source stage entry symbol)
    if(NOT OPENBL_BUILD_VIEWER)
        return()
    endif()
    set(out_dir ${CMAKE_CURRENT_BINARY_DIR}/shaders)
    set(output ${out_dir}/${symbol}.h)
    get_filename_component(source_abs ${source} ABSOLUTE)
    add_custom_command(
        OUTPUT ${output}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${out_dir}
        COMMAND $<TARGET_FILE:glslang-standalone> -V -D -S ${stage} -e ${entry} --target-env vulkan1.3
                --vn ${symbol} -o ${output} ${source_abs}
        DEPENDS ${source_abs} glslang-standalone
        COMMENT "Compiling shader ${source} (${stage}, ${entry})"
        VERBATIM)
    target_sources(${target} PRIVATE ${output})
    target_include_directories(${target} PRIVATE ${out_dir})
endfunction()
