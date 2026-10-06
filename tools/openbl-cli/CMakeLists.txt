add_executable(openbl-cli main.cpp)
target_link_libraries(openbl-cli PRIVATE openbl::formats openbl::vfs openbl_options)
