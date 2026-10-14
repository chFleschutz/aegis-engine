# Aegis Engine

<p>
  <img alt="Language" src="https://img.shields.io/badge/Language-C%2B%2B23-blue?style=for-the-badge&logo=c%2B%2B">
  <img alt="Platform" src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-blue?style=for-the-badge">
  <a href="https://github.com/chFleschutz/aegis-engine/blob/main/LICENSE">
    <img alt="License" src="https://img.shields.io/badge/License-Apache%202.0-blue?style=for-the-badge">
  </a>
</p>

C++ render engine developed with Vulkan to explore low level graphics programming

###

###
![Screenshot 2025-03-20 095757](https://github.com/user-attachments/assets/b5d7ec34-6f00-4b99-b569-0e58ed10c57a)

> [!NOTE]
> *My original Master's Thesis version is archived in the [thesis branch](https://github.com/chFleschutz/aegis-engine/tree/thesis) or at the [thesis tag](https://github.com/chFleschutz/aegis-engine/releases/tag/thesis).*

# Features

- **Physically Based Rendering (PBR)** - Realistic lighting and materials, with image based lighting from HDR environments
- **GLTF & OBJ Loading** - Loading of custom 3D models
- **Immediate Mode Editor UI** - Editor tools using Dear ImGui and ImGuizmo
- **Entity Component System (ECS)** - Efficient object management using EnTT
- **Render Graph** - Passes declare the resources they read and write; the graph orders them and places the barriers
- **Bindless Resources** - Images and samplers in one descriptor heap, buffers through device addresses
- **Deferred Rendering** - Improved lighting performance
- **HDR Rendering and Tone Mapping** - Enables wider color range for more visible details
- **Physically Based Bloom** - Realistic glow effect around bright areas created using convolution
- **Mesh Shaders Pipeline** - Modern geometry rendering
- **GPU Driven Rendering** - Minimize CPU rendering overhead
- **Headless Rendering** - Offscreen rendering without a window, used by the GPU integration tests
  
## Prerequisites <a name="prerequisites"></a>

The engine runs on **Windows** and **Linux**. Before building ensure you have the following installed:
- [Vulkan SDK](https://vulkan.lunarg.com/) - Install version **1.4.328.1** (see the version note below)
- [CMake](https://cmake.org/) 3.28 or newer and [Ninja](https://ninja-build.org/)
- A C++23 compiler with C++20 modules support:
  - **Windows:** [Clang](https://releases.llvm.org/) with the MSVC standard library (the `windows-msvc-*` presets exist, but MSVC currently fails with internal compiler errors)
  - **Linux:** Clang with libstdc++ 15 (Ubuntu 24.04's libstdc++ 14.2 fails to compile the modules; install `libstdc++-15-dev` from the `ppa:ubuntu-toolchain-r/test` PPA)
- **Linux only:** the GLFW build dependencies, e.g. on Ubuntu `sudo apt install libwayland-dev libxkbcommon-dev xorg-dev`

> [!IMPORTANT]
> **Vulkan SDK version:** the modules-based build needs a Vulkan SDK whose C++ module (`vulkan.cppm`)
> *guards* its use of the C++ `std` module behind a feature macro, e.g. **1.4.328.1**. Newer SDKs
> (~1.4.350 and later) hardcode `export import std;`, which the toolchain can't build yet, so CMake
> **configure fails fast** against them. If your default SDK is such a version, install 1.4.328.1 next
> to it and point the build at it:
>
> ```bash
> cmake --preset windows-clang-debug --fresh -DVulkan_ROOT="C:/VulkanSDK/1.4.328.1"   # Windows
> source ~/VulkanSDK/1.4.328.1/setup-env.sh && cmake --preset linux-clang-debug --fresh  # Linux
> ```

## Getting Started <a name="getting-started"></a>

Follow these steps to get started:

1. Clone the repository and its submodules:

    ```bash
    git clone --recurse-submodules https://github.com/chFleschutz/aegis-engine.git
    ```

2. Configure and build with a preset (`windows-clang-*` or `linux-clang-*`, each `-debug` or
   `-release`), or open the folder in a CMake supported IDE:

    ```bash
    cmake --preset windows-clang-debug
    cmake --build --preset windows-clang-debug
    ```

3. Run one of the examples, e.g. `build/windows-clang-debug/examples/sponza/Sponza`.

4. Optionally run the tests (the `device` tests need a Vulkan GPU; `-LE device` skips them):

    ```bash
    ctest --preset windows-clang-debug
    ```

    See [tests/README.md](tests/README.md) for the test layout, conventions, and what is in scope.


## Modules and External Libraries <a name="external-libraries"></a>

This project is structured into several independent modules:
- [aegis-assets](https://github.com/chFleschutz/aegis-assets) - Collection of sample assets
- [aegis-log](https://github.com/chFleschutz/aegis-log) - Logging library

The engine relies on the following external libraries:
- [doctest](https://github.com/doctest/doctest) - Unit testing
- [entt](https://github.com/skypjack/entt) - Entity component system (ECS)
- [fastgltf](https://github.com/spnda/fastgltf) - GLTF 3D model loader
- [glfw](https://github.com/glfw/glfw) - Window management
- [glm](https://github.com/g-truc/glm) - Math library
- [imgui](https://github.com/ocornut/imgui) - Immediate mode UI
- [imguizmo](https://github.com/CedricGuillemet/ImGuizmo) - Gizmos
- [meshoptimizer](https://github.com/zeux/meshoptimizer) - Generating meshlets
- [stb](https://github.com/nothings/stb) - Image loading
- [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) - OBJ 3D model loader
- [Vulkan](https://www.vulkan.org/) - Graphics API, through [Vulkan-Hpp](https://github.com/KhronosGroup/Vulkan-Hpp)
- [Slang](https://shader-slang.org/) - Shading language
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - Vulkan Memory Management

## Screenshots

### Lumberyard Bistro Scene:
<img width="1920" height="1079" alt="bistro" src="https://github.com/user-attachments/assets/17da8987-aafc-4950-a82c-b4365cc5bcab" />

### Crytek Sponza Attrium Scene with Editor UI:
![Screenshot 2025-03-20 100442](https://github.com/user-attachments/assets/86098227-0346-4bbd-9b2c-d99a85a6dafb)

### Instancing Stress-Test of 1 Million Objects:
<img width="1919" height="1080" alt="lpo" src="https://github.com/user-attachments/assets/60326ef3-bad8-420c-b13d-63f4c06479cd" />

