add_library(openbl_particles
    particle_system.cpp)
add_library(openbl::particles ALIAS openbl_particles)

target_include_directories(openbl_particles PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_particles PUBLIC openbl::formats PRIVATE openbl_options)
