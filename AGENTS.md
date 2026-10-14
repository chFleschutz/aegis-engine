# AGENTS.md

This file provides guidance to coding agents working with code in this repository.

Aegis Engine is a cross-platform (Windows and Linux) C++23 Vulkan render engine. The build is **C++20/23 modules-only**
(every subsystem ships `.cppm` module files) and treats **warnings as errors** — your edits must
compile clean.

## Build & run

CMake ≥3.28 + Ninja via presets (`build/<preset>/`). Default preset: `windows-clang-debug`; on Linux
`linux-clang-debug`. Presets exist per platform and compiler: `windows-clang-*`, `windows-msvc-*`,
`linux-clang-*` (each `-debug` / `-release`).

    cmake --preset windows-clang-debug
    cmake --build --preset windows-clang-debug

- Main library target: `aegis-engine` (alias `Aegis::Engine`).
- MSVC presets (`windows-msvc-*`) require a Visual Studio developer shell (`VSCMD_VER` set). They
  **don't build yet**: MSVC 14.50 (VS 18.5) compiles `rhi`, `renderer` and `assets` but hits internal
  compiler errors (C1001) in `render_graph` and its tests. MSVC also needs `<compare>` in every
  translation unit that instantiates a defaulted `operator<=>` (Vulkan-Hpp's `vk::Flags` included),
  and resolves `std::pair` structured bindings in templates at the instantiating TU.
- Requires `git clone --recurse-submodules` — assets live in the `aegis-assets` submodule.

> **Vulkan SDK version matters — configure fails fast with the wrong one.** The build needs the
> SDK's `vulkan.cppm` in its *guarded* form (module `vulkan_hpp`, `import std` only behind
> `__cpp_lib_modules`), e.g. **1.4.328.1**. Newer SDKs (~1.4.350+) export `vulkan` with a hardcoded
> `export import std;`, which the toolchain can't build; `src/aegis/rhi/CMakeLists.txt` rejects them
> at configure. Point the build at the guarded SDK, with `--fresh` when the build dir already found
> another one (the headers path is cached):
>
>     cmake --preset windows-clang-debug --fresh -DVulkan_ROOT="C:/VulkanSDK/1.4.328.1"
>
> (or set `VULKAN_SDK` / `Vulkan_ROOT` in the environment; on Linux, `source <sdk>/setup-env.sh`).
> `slangc` is taken from the same SDK. A configure that failed this check can make the next
> `--fresh` fail once in compiler detection ("CMAKE_C_COMPILER not set"); run it again.

### Examples (visual smoke tests of the engine)
Executable targets are the PascalCase `project()` names: `Simple-Scene`, `Sponza`, `Eval-Scene`,
`Helmets`, `Template-Scene`; each lives in `examples/<lowercased-name>`. Build one and run its exe:

    cmake --build --preset windows-clang-debug --target Simple-Scene
    build\windows-clang-debug\examples\simple-scene\Simple-Scene.exe

`Template-Scene` is empty (safest smoke test); `Simple-Scene` is the simplest content scene. Content
scenes need the `aegis-assets` submodule. Report whether it built clean (warnings are errors) and the
window rendered; on a build failure, surface the first real error, not the downstream cascade.

The examples render through the `Engine` facade, but they open a window and have no pass/fail
signal — validation errors only go to stderr. Verify RHI work with `aegis-test` first (see "Verifying
device paths" under Unit tests).

### Shaders
Shaders auto-compile on every engine build (`compile_shaders` target) with `slangc` from the same
Vulkan SDK as the headers (falling back to `$VULKAN_SDK/bin`). Every shader depends on all of
`shaders/modules/*.slang`, so editing a module recompiles them. Output: `build/<preset>/shaders/*.spv`.
SDK 1.4.328's slangc (2025.17) emits invalid SPIR-V for a struct-pointer member of a struct reached
through a pointer; keep such members as `uint64_t` addresses behind properties (see `common::Mesh`).

### Asset/shader paths (gotcha)
`PROJECT_DIR`/`BUILD_DIR` are baked in as compile definitions, so assets and shaders load via
**absolute paths fixed at configure time**. Running the exe from any cwd is fine, but moving or
renaming the source tree or the build dir breaks asset/shader loading — re-run configure instead.

## Two code styles (most important)
The renderer was rewritten onto a new RHI; the code around it (engine, editor, scene, scripting, UI)
still follows the older style. **Match the conventions of the code you are editing.**

| | New RHI — `src/aegis/rhi/`, `renderer/`, `platform/`, `assets/`, `render_graph/`, `test/` | Legacy — everything else (`core/`, `editor/`, `scene/`, `scripting/`, `ui/`, `engine.cppm`, `examples/`) |
|---|---|---|
| Namespace | `aegis::rhi` (lowercase) | `Aegis::Scene` (PascalCase) |
| Vulkan | vulkan-hpp `vk::raii`, `import vulkan_hpp` | none — go through `aegis::rhi` and friends |
| Errors | `std::expected<T, Error>` + `makeError`, never throw | `AGX_ASSERT*` |
| Indent | 4 spaces; namespace body un-indented | tabs; namespace body indented |
| Modules | `.cppm` interface + `.cpp` impl, `import :partition;` | `.cppm`, dotted PascalCase module names |

Both styles: `m_`-prefixed camelCase members, Allman braces, `snake_case` filenames. New RHI also
uses trailing return types, `[[nodiscard]]`, `Desc` nested config structs, and packed generational
handles (`ResourceHandle<T>` / `BufferHandle` / `ImageHandle`) as the public currency instead of raw
pointers. Descriptors are bindless with fixed slots; **buffers use buffer device address, not
descriptors**. Every pipeline shares one layout (the bindless set plus 128 bytes of push constants),
so `Pipeline::*Desc` names no layout; shaders get addresses and descriptor indices through push
constants. Convention: `shaders/modules/README.md`.

**`…Handle` vs `…Descriptor` (naming rule):** `…Handle` always means a pooled, generational
resource reference resolved through `Device::get` (`BufferHandle`, `ImageViewHandle`,
`SamplerHandle`). `…Descriptor` always means a bindless slot index a shader consumes
(`Descriptor<T>`: `SampledImageDescriptor`, `StorageImageDescriptor`, `SamplerDescriptor`). A
resource *owns* its descriptors (`ImageView::sampledDescriptor()`, `Sampler::descriptor()`). Never
call a slot index a handle.

**Never `#include <vulkan/vulkan.h>` directly** — use `import vulkan_hpp`, and only inside the new
RHI modules (the vulkan module is linked PRIVATE, so `vk::` types never cross their public API).

### Cross-platform rules
Windows (Clang + MSVC STL, or MSVC) and Linux (Clang + libstdc++) must both build:

- **Include every standard header you use, in each translation unit** (in a module unit's global
  module fragment). libstdc++ pulls in far fewer headers transitively than MSVC's STL, so a missing
  `<cstdint>`, `<utility>` or `<cstddef>` builds on Windows and fails on Linux.
- **Paths are case-sensitive on Linux**: asset paths must match the file names exactly.
- **No platform headers outside a guard.** Windowing and input go through `aegis.platform` (GLFW);
  the only Win32 code is `aegis-test`'s synthetic-input check, behind `#ifdef _WIN32`.
- **Checking Linux from Windows:** configure a scratch build with `clang++ --target=x86_64-w64-mingw32`
  against a MinGW GCC (libstdc++) on PATH (e.g. WinLibs) and `-DCMAKE_EXE_LINKER_FLAGS=-pthread`.
  It reproduces libstdc++'s missing-include errors for `rhi`, `renderer`, `platform`, `core` and the
  unit tests; it can't get past `Aegis.Scene`, where clang crashes generating COFF code for
  module initializers (a MinGW-only clang bug, not a code problem).

### Formatting (clang-format)
One global root `.clang-format` (clang-format **22.x**), encoding the new-RHI style. It's a
normalizer, not a reflow engine (`ColumnLimit: 0` keeps your line breaks). **Adopt diff-only** —
format just what you changed with `git clang-format`; never `clang-format -i` whole files. Editing an
legacy file nudges changed lines toward new-RHI style (intended). Guard deliberately hand-aligned
blocks (e.g. enum tables — `AlignConsecutive*` is off) with `// clang-format off` / `on`.

## Commits
Keep messages short: one imperative subject line (e.g. `Add rhi sampler`), no body unless the why
is non-obvious. **No `Co-Authored-By` or other agent/tool attribution trailers.**

## Unit tests

doctest covers **pure, GPU-independent logic** on the new RHI stack. One executable per module
(`aegis-tests-rhi`, `aegis-tests-assets`, `aegis-tests-render_graph`, `aegis-tests-math`), each linking only that module's library. Layout,
conventions, how to add a module, and the full rationale: **`tests/README.md`**.

    cmake --build --preset windows-clang-debug            # builds all aegis-tests-*
    ctest --preset windows-clang-debug                    # runs every case, plus the device check
    ctest --preset windows-clang-debug -LE device         # skips the device check (no GPU needed)
    cmake --build --preset windows-clang-debug --target aegis-tests-rhi   # iterate one module
    ctest --preset windows-clang-debug -L rhi                # one module (by label; -R "rhi::" matches nothing)

- **Scope:** only logic needing no live `rhi::Device`. Invariants guarded by `assert` (e.g.
  `ResourcePool` stale-handle rejection) abort rather than return — test around them, not the abort.
- **Naming:** `<unit>.test.cpp`; `TEST_SUITE("<ns>::<Type>")`; `TEST_CASE` as a present-tense
  behavior; prefer `CHECK` over `REQUIRE`.
- **Extract rather than mock:** when pure logic sits behind a GPU-owning type, lift it out and leave
  a thin adapter at the Vulkan call site. A **stateful type** earns its own partition; a **stateless
  helper** goes in `aegis::rhi::detail` inside its owner's partition, keeping it out of the public
  API. Prefer this over a device check: `aegis-test` proves integration, not the logic's edge cases.
- **Three traps, each costing a build cycle to rediscover:** **(1)** a test TU cannot name a `vk::`
  type at all (`vulkan-module` is linked PRIVATE, `rhi.cppm` re-exports nothing from it), so
  extracted helpers must take **plain values** — making a `vk::`-taking function public does *not*
  make it testable; **(2)** tests see only what the *primary* interface re-exports, so the type
  needs `export import` from the umbrella (`rhi.cppm`) — partition-level `export` alone is
  invisible; **(3)** check import direction before picking a home — `:utility` cannot host an
  `Extent3D` helper because `:common` already imports `:utility`.
- **Verifying device paths:** green unit tests are *not* sufficient when device code changed.
  `src/aegis/test/` (`aegis-test`) is the only `main()` that checks a real `rhi::Device` for
  failures; `examples/` run the same stack but nothing fails them on a regression. Debug builds enable
  core **and synchronization** validation, and `aegis-test` exits non-zero on any failed check or
  validation error (messages go to stderr) — a run is only verified if it exits 0.
  - `aegis-test --headless` renders offscreen: no window, no surface, no swapchain, and stops after
    600 frames (or `--frames <n>`). `ctest` runs it as a `device`-labelled test. Use this by default.
  - `--scene` / `--gpu-driven` render a loaded scene through `aegis.render_graph` (CPU- / GPU-driven
    geometry) instead of the test triangle; `--screenshot <png>` saves the last headless frame — look at
    it after render changes. `ctest` runs both paths too.
  - Headless does not touch surface, swapchain, acquire or present. When that code changed, also run
    `aegis-test --frames 600`: it opens a window briefly and exits by itself. On Windows it also checks
    `platform::Input` with synthetic key and mouse messages, so run it when platform code changes too.
- **`import std` / Vulkan SDK (gotcha):** the toolchain (Clang + MSVC STL) can't build the C++23
  `import std` module yet, so the whole build stays on classic includes. The guarded `vulkan.cppm`
  (SDK ≤ 1.4.328) only imports std behind `__cpp_lib_modules` and `!VULKAN_HPP_NO_STD_MODULE`; the
  build defines the latter, so a standard library that advertises modules (libstdc++ 15) doesn't
  switch it on. Newer SDKs hardcode `export import std;` and are rejected at configure (see Build &
  run). Moving to them means building `import std` and switching `import vulkan_hpp` to
  `import vulkan`. See `src/aegis/rhi/CMakeLists.txt`.
