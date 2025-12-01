cmake_minimum_required(VERSION 4.0)
project(implot3d_mmd_renderer)

set(CMAKE_CXX_STANDARD 20)

add_subdirectory(3rd_party)

add_executable(implot3d_mmd_renderer main.cpp)
target_link_libraries(implot3d_mmd_renderer PRIVATE sokol mmd imgui implot3d)
