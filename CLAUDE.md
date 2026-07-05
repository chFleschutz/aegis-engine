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

### Examples (there is no unit-test framework — verify by building clean + running an example)
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
