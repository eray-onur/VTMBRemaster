add_library(openbl_anim
    animation.cpp
    cloth.cpp
    flex.cpp)
add_library(openbl::anim ALIAS openbl_anim)

target_include_directories(openbl_anim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_anim PUBLIC openbl::formats PRIVATE openbl_options)
