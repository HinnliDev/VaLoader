cmake_minimum_required(VERSION 3.22.1)
project(Valoader LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_library(valoader SHARED
    platform/JniBridge.cpp
    platform/SurfaceRenderer.cpp
    core/Memory.cpp
    game/UnrealReflection.cpp
    game/UnrealRuntime.cpp
    overlay/Menu.cpp
    overlay/SkeletonOverlay.cpp
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(valoader PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui
)

target_compile_definitions(valoader PRIVATE IMGUI_IMPL_OPENGL_ES3)
target_compile_options(valoader PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -Wconversion
    -Wno-unused-parameter
)

target_link_libraries(valoader PRIVATE
    android
    EGL
    GLESv3
    log
)
