cmake_minimum_required(VERSION 3.22)

project(testgpu_spinning_cube_xr)

# Find SDL3 (built by parent CMakeLists)
if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()

# Use SDL's testgpu_spinning_cube_xr - simpler test without input API
set(SDL_TEST_PATH "${SDL_PATH}/test")

# Build SDL's XR test as a shared library called "main" (required by SDL Android)
add_library(main SHARED
    ${SDL_TEST_PATH}/testgpu_spinning_cube_xr.c
)

# Include paths for shader headers and OpenXR
target_include_directories(main PRIVATE 
    ${SDL_TEST_PATH}
    ${SDL_PATH}/src/video/khronos
)

target_link_libraries(main PRIVATE SDL3::SDL3)
