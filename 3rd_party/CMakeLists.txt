# sokol
add_library(sokol INTERFACE)
target_include_directories(sokol INTERFACE sokol)

# libmmd
add_library(mmd INTERFACE)
target_include_directories(mmd INTERFACE libmmd/include)

# imgui
add_library(imgui
    imgui/imgui.cpp
    imgui/imgui_draw.cpp
    imgui/imgui_tables.cpp
    imgui/imgui_widgets.cpp
    imgui/imgui_demo.cpp)
target_include_directories(imgui PUBLIC imgui)

# implot3d
add_library(implot3d
    implot3d/implot3d.cpp
    implot3d/implot3d_demo.cpp
    implot3d/implot3d_items.cpp
    implot3d/implot3d_meshes.cpp)
target_include_directories(implot3d PUBLIC implot3d)
target_link_libraries(implot3d PRIVATE imgui)

# bullet
option(BUILD_BULLET2_DEMOS OFF)
option(BUILD_CPU_DEMOS OFF)
option(BUILD_EXTRAS OFF)
option(BUILD_OPENGL3_DEMOS OFF)
option(BUILD_UNIT_TESTS OFF)
add_subdirectory(bullet)
