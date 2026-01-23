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

# If not found, try our local build
if(NOT OpenXR_FOUND)
    set(OPENXR_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../OpenXR-SDK-Source")
    if(EXISTS "${OPENXR_SDK_DIR}/build/src/loader/Release/openxr_loader.lib")
        message(STATUS "Using local OpenXR SDK build")
        add_library(OpenXR::openxr_loader STATIC IMPORTED)
        set_target_properties(OpenXR::openxr_loader PROPERTIES
            IMPORTED_LOCATION "${OPENXR_SDK_DIR}/build/src/loader/Release/openxr_loader.lib"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENXR_SDK_DIR}/include"
        )
        set(OpenXR_FOUND TRUE)
    endif()
endif()

# Content directory (local shaders)
set(CONTENT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/Content")

# XR Input convenience library (experimental)
add_library(xr_input STATIC
    src/xr_input.c
    src/xr_input.h
)

target_include_directories(xr_input PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/../SDL/src/video/khronos
)

if(OpenXR_FOUND)
    target_link_libraries(xr_input PUBLIC OpenXR::openxr_loader)
else()
    # OpenXR headers from SDL, no loader (for header-only compile check)
    message(WARNING "OpenXR not found - xr_input will compile but not link fully")
endif()

# Spinning Cubes XR Example
add_executable(SpinningCubes
    examples/SpinningCubes/main.c
)

target_link_libraries(SpinningCubes PRIVATE SDL3::SDL3 xr_input)

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
