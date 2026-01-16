cmake_minimum_required(VERSION 3.16)
project(SDL_gpu_xr_examples)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Options
option(SDL_GPU_OPENXR "Enable OpenXR support" ON)

# Find SDL3 (assumes SDL is built and installed, or use add_subdirectory)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../SDL/CMakeLists.txt")
    # SDL is adjacent - use as subproject
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../SDL ${CMAKE_BINARY_DIR}/SDL_build)
else()
    # SDL installed system-wide
    find_package(SDL3 REQUIRED CONFIG)
endif()

# Find OpenXR (optional, but required for XR examples)
find_package(OpenXR CONFIG)

# Content directory (local shaders)
set(CONTENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Content")

# Spinning Cubes XR Example
add_executable(SpinningCubes
    examples/SpinningCubes/main.c
)

target_link_libraries(SpinningCubes PRIVATE SDL3::SDL3)

# Include OpenXR headers from SDL
target_include_directories(SpinningCubes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../SDL/src/video/khronos
)

# Copy Content folder for runtime shader access
add_custom_command(TARGET SpinningCubes POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CONTENT_DIR} $<TARGET_FILE_DIR:SpinningCubes>/Content
    COMMENT "Copying shader Content to build directory"
)

# Installation
install(TARGETS SpinningCubes
    RUNTIME DESTINATION bin
)
