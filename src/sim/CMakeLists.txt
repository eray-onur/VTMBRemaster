add_library(openbl_sim
    classes.cpp
    collision.cpp
    effects.cpp
    entity.cpp
    logic.cpp
    movers.cpp
    player.cpp
    triggers.cpp
    world.cpp)
add_library(openbl::sim ALIAS openbl_sim)

target_include_directories(openbl_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(openbl_sim PUBLIC openbl::formats PRIVATE openbl_options)
