cmake_minimum_required(VERSION 3.22)

project(SpinningCubes)

# Find SDL3 (built by parent CMakeLists)
if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 CONFIG REQUIRED)
endif()

# Build our VR test as a shared library called "main" (required by SDL Android)
add_library(main SHARED
    ${EXAMPLES_PATH}/SpinningCubes/main.c
)

# Include path for OpenXR headers from SDL
target_include_directories(main PRIVATE 
    ${SDL_PATH}/src/video/khronos
)

target_link_libraries(main PRIVATE SDL3::SDL3)
