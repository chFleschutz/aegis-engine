# AGENTS.md

This file provides guidance to coding agents working with code in this repository.

Aegis Engine is a cross-platform (Windows and Linux) C++23 Vulkan render engine. The build is
**C++20/23 modules-only** (every subsystem ships `.cppm` module files) and treats **warnings as
errors** — your edits must compile clean.

## Build & run

CMake ≥4.5 (for `import std`; 4.5.0-rc1 until the release) + Ninja via presets (`build/<preset>/`). Default preset: `windows-clang-debug`; on Linux
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

> **Vulkan SDK version matters — configure fails fast with the wrong one.** The RHI imports the
> SDK's Vulkan-Hpp module (`import vulkan`), which still changes between SDK releases, so the build
> is pinned to **1.4.363.0** (CI uses the same). SDKs 1.4.328 and older export `vulkan_hpp` instead;
> `src/aegis/rhi/CMakeLists.txt` rejects them at configure. Point the build at the pinned SDK, with
> `--fresh` when the build dir already found another one (the headers path is cached):
>
>     cmake --preset windows-clang-debug --fresh -DVulkan_ROOT="C:/VulkanSDK/1.4.363.0"
>
> Easiest: set `Vulkan_ROOT` to the pinned SDK once in your user environment. CMake searches it
> before `$VULKAN_SDK`, so it wins even when `VULKAN_SDK` points at another SDK, and fresh build
> dirs (e.g. new worktrees) configure without flags. On Linux, `source <sdk>/setup-env.sh`.
> `slangc` is taken from the same SDK. A configure that failed this check can make the next
> `--fresh` fail once in compiler detection ("CMAKE_C_COMPILER not set"); run it again.

**`import std`:** CMake 4.5 makes `import std` available to every C++ target. It builds the
standard library module (the MSVC STL's `std.ixx` on Windows, libstdc++'s `std.cc` on Linux) once
per distinct set of compile flags, with the importing target's flags. The engine and the SDK's
`vulkan.cppm` use it instead of standard headers. Rules:
- **`import std;` in every translation unit that uses the standard library**, as the first import
  after the module declaration (plain `.cpp`: after its includes). Modules never re-export it, so
  importing an engine module doesn't make `std` visible.
- **Standard headers only for their macros.** `import std` exports no macros, so keep `#include
  <cassert>` for `assert`, `<cfloat>` for `FLT_MAX`, `<cstdio>` for `stderr` and so on, in the
  global module fragment. Nothing else.
- **Qualify C names:** `import std` exports `std::uint32_t`, `std::size_t`, `std::sin`, not the
  global `uint32_t`, `size_t` or `sin`.
- **Includes before imports.** In a translation unit, `#include` every header (standard or
  third-party) *before* `import std;` or any module that imports it. The reverse order gives
  "redefinition" errors with the MSVC STL. A module unit's global module fragment satisfies this.
- **Keep std-using headers out of interface fragments when a module will do.** If a module
  interface *uses* a header's std-based code from its global module fragment (`core/assert.h`'s old
  inline handler, entt templates), Clang hides standard concepts from that module's importers:
  `missing '#include <concepts>'; 'derived_from' must be declared before it is used`, with a note
  "declaration here is not visible". Import a module instead (entt names come from
  `aegis.world.entity`), move the code into a `.cpp`, or, as a last resort, `#include` the named
  header in the failing file's global module fragment.
- **Warnings-as-errors reaches the std module.** The std module sources trip Clang's
  `-Wreserved-module-identifier` and `-Winclude-angled-in-module-purview`; the root `CMakeLists.txt`
  silences just those two. A fresh build still prints about 110 `-Winclude-angled-in-module-purview`
  warnings under "Diagnostics while scanning dependencies" for `std.ixx` and `vulkan.cppm`:
  `clang-scan-deps` ignores warning flags. They come from the scan, not a compile, and don't fail
  the build; ignore them.

Vulkan-Hpp (no exceptions, `VULKAN_HPP_USE_STD_EXPECTED`) returns `std::expected` from calls with one
success code: test `has_value()`, read `.error()` and `*result`. Calls with several success codes
(`acquireNextImage`, `QueryPool::getResults`) still return `vk::ResultValue` (`.result`, `.value`).

### Examples (visual smoke tests of the engine)
Executable targets are the PascalCase `project()` names: `Simple-Scene`, `Sponza`, `Eval-Scene`,
`Helmets`, plus `Template` / `Template-Editor`; each lives in `examples/<lowercased-name>`. Build one and
run its exe:

    cmake --build --preset windows-clang-debug --target Simple-Scene
    build\windows-clang-debug\examples\simple-scene\Simple-Scene.exe

`Template` is empty (safest smoke test); `Simple-Scene` is the simplest content scene. Content
scenes need the `aegis-assets` submodule. Report whether it built clean (warnings are errors) and the
window rendered; on a build failure, surface the first real error, not the downstream cascade.

**Games** (`cmake/AegisGame.cmake`, D7): `aegis_add_game(<Name> SOURCES … PROJECT <dir>)` builds the
game's sources once (an OBJECT library) into `<Name>` (runtime, no `aegis-editor`) and `<Name>-Editor`
around the engine's launcher, `src/aegis/launch/main.cpp`, the only `main()`. One game source file ends
with `AEGIS_GAME_MODULE({ .name = …, .registerTypes = …, .onStartup = … })` (`aegis/launch/game_module.h`).
The launcher finds the project by `--project <dir>`, then `project/` next to the executable, then
`PROJECT` (`paths::locate`); the shaders and the engine content are copied next to the executables and
found the same way, so a copied output folder runs. `Template` is the first game; the other examples
follow in #167.

The examples render through the `Engine` facade, but they open a window and have no pass/fail
signal — validation errors only go to stderr. Verify RHI work with `aegis-test` first (see
"Verifying device code").

### Shaders
Shaders compile on every build of `aegis-engine` or `aegis-render-graph` (the `compile_shaders`
target they depend on) while `COMPILE_SHADERS` is ON, the default, with `slangc` from the same
Vulkan SDK as the headers (falling back to `$VULKAN_SDK/bin`) through `aegis_add_shaders`
(`cmake/AegisShaders.cmake`), which `shaders/` and the `aegis-test` shader both use, so one flag set
applies everywhere. Each `.spv` is then checked with the SDK's `spirv-val`; invalid SPIR-V fails the
build (and CI). Editing a module recompiles only the shaders that import it (slangc `-depfile`, Ninja
and Makefile generators; other generators rebuild every shader). Output: `build/<preset>/shaders/<path>.slang.spv`, mirroring the source path;
stale `.spv` are removed at configure.
SDK 1.4.328's slangc (2025.17) emits invalid SPIR-V for a struct-pointer member of a struct reached
through a pointer; keep such members as `uint64_t` addresses behind properties (see `common::Mesh`).
(Not re-checked with SDK 1.4.363's slangc 2026.17, whose output for the current shaders passes
`spirv-val`, which now runs on every build.)

### Asset/shader paths (gotcha)
The `aegis.paths` module (`src/aegis/paths/`) is the one reader of the build-tree locations:
`aegis::paths::shaders()` (`<build>/shaders`), `aegis::paths::assets()`
(`<source>/modules/aegis-assets`), `aegis::paths::content()` (`<source>/content`, the engine content) and
`aegis::paths::cache()` (`<build>/.aegis`, the asset index). They are PRIVATE compile definitions
`AEGIS_SHADER_DIR`, `AEGIS_ASSETS_DIR`, `AEGIS_CONTENT_DIR` and `AEGIS_CACHE_DIR` of `aegis-paths`, read only in `paths.cpp`, so they are **absolute paths fixed at
configure time**, derived from `PROJECT_BINARY_DIR`/`PROJECT_SOURCE_DIR` so the engine also works as an
`add_subdirectory()` of another project. Running the exe from any cwd is fine, but moving or renaming
the source tree or the build dir breaks asset/shader loading unless you re-run configure or set the
environment variables of the same names, which override the baked-in values at
run time. Shaders and content are looked up first in `shaders/` and `content/` next to the executable
(after the variable, before the baked-in path), which is how a game built with `aegis_add_game` finds
them. Examples name assets through the library, by path inside the project (`assets.id("Misc/cube.obj")`, then
`assets.instantiate(scene, id)` / `assets.loadEnvironment(id)`); never add another `-D` path.
`aegis-test` still has its own `AEGIS_TEST_SHADER_PATH` for its fixture shader (built next to it).

### Assets by ID
`aegis::AssetLibrary` (`src/aegis/asset_library.*`, target `aegis-asset-library`) loads assets by `AssetId`
and caches them by ID; components store `AssetRef<T>` (`MeshRenderer`, `Sky`). It scans two roots through
`assets::AssetDatabase`: the project (`aegis-assets` for now; **a `.meta` sidecar with new IDs is written next
to every source file that has none**, so the submodule shows untracked `.meta` files) and the read-only engine
content folder `content/` (named in code as `assets::engine::white`, ...). A model file's meshes, materials and
textures get sub-asset IDs, recorded in its sidecar. Missing assets fall back to engine content with one
warning. Loaded assets are shared: editing one (a material's parameters) changes every user, so code that
changes one object's material gives it its own first (`assets.copyMaterial(ref)` / `createMaterial`). Sidecars and the index use glaze (`external/glaze`, include it only in `.cpp` files). Design:
`docs/asset-system.md`.

## Two code styles (most important)
The renderer was rewritten onto a new RHI; the code around it (engine, editor, world, scripting, UI)
still follows the older style. **Match the conventions of the code you are editing.**

| | New RHI — `src/aegis/rhi/`, `renderer/`, `platform/`, `assets/`, `render_graph/`, `test/` | Legacy — everything else (`core/`, `editor/`, `math/`, `world/`, `scripting/`, `ui/`, `utils/`, `engine.cppm`, `scene_*.cppm`, `examples/`) |
|---|---|---|
| Vulkan | vulkan-hpp `vk::raii`, `import vulkan` | none — go through `aegis::rhi` and friends |
| Errors | `std::expected<T, Error>` + `makeError`, never throw | `AGX_ASSERT*` |
| Modules | `.cppm` interface + `.cpp` impl, `import :partition;` | `.cppm` only, dotted submodules (`aegis.world.entity`, `aegis.utils.timer`); `math` is one module |

Both styles: lowercase `snake_case` namespaces (`aegis::rhi`, `aegis::world`) and module names
(`aegis.rhi`, `aegis.world`), 4-space indent with un-indented namespace bodies, `m_`-prefixed
camelCase members, Allman braces, `snake_case` filenames. New RHI also
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
call a slot index a handle. The rule covers GPU resources; CPU assets are shared through
`shared_ptr`. A plain vector index is an `…Id` (`render_graph::ResourceId`), not a handle.

**Never `#include <vulkan/vulkan.h>` directly** — use `import vulkan`, and only inside the new
RHI modules. rhi classes expose native `vk::` handles for interop (`Device::device()`,
`Buffer::vk()`, ...); the vulkan module is linked PRIVATE, so code outside rhi cannot name `vk::`
types, except `render_graph/passes/ui_pass.cpp`, which imports it for the ImGui backend.

**Known deviations (current state):** the `Error` types differ per module (rhi struct `{code}`,
`renderer` enum, `render_graph` `{message, rhi?}`, `assets` struct) and `platform::Window` asserts
instead of returning one; `render_graph` imports `aegis.world` and holds `Scene&`; 
components, `Profiler` and `Engine` sit directly in
`aegis`; `Window::glfwWindow()` returns a raw `GLFWwindow*` for the ImGui backend.

**Foundation (`aegis.base`, `src/aegis/base/`):** logging, flag enums, alignment, `match` and the
profiler; any module may import it (renamed `aegis.core` once the legacy core is gone).
- *Logging:* `aegis::log::{info,warn,error,fatal}` take a `std::format_string`, work with no setup
  and write `[warn] ...` to stderr under one mutex; `log::setSink` redirects them (tests capture
  output with it). Library code logs only degraded behaviour that carries on; errors are returned
  and only the top level logs them. `std::print` stays in `aegis-test`, examples and tools.
- *Flag enums:* opt in with `AEGIS_FLAGS(Enum)` (`#include "aegis/base/flags.h"`) directly after the
  enum, inside its namespace, so ADL finds the operators with no using-directive; an enum without
  it has no `|`. `base::hasFlag` tests bits.

**Target style (decided, not yet the current state):**
- *Errors:* one shape per module, `{ErrorCode code; std::string message; std::optional<rhi::Error>
  rhi}` (rhi keeps `{code, debug location}`), built with `makeError(code, msg)` /
  `makeError(lowerError, msg)` and printed only through the module's `describe(const Error&)`. A
  layer adds context and copies the cause; it never re-stringifies.
- *Static/dynamic entities:* static entities stay frozen after `begin`. Entity create/destroy and
  `Mesh`/`Material` add/remove bump a static-set version through the entt signals and the static set
  is rebuilt. Moving a static entity is explicit: the editor calls `markStaticDirty(entity)` after
  gizmo/inspector edits, and scripts that move their entity use `DynamicTag`.

**Link visibility:** link a library `PUBLIC` when its `.cppm` module interfaces import it (or export
its names, like EnTT from `aegis-world`), `PRIVATE` when only `.cpp` implementation units use it.
Linux fails on a missing transitive link that Windows hides, so declare what you import yourself.

### Cross-platform rules
Windows (Clang + MSVC STL, or MSVC) and Linux (Clang + **libstdc++ 15**; Ubuntu 24.04's 14.2 fails
on the modules) must both build. CI proves the Linux build on every PR.

- **`import std;` instead of standard headers** (see "`import std`"). It's the same module on both
  standard libraries, which ends the libstdc++ missing-include failures that used to build on
  Windows and break on Linux. The macro headers you still include must be included explicitly in
  each translation unit.
- **Paths are case-sensitive on Linux**: asset paths must match the file names exactly.
- **No platform headers outside a guard.** Windowing and input go through `aegis.platform` (GLFW);
  the only Win32 code is `aegis-test`'s synthetic-input check, behind `#ifdef _WIN32`.
- **Checking Linux from Windows before pushing (optional):** configure a scratch build with
  `clang++ --target=x86_64-w64-mingw32` against a MinGW GCC (libstdc++) on PATH (e.g. WinLibs) and
  `-DCMAKE_EXE_LINKER_FLAGS=-pthread`. It reproduced libstdc++'s missing-include errors for `rhi`,
  `renderer`, `platform`, `core` and the unit tests (written before `import std`; not re-checked
  since); it can't get past `aegis.world`, where clang crashes generating COFF code for module
  initializers (a MinGW-only clang bug, not a code problem).

### Formatting (clang-format)
One global root `.clang-format` (clang-format **22.x**), encoding the new-RHI style. It's a
normalizer, not a reflow engine (`ColumnLimit: 0` keeps your line breaks). **The whole tree is
formatted and stays that way:** run `tools/format.sh` before committing. It formats every source
file changed on your branch (`--all` formats the tree, `--check` only reports). If it reformats
lines you didn't touch, commit that as a separate formatting commit before your change. A tree-wide
reformat (e.g. after a `.clang-format` change) goes in its own commit and into
`.git-blame-ignore-revs`. Guard deliberately hand-aligned blocks (e.g. enum tables —
`AlignConsecutive*` is off) with `// clang-format off` / `on`.

## Commits and pull requests
Keep messages short: one imperative subject line (e.g. `Add rhi sampler`), no body unless the why
is non-obvious. **No `Co-Authored-By` or other agent/tool attribution trailers**, in commits or PR
descriptions.

**Code changes go through a PR** (anything under `src/`, `shaders/`, `tests/`, `examples/`, `tools/`,
CMake or CI). Docs-only changes may be committed to `main` directly.

**Planned work:** GitHub issues are the work items, grouped into milestones (`M6`, `M7`, `M9`,
then `Backlog`), with dependencies as "Blocked by" relations. An issue body holds the branch, size,
design refs (D-n), the finding IDs it closes, content, out of scope, "done when" and verification.
Labels: the kind (`bug`, `feature`, `refactor`, `cleanup`, `style`, `build`, `ci`, `tests`,
`documentation`) matches the branch prefix; `size/*`; `area/*` (don't run two items with the same
area in parallel); `needs-gpu` (verification needs the local device tests, which CI and cloud
sessions can't run); `needs-design` (an open decision; don't start without the owner). **Status is
the assignee:** whoever starts an item assigns it (`gh issue edit <n> --add-assignee @me`), and an
assigned issue is taken. `docs/audit/backlog.md` keeps the decisions (D1–D8, F1/F2) and each
milestone's goal and order; read the decisions an issue cites. Before starting an item, read its
issue (`gh issue view <n>`) and the finding IDs it lists (`docs/audit/findings-*.md`, corrected by
`verify-*.md`), then re-check them against the current code. Feature PRs don't edit `backlog.md`.

1. **One worktree per PR.** `tools/new-worktree.sh <branch> [base]` creates
   `../aegis-engine.worktrees/<branch>` from `origin/main` with submodules, then configures and
   builds it (about 20 s to configure, plus about a minute for the build). Several agents can work
   in parallel, one worktree each. Name branches
   by kind: `fix/`, `test/`, `refactor/`, `cleanup/`, `style/`, `build/`, `feat/`, `ci/`. After the
   PR merges, `tools/remove-worktree.sh <branch> --delete-branch`. On Windows, run the `tools/*.sh`
   scripts from Git Bash. In PowerShell, `bash` is usually the WSL launcher, so call Git's bash
   explicitly: `& "C:\Program Files\Git\bin\bash.exe" tools/new-worktree.sh <branch>`.
   Submodules come from `tools/init-submodules.sh`, which shares the main checkout's submodule
   objects instead of copying them. A worktree made some other way (e.g. Claude Code's
   `.claude/worktrees/`) gets its submodules and a configured preset from the `SessionStart` hook
   in `.claude/settings.json` (`tools/session-start.sh`); it still needs a build. Such a worktree
   starts on a throwaway branch (`claude/<session-name>`): rename it to the kind-prefixed name with
   `git branch -m <kind>/<name>` before the first commit. Keep worktree paths short
   on Windows: some build and submodule files are about 140 characters below the worktree root,
   and the limit is 260 (`git config --global core.longpaths true` helps git, not every tool).
2. **Stack dependent PRs** instead of waiting: pass the unmerged branch as `base`, open the PR
   against it (`gh pr create --base <that-branch>`), and retarget it once the base merges
   (`gh pr edit --base main`). Keep stacks to two or three PRs.
3. **Commit in reviewable steps.** Keep mechanical changes (whole-file reformats, renames, deletions,
   signature churn) in their own commits, separate from logic changes.
4. **Verify each commit:** it builds clean and `ctest -LE device` passes. **Before pushing**, and for
   any change to device code, run the full `ctest` *including* the `device` tests (CI has no GPU and
   skips them, see "Verifying device code"), and look at the `--screenshot` output after render
   changes. The test presets run tests in parallel; a `gpu` resource lock keeps the device tests
   apart within one run. Across worktrees, run one `ctest` with device tests at a time per machine.
5. **Format** with `tools/format.sh` (see "Formatting").
6. **Push and open the PR** with `gh pr create`; the body says `Closes #<issue>`. CI
   (`.github/workflows/build.yml`) builds Linux and Windows, debug and release, and runs
   `ctest -LE device`. It is the proof that Linux builds.
7. **Check CI** with `gh pr checks` (`--watch` to block) and report the result; work on another
   independent item meanwhile. Fix failures with new commits; don't force-push a PR under review.
8. **The owner merges.** Agents never merge a PR or enable auto-merge (`gh pr merge`); they stop
   once CI is green and the PR is ready, and the owner reviews and merges it. Write the PR body for
   that review: what changed and why, the verification actually run (device tests included or
   skipped) and anything left out. `main` is protected (required checks `Format`, `Linux`/`Windows`
   × debug/release, branch up to date). CI has no GPU: run the full `ctest` with the device tests
   locally first for device code. PRs land as merge commits, not squash or rebase, so commit hashes
   survive. After a PR with mechanical commits merges, add their hashes to `.git-blame-ignore-revs`
   (docs-only commit on `main`; only the owner can push to `main` past the protection). The
   `/implement-issue` skill follows this.

## Unit tests

doctest covers **pure, GPU-independent logic** in any module. One
executable per module (`aegis-tests-base`, `aegis-tests-rhi`, `aegis-tests-assets`, `aegis-tests-render_graph`,
`aegis-tests-math`, `aegis-tests-paths`, `aegis-tests-world`), each linking only that module's library. Layout, conventions, how to add a
module, and the full rationale: **`tests/README.md`**.

    cmake --build --preset windows-clang-debug            # builds all aegis-tests-*
    ctest --preset windows-clang-debug                    # runs every case in parallel, plus the device checks
    ctest --preset windows-clang-debug -LE device         # skips the device checks (no GPU needed)
    cmake --build --preset windows-clang-debug --target aegis-tests-rhi   # iterate one module
    ctest --preset windows-clang-debug -L rhi             # one module (by label; -R "rhi::" matches nothing)

- **Scope:** only logic needing no live `rhi::Device`. Invariants guarded by `assert` (e.g.
  `ResourcePool` stale-handle rejection) abort rather than return — test around them, not the abort.
- **Naming:** `<unit>.test.cpp`; `TEST_SUITE("<module>::<Type or function>")`; `TEST_CASE` as a
  present-tense behavior, with **no `;` in its name** (CTest discovery splits on it and both halves
  run zero cases while showing green; split the case instead); prefer `CHECK` over `REQUIRE`.
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

## Verifying device code
Green unit tests are *not* sufficient when device code changed. `src/aegis/test/` (`aegis-test`) is
the only `main()` that checks a real `rhi::Device` for failures; `examples/` run the same stack but
nothing fails them on a regression. Debug builds enable core **and synchronization** validation, and
`aegis-test` exits non-zero on any failed check or validation error of severity *error* (messages go
to stderr; warnings print but don't fail, and the count is process-wide) — a run is only verified if
it exits 0.

- `aegis-test --headless` renders offscreen: no window, no surface, no swapchain, and stops after
  600 frames (or `--frames <n>`). `ctest` runs it as a `device`-labelled test. Use this by default.
- `--scene` / `--gpu-driven` render a loaded scene through `aegis.render_graph` (CPU- / GPU-driven
  geometry) instead of the test triangle; `--screenshot <png>` saves the last headless frame — look
  at it after render changes. `ctest` runs both paths too.
- Headless does not touch surface, swapchain, acquire or present. When that code changed, also run
  `aegis-test --frames 600`: it opens a window briefly and exits by itself. On Windows it also checks
  `platform::Input` with synthetic key and mouse messages, so run it when platform code changes too.
