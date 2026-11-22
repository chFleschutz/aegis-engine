# Aegis Engine

<p>
  <img alt="Language" src="https://img.shields.io/badge/Language-C%2B%2B23-blue?style=for-the-badge&logo=c%2B%2B">
  <img alt="Platform" src="https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-blue?style=for-the-badge">
  <a href="https://github.com/chFleschutz/aegis-engine/blob/main/LICENSE">
    <img alt="License" src="https://img.shields.io/badge/License-Apache%202.0-blue?style=for-the-badge">
  </a>
  <a href="https://github.com/chFleschutz/aegis-engine/actions/workflows/build.yml">
    <img alt="Build" src="https://img.shields.io/github/actions/workflow/status/chFleschutz/aegis-engine/build.yml?branch=main&style=for-the-badge&label=Build">
  </a>
</p>

A Vulkan render engine in modern C++ (C++23, built entirely from C++20 modules) for exploring
low-level graphics programming: a render graph, bindless resources, GPU-driven mesh-shader
rendering, and an editor to build scenes with.

![Screenshot 2025-03-20 095757](https://github.com/user-attachments/assets/b5d7ec34-6f00-4b99-b569-0e58ed10c57a)

> [!NOTE]
> *My original Master's Thesis version is archived in the [thesis branch](https://github.com/chFleschutz/aegis-engine/tree/thesis) or at the [thesis tag](https://github.com/chFleschutz/aegis-engine/releases/tag/thesis).*

## Features

**Rendering**
- **Render graph** - passes declare the resources they read and write; the graph orders them, places
  the barriers and keeps resources that live across frames. Games can add their own passes
- **Bindless resources** - images and samplers in one descriptor heap, buffers through buffer device
  addresses, one pipeline layout for everything
- **GPU-driven rendering** - culling and draw submission on the GPU, with a mesh-shader pipeline over
  meshlets; a CPU-driven path covers the same scenes
- **Physically based rendering** - PBR materials, image-based lighting from HDR environments
- **Deferred shading** with point, spot and directional lights stored in a GPU buffer
- **Shadows** - cascaded shadow maps for the directional light, and shadows from spot lights
- **Transparency** - alpha-masked and double-sided materials, and blended materials drawn in a forward
  pass, sorted back to front
- **Several views per frame**, each with its own post-processing settings
- **HDR and post-processing** - tone mapping and physically based bloom
- **Headless rendering** - offscreen, with no window, used by the GPU tests

**Engine and tools**
- **Editor** - viewport with gizmos, hierarchy, a registry-driven Inspector, content browser, undo
  and redo, play-in-editor, and saving scenes (Dear ImGui and ImGuizmo)
- **Entity component system** on EnTT, with C++ scripts attached to entities
- **Type registry** - components and scripts registered once are saved in scene files and edited
  in the Inspector with no editor code
- **Asset pipeline** - glTF and OBJ models, KTX2 textures, stable asset IDs in `.meta` sidecars, an
  import cache, and scene files that nest other scenes as prefabs
- **Input actions** that map keys and buttons to named actions
- **Own math library** (`aegis.math`): row-major, generic, tested and benchmarked against glm
- **Shaders in [Slang](https://shader-slang.org/)**, compiled and validated with every build

## Prerequisites

The engine builds on **Windows** and **Linux**. Install:
- [Vulkan SDK](https://vulkan.lunarg.com/) **1.4.363.0** exactly (see the note below)
- [CMake](https://cmake.org/) **4.5** or newer (currently [4.5.0-rc1](https://github.com/Kitware/CMake/releases/tag/v4.5.0-rc1)) and [Ninja](https://ninja-build.org/). The build uses the C++23 `import std` module, which needs CMake 4.5
- A C++23 compiler with C++20 modules support:
  - **Windows:** [Clang](https://releases.llvm.org/) 21 or newer with the MSVC standard library from Visual Studio 2022 17.6 or newer (the `windows-msvc-*` presets exist, but MSVC currently fails with internal compiler errors)
  - **Linux:** Clang 21 or newer with libstdc++ 15. Ubuntu 26.04: `sudo apt install clang clang-tools libstdc++-15-dev`. Older Ubuntu releases don't work: libstdc++ 14 fails to compile the modules, and their libstdc++ 15 packages ship a broken `libstdc++.modules.json`, which `import std` needs
- **Linux only:** the GLFW build dependencies, e.g. on Ubuntu `sudo apt install libwayland-dev libxkbcommon-dev xorg-dev`

> [!IMPORTANT]
> **Vulkan SDK version:** the engine imports the SDK's Vulkan-Hpp C++ module (`import vulkan`),
> which still changes between SDK releases, so the build is pinned to **1.4.363.0** and CMake
> **configure fails fast** against any other SDK. If several SDKs are installed, set `Vulkan_ROOT` to
> this one in your environment, or pass it once (`--fresh` if the build folder found another SDK):
>
> ```bash
> cmake --preset windows-clang-debug --fresh -DVulkan_ROOT="C:/VulkanSDK/1.4.363.0"   # Windows
> source ~/VulkanSDK/1.4.363.0/setup-env.sh && cmake --preset linux-clang-debug --fresh  # Linux
> ```

## Getting Started

1. Clone the repository with its submodules. The sample project (the example scenes and their
   assets) is an opt-in submodule; check it out too to run the examples:

    ```bash
    git clone --recurse-submodules https://github.com/chFleschutz/aegis-engine.git
    git -C aegis-engine submodule update --init --checkout games/examples/project
    ```

2. Configure and build with a preset, or open the folder in a CMake-aware IDE:

    ```bash
    cmake --preset windows-clang-dev
    cmake --build --preset windows-clang-dev
    ```

    The presets are `windows-clang-*` and `linux-clang-*`, each in three build types:

    | Suffix     | Build type    | Use it for                                                         |
    |------------|---------------|--------------------------------------------------------------------|
    | `-debug`   | `Debug`       | stepping through code in a debugger                                |
    | `-dev`     | `Development` | day-to-day runs: optimised, with asserts and Vulkan validation on  |
    | `-release` | `Release`     | performance                                                        |

3. Run an example. The examples are the scene files of the `Examples` game
   (`games/examples/project/content/scenes/`); pick one with `--scene`, or run `Examples-Editor` to
   open it in the editor:

    ```bash
    build/windows-clang-dev/games/examples/Examples --scene scenes/sponza.scene
    build/windows-clang-dev/games/examples/Examples-Editor --scene scenes/sponza.scene
    ```

    Without the sample project, run the empty `Template` game instead
    (`build/<preset>/games/template/Template`). Every game prints its options with `--help`.

4. Optionally run the tests. The `device` tests render on a Vulkan GPU; `-LE device` skips them:

    ```bash
    ctest --preset windows-clang-dev
    ctest --preset windows-clang-dev -LE device
    ```

    [tests/README.md](tests/README.md) explains the test layout and conventions.

## Usage: making a game

A game is a folder under `games/` with its code and a project folder for its data. The engine owns
`main()` (the launcher): the game hands it a `GameModule` that registers its types and loads its
first scene. Each game builds twice, as `<Name>`, the runtime, and as `<Name>-Editor`, the same game
with the editor. `games/template/` is the starting point; copy it, or start from this:

```
games/my_game/
├── CMakeLists.txt
├── game.cpp
└── project/
    └── content/     # the game's assets and scene files
```

**`games/my_game/CMakeLists.txt`**, then add `add_subdirectory(my_game)` to `games/CMakeLists.txt`:

```cmake
aegis_add_game(MyGame SOURCES game.cpp PROJECT project)
```

**`games/my_game/game.cpp`**: a component, a script, and a scene that attaches both to a cube:

```cpp
#include <aegis/launch/game_module.h>

import std;
import aegis.game;

namespace
{
/// @brief A component: plain data, saved in scene files and edited in the editor's Inspector
struct Health
{
    float value{ 100.0f };
};

/// @brief A script: behaviour attached to an entity, with fields like a component
class Spin : public aegis::scripting::Script
{
public:
    float speed{ 1.0f }; // Radians per second

    void onUpdate(aegis::scripting::ScriptContext& context, float deltaSeconds) override
    {
        auto& transform = context.get<aegis::Transform>();
        transform.rotation *= aegis::math::Quat::fromAxisAngle(aegis::math::world::up<float>, speed * deltaSeconds);
    }
};

auto registerTypes(aegis::reflect::TypeRegistry& types) -> std::expected<void, aegis::reflect::Error>
{
    return aegis::world::registerComponent<Health>(types, "Health")
        .and_then([](auto builder) { return builder.field("value", &Health::value).status(); })
        .and_then([&types] { return aegis::scripting::registerScript<Spin>(types, "Spin"); })
        .and_then([](auto builder) { return builder.field("speed", &Spin::speed).status(); });
}

/// @brief The default camera, lights and sky, and a spinning cube
class MyScene : public aegis::SceneDescription
{
public:
    void initialize(aegis::SceneContext& context) override
    {
        aegis::addDefaults(context);

        auto cube = aegis::instantiate(context, aegis::assets::engine::cube); // The engine's cube.obj
        if (!cube)
            return;
        auto& registry = context.scene.registry();
        // The script turns the instance and with it the cube's mesh, a child: both move every frame
        registry.add<aegis::DynamicTag>(*cube);
        for (auto child = registry.get<aegis::Children>(*cube).first; child; child = registry.get<aegis::Siblings>(child).next)
            registry.add<aegis::DynamicTag>(child);
        registry.get<aegis::Transform>(*cube).scale = aegis::math::Float3{ 5.0f };
        registry.add<Health>(*cube);
        [[maybe_unused]] auto spin = aegis::scripting::attachScript(registry, context.types, *cube, "Spin");
    }
};

auto onStartup(aegis::Engine& engine) -> std::expected<void, aegis::EngineError>
{
    engine.loadScene<MyScene>();
    return {};
}
}

AEGIS_GAME_MODULE({ .name = "MyGame", .registerTypes = registerTypes, .onStartup = onStartup })
```

Build and run it, in the runtime or in the editor:

```bash
cmake --preset windows-clang-dev   # picks up the new folder
cmake --build --preset windows-clang-dev --target MyGame MyGame-Editor
build/windows-clang-dev/games/my_game/MyGame
build/windows-clang-dev/games/my_game/MyGame-Editor
```

**Project data and scene files.** Assets go in `project/content/` and are named by their path in it.
On the first scan each source file gets a `.meta` sidecar with its stable asset ID; commit the
sidecars with the files. Scenes are JSON `.scene` files in the same folder: build one in the editor
and save it, and the game loads it by path instead of (or after) a scene written in code:

```cpp
AEGIS_GAME_MODULE({ .name = "MyGame", .startupScene = "scenes/main.scene", .registerTypes = registerTypes })
```

`--scene <path>` overrides the startup scene, and `--project <dir>` points a game at another project
folder. A scene file stores registered components by name and an entity's scripts in its `scripts`
list, with the fields you registered:

```json
"components": { "Health": { "value": 100 }, "Transform": { ... } },
"scripts": [ { "Spin": { "speed": 1 } } ]
```

[`games/examples/`](games/examples/) has more scripts, and the glossary and docs below explain the
concepts in depth.

## Documentation

- [GLOSSARY.md](GLOSSARY.md) - the engine's terms
- [docs/asset-system.md](docs/asset-system.md) - assets, IDs, the import cache and scene files
- [docs/editor.md](docs/editor.md) - the editor's architecture and plan
- [docs/adr/](docs/adr/README.md) - architecture decision records
- [docs/roadmap.md](docs/roadmap.md) - milestones and their order; the work items are [GitHub issues](https://github.com/chFleschutz/aegis-engine/issues)
- [tests/README.md](tests/README.md) - unit and device tests
- [AGENTS.md](AGENTS.md) - code style, build details and the contribution workflow

## External Libraries

- [aegis-samples](https://github.com/chFleschutz/aegis-samples) - the sample project: the examples' scenes and assets (opt-in submodule)
- [Vulkan](https://www.vulkan.org/) - graphics API, through [Vulkan-Hpp](https://github.com/KhronosGroup/Vulkan-Hpp)
- [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) - GPU memory management
- [Slang](https://shader-slang.org/) - shading language
- [glfw](https://github.com/glfw/glfw) - windows and input
- [entt](https://github.com/skypjack/entt) - entity component system
- [imgui](https://github.com/ocornut/imgui) and [imguizmo](https://github.com/CedricGuillemet/ImGuizmo) - editor UI and gizmos
- [fastgltf](https://github.com/spnda/fastgltf) and [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) - glTF and OBJ loading
- [meshoptimizer](https://github.com/zeux/meshoptimizer) - meshlet generation
- [KTX-Software](https://github.com/KhronosGroup/KTX-Software) - KTX2 textures
- [stb](https://github.com/nothings/stb) - image loading
- [glaze](https://github.com/stephenberry/glaze) - JSON for scene files and sidecars
- [xxHash](https://github.com/Cyan4973/xxHash) and [zstd](https://github.com/facebook/zstd) - hashing and compression for the import cache
- [doctest](https://github.com/doctest/doctest) - unit tests
- [glm](https://github.com/g-truc/glm) - reference for the math library's tests and benchmark only

## Screenshots

### Lumberyard Bistro Scene
<img width="1920" height="1079" alt="bistro" src="https://github.com/user-attachments/assets/17da8987-aafc-4950-a82c-b4365cc5bcab" />

### Crytek Sponza Atrium Scene with Editor UI
![Screenshot 2025-03-20 100442](https://github.com/user-attachments/assets/86098227-0346-4bbd-9b2c-d99a85a6dafb)

### Instancing Stress Test of 1 Million Objects
<img width="1919" height="1080" alt="lpo" src="https://github.com/user-attachments/assets/60326ef3-bad8-420c-b13d-63f4c06479cd" />

## License

Apache 2.0, see [LICENSE](LICENSE).
