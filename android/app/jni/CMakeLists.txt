cmake_minimum_required(VERSION 3.22)

project(SDL_gpu_xr_examples)

# Find Khronos OpenXR loader from Prefab (provided by the AAR dependency)
find_package(OpenXR REQUIRED CONFIG)

# SDL path is now one level deeper (SDL_gpu_xr_examples/android/app/jni -> SDL)
set(SDL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../../../../SDL")

# Add SDL as a subdirectory
add_subdirectory(${SDL_PATH} SDL_build)

# Add our VR test app
add_subdirectory(src)
