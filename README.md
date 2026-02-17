# Aegis Engine

A small render engine developed with Vulkan in modern C++. 

###
![Screenshot 2025-03-20 095757](https://github.com/user-attachments/assets/b5d7ec34-6f00-4b99-b569-0e58ed10c57a)
###
![Screenshot 2025-03-20 100442](https://github.com/user-attachments/assets/86098227-0346-4bbd-9b2c-d99a85a6dafb)

# Features

- **Physically Based Rendering (PBR)** - Realistic lighting and materials 
- **GLTF & OBJ Loading** - Loading of custom 3D models 
- **Immediate Mode Editor UI** - Editor tools using Dear ImGui and ImGuizmo
- **Entity Component System (ECS)** - Efficient object management using EnTT
- **Framegraph** - Multipass rendering with automatic renderpass sorting
- **Deferred Rendering** - Improved lighting performance
- **HDR Rendering and Tone Mapping** - Enables wider color range for more visible details
- **Physically Based Bloom** - Realistic glow effect around bright areas created using convolution
- **Screen Space Ambient Occlusion (SSAO)** - Enhanced depth perception
- **Mesh Shaders Pipeline** - Modern geometry rendering
- **GPU Driven Rendering** - Minimize CPU rendering overhead
  
## Prerequisites <a name="prerequisites"></a>

Before building ensure you have the following installed:
- [Vulkan SDK](https://vulkan.lunarg.com/) - Download and install the latest version (at least version 1.3).
- CMake - Used for building the project. Either install it manually or use an IDE with built-in CMake support (like Visual Studio)

> Note: The engine currently only targets Windows.

## Getting Started <a name="getting-started"></a>

Follow these steps to get started:

1. Clone the repository and its submodules:

    ```bash
    git clone --recurse-submodules https://github.com/chFleschutz/aegis-engine.git
    ```

2. Open the folder in a CMake supported IDE (e.g. Visual Studio) or generate project files using CMake.

3. Build and Run one of the example projects.
   

## Modules and External Libraries <a name="external-libraries"></a>

This project is structured into several independent modules:
- [aegis-assets](https://github.com/chFleschutz/aegis-assets) - Collection of sample assest
- [aegis-gltf](https://github.com/chFleschutz/aegis-gltf) - GLTF 3D model loader
- [aegis-log](https://github.com/chFleschutz/aegis-log) - Logging library

The engine relies on the following external libraries:
- [entt](https://github.com/skypjack/entt) - Entity component system (ECS)
- [glfw](https://github.com/glfw/glfw) - Window management
- [glm](https://github.com/g-truc/glm) - Math library
- [imgui](https://github.com/ocornut/imgui) - Immediate mode UI
- [imguizmo](https://github.com/CedricGuillemet/ImGuizmo) - Gizmos
- [meshoptimizer](https://github.com/zeux/meshoptimizer) - Generating meshlets
- [stb](https://github.com/nothings/stb) - Image loading
- [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) - OBJ 3D model loader
- [Vulkan](https://www.vulkan.org/) - Graphics API
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - Vulkan Memory Management
- [Volk](https://github.com/zeux/volk) - Vulkan Function loader
