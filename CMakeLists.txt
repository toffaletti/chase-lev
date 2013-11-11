project(chase_lev)
cmake_minimum_required(VERSION 2.8.11)
include(ExternalProject)
ExternalProject_Add(relacy_2_4
    HG_REPOSITORY https://code.google.com/p/relacy/
    CONFIGURE_COMMAND ""
    BUILD_COMMAND ""
    INSTALL_COMMAND ""
    # Disable update
    UPDATE_COMMAND ""
    )
ExternalProject_Get_Property(relacy_2_4 source_dir binary_dir)
include_directories(${source_dir})
set(CMAKE_CXX_FLAGS "-std=c++11 -ggdb")

add_executable(chase_lev chase_lev.cpp)
add_dependencies(chase_lev relacy_2_4)

add_executable(ws_deque ws_deque.cpp)
add_dependencies(ws_deque relacy_2_4)

add_executable(ws_deque2 ws_deque2.cpp)
add_dependencies(ws_deque2 relacy_2_4)
