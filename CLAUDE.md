# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Aegis Engine is a Windows-only C++23 Vulkan render engine. The build is **C++20/23 modules-only**
(every subsystem ships `.cppm` module files) and treats **warnings as errors** — your edits must
compile clean.

## Build & run

CMake ≥3.28 + Ninja via presets (`build/<preset>/`). Default preset: `windows-clang-debug`.

    cmake --preset windows-clang-debug
    cmake --build --preset windows-clang-debug

- Main library target: `aegis-engine` (alias `Aegis::Engine`).
- MSVC presets (`windows-msvc-*`) require a Visual Studio developer shell (`VSCMD_VER` set).
- Requires `git clone --recurse-submodules` — assets live in the `aegis-assets` submodule.

> **Vulkan SDK version matters — configure fails fast with the wrong one.** Configure **rejects** 
> any SDK that hardcodes `export import std;` (~1.4.350+). Point the build at a **guarded** SDK 
> such as **1.4.328.1** — if your installed `VULKAN_SDK` is a newer hardcoded one, override it per 
> configure:
>
>     cmake --preset windows-clang-debug -DVulkan_ROOT="C:/VulkanSDK/1.4.328.1"
>
> (or set `VULKAN_SDK`/`Vulkan_ROOT` in the environment). See the unit-tests section for the full
> rationale.

### Examples (integration smoke tests — verify device-level work by building clean + running an example)
Executable targets are the PascalCase `project()` names: `Simple-Scene`, `Sponza`, `Eval-Scene`,
`Helmets`, `Template-Scene`. Build one and run its exe:

    cmake --build --preset windows-clang-debug --target Simple-Scene
    build\windows-clang-debug\examples\simple-scene\Simple-Scene.exe

`Template-Scene` is empty (safest smoke test); `Simple-Scene` is the simplest content scene.

### Shaders
Shaders auto-compile on every engine build (`compile_shaders` target). Needs both `glslc` and
`slangc` from the Vulkan SDK on PATH (`$VULKAN_SDK/bin`). Output: `build/<preset>/shaders/*.spv`.

### Asset/shader paths (gotcha)
`PROJECT_DIR`/`BUILD_DIR` are baked in as compile definitions, so assets and shaders load via
**absolute paths fixed at configure time**. Running the exe from any cwd is fine, but moving or
renaming the source tree or the build dir breaks asset/shader loading — re-run configure instead.

## Two stacks, two conventions (most important)
The codebase is mid-migration. **Match the conventions of the stack you are editing.**

| | New RHI — `src/aegis/rhi/`, `renderer/`, `platform/` | Old stack — everything else |
|---|---|---|
| Namespace | `aegis::rhi` (lowercase) | `Aegis::Graphics` (PascalCase) |
| Vulkan | vulkan-hpp `vk::raii`, `import vulkan_hpp` | volk, via `graphics/vulkan/vulkan_include.h` |
| Errors | `std::expected<T, Error>` + `makeError`, never throw | `VK_CHECK(...)`, `AGX_ASSERT*` |
| Indent | 4 spaces; namespace body un-indented | tabs; namespace body indented |
| Modules | `.cppm` interface + `.cpp` impl, `import :partition;` | `.cppm`, dotted PascalCase module names |

Both stacks: `m_`-prefixed camelCase members, Allman braces, `snake_case` filenames. New RHI also
uses trailing return types, `[[nodiscard]]`, `Desc` nested config structs, and packed generational
handles (`ResourceHandle<T>` / `BufferHandle` / `ImageHandle`) as the public currency instead of raw
pointers. Descriptors are bindless with fixed slots; **buffers use buffer device address, not
descriptors**.

**Never `#include <vulkan/vulkan.h>` directly** — new RHI uses `import vulkan_hpp`; the old stack
uses `graphics/vulkan/vulkan_include.h` (which loads volk with `VK_NO_PROTOTYPES`).

### Formatting (clang-format)
One global root `.clang-format` (clang-format **22.x**), encoding the new-RHI style. It's a
normalizer, not a reflow engine (`ColumnLimit: 0` keeps your line breaks). **Adopt diff-only** —
format just what you changed with `git clang-format`; never `clang-format -i` whole files. Editing an
old-stack file nudges changed lines toward new-RHI style (intended). Guard deliberately hand-aligned
blocks (e.g. enum tables — `AlignConsecutive*` is off) with `// clang-format off` / `on`.

## Unit tests

doctest covers **pure, GPU-independent logic** on the new RHI stack. One executable per module
(`aegis-tests-rhi`, `aegis-tests-math`), each linking only that module's library. Layout,
conventions, how to add a module, and the full rationale: **`tests/README.md`**.

    cmake --build --preset windows-clang-debug            # builds all aegis-tests-*
    ctest --preset windows-clang-debug                    # runs every case
    cmake --build --preset windows-clang-debug --target aegis-tests-rhi   # iterate one module
    ctest --preset windows-clang-debug -R "rhi::"

- **Scope:** only logic needing no live `rhi::Device`. Invariants guarded by `assert` (e.g.
  `ResourcePool` stale-handle rejection) abort rather than return — test around them, not the abort.
- **Naming:** `<unit>.test.cpp`; `TEST_SUITE("<ns>::<Type>")`; `TEST_CASE` as a present-tense
  behavior; prefer `CHECK` over `REQUIRE`.
- **Extract rather than mock:** when pure logic sits behind a GPU-owning type, lift it out and leave
  a thin adapter at the Vulkan call site. A **stateful type** earns its own partition; a **stateless
  helper** goes in `aegis::rhi::detail` inside its owner's partition, keeping it out of the public
  API. Prefer this over standing up a device — there is no headless harness.
- **Three traps, each costing a build cycle to rediscover:** **(1)** a test TU cannot name a `vk::`
  type at all (`vulkan-module` is linked PRIVATE, `rhi.cppm` re-exports nothing from it), so
  extracted helpers must take **plain values** — making a `vk::`-taking function public does *not*
  make it testable; **(2)** tests see only what the *primary* interface re-exports, so the type
  needs `export import` from the umbrella (`rhi.cppm`) — partition-level `export` alone is
  invisible; **(3)** check import direction before picking a home — `:utility` cannot host an
  `Extent3D` helper because `:common` already imports `:utility`.
- **Verifying device paths:** green unit tests are *not* sufficient when live upload/swapchain code
  moved. `src/aegis/test/` (`aegis-test`) is the only `main()` reaching a real `rhi::Device`;
  `examples/` still runs the old `Aegis::Graphics` stack and will not catch an RHI regression.
- **`import std` / Vulkan SDK (gotcha):** the toolchain (Clang + MSVC runtime) can't build the
  C++23 `import std` module yet, so `AEGIS_USE_IMPORT_STD` defaults **OFF** and the whole build
  stays on classic includes. Older `vulkan.cppm` guards its std module behind
  `__cpp_lib_modules` + `!VULKAN_HPP_NO_STD_MODULE` (we define the latter); newer SDKs (~1.4.350+)
  hardcode `export import std;`. When OFF, configure **fails fast** if the found SDK is such a
  hardcoded one — point `VULKAN_SDK`/`Vulkan_ROOT` at a guarded SDK (e.g. **1.4.328.1**). Flip
  `-DAEGIS_USE_IMPORT_STD=ON` (plus the `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` UUID) once the toolchain
  supports it; that lifts the SDK restriction too. See `src/aegis/rhi/CMakeLists.txt`.
