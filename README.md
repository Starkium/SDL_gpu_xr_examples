# SDL_gpu_xr_examples

Cross-platform XR (VR/AR) examples using SDL3's GPU API with OpenXR.
Supports desktop VR (SteamVR, Oculus Link) and standalone Quest headsets.

## Features

- SDL3 GPU API with Vulkan backend
- OpenXR stereo rendering
- Cross-platform: Desktop (Windows/Linux) and Android (Quest)
- SPIR-V shader loading
- Proper XR view/projection matrix math

## Examples

| Example | Description |
|---------|-------------|
| SpinningCubes | Multiple colored cubes spinning in VR space |
| *(more coming)* | BasicVr, VrGallery, etc. |

## Project Structure

```
SDL_gpu_xr_examples/
├── examples/
│   └── SpinningCubes/
│       └── main.c            # Spinning cubes VR demo
├── shaders/                  # SPIR-V shaders
├── android/                  # Android/Quest build
│   ├── app/
│   │   ├── build.gradle
│   │   └── jni/CMakeLists.txt
│   └── build.gradle
├── CMakeLists.txt            # Desktop build
└── README.md
```

## Building

### Prerequisites

**Desktop:**
- CMake 3.16+
- SDL3 (with OpenXR support)
- OpenXR SDK/runtime (SteamVR, Oculus, etc.)

**Android/Quest:**
- Android SDK (API 35+)
- Android NDK (27.0.12077973+)
- JDK 11+ (tested with JDK 19)
- Meta Quest in developer mode

### Desktop Build

```bash
mkdir build && cd build
cmake .. -DSDL3_DIR=/path/to/SDL
cmake --build .
./SpinningCubes
```

### Android/Quest Build

Requires sibling SDL directory:
```
parent/
├── SDL/                      # Starkium/SDL openxr branch
├── SDL_gpu_examples/         # For shader assets
└── SDL_gpu_xr_examples/      # This project
```

```powershell
cd android
$env:JAVA_HOME = "C:\Program Files\Java\jdk-19"
.\gradlew installDebug
```

Launch on Quest:
```bash
adb shell am start -n com.sdl.xr.examples/org.libsdl.app.SDLActivity
```

## Android OpenXR Loader

Uses the **Khronos OpenXR loader** via Maven:
```gradle
implementation 'org.khronos.openxr:openxr_loader_for_android:1.1.54'
```

> **Note:** Do NOT use Meta's forwardloader - it has issues with pre-instance OpenXR calls.

## Related Links

- [SDL PR #14837](https://github.com/libsdl-org/SDL/pull/14837) - GPU OpenXR integration
- [SDL_gpu_examples](https://github.com/TheSpydog/SDL_gpu_examples) - Non-XR GPU examples
- Based on original work by Beyley in [SDL PR #11601](https://github.com/libsdl-org/SDL/pull/11601)

## License

zlib license (same as SDL)
