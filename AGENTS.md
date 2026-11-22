# AGENTS.md

This file provides guidance to coding agents working with code in this repository.

Aegis Engine is a cross-platform (Windows and Linux) C++23 Vulkan render engine. The build is
**C++20/23 modules-only** (every subsystem ships `.cppm` module files) and treats **warnings as
errors** — your edits must compile clean.

`GLOSSARY.md` defines the engine's terms: read it when a term here is unclear or before naming a
new concept, and use its names in code, docs and issues. When a build, tool or test behaves
unexpectedly, check `docs/agent-notes.md`: gotchas from past sessions.

## Build & run

CMake ≥4.5 (for `import std`; 4.5.0-rc1 until the release) + Ninja via presets (`build/<preset>/`):
`windows-clang-*`, `windows-msvc-*`, `linux-clang-*`, each `-debug` / `-dev` / `-release`. Default
`windows-clang-debug`; on Linux `linux-clang-debug`. Main library target: `aegis-engine`.

    cmake --preset windows-clang-debug
    cmake --build --preset windows-clang-debug

- **Day-to-day runs: `windows-clang-dev` / `linux-clang-dev`**, the `Development` build type
  (`cmake/AegisBuildTypes.cmake`): Debug with optimisation, so asserts and validation stay on and a
  headless `aegis-test` Sponza run takes about half as long. Keep `-debug` for stepping through code.
  `CMAKE_BUILD_TYPE` is `Debug`, `Development` or `Release`; generator expressions that mean "not
  Release" name both (`$<CONFIG:Debug,Development>`).
- `-DAEGIS_BUILD_BENCHMARKS=ON` adds `aegis-bench-math` (`tools/bench-math/`), `aegis.math` against glm on six
  operations; run it from a `-release` and a `-dev` build (it exits 1 past the gate, 2 when the libraries disagree) and never from `ctest`.
- The MSVC presets **don't build yet** (internal compiler errors in `render_graph`) and configure only
  from a shell with the Visual Studio environment (`vcvars64.bat`).
- Clone with `--recurse-submodules`. The sample project (the examples' scenes and the assets the
  device tests load) is the opt-in submodule `samples`, checked out at the `Examples` game's project
  folder: `git submodule update --init --checkout games/examples/project`. Without it configure
  skips the `Examples` game and the device tests that load samples.
  It clones the full history, so `tools/init-submodules.sh` shares the main checkout's copy with
  worktrees and git can tell whether a pointer is on its `origin/main`; a main checkout whose
  samples are still shallow (cloned before #327) needs `git -C games/examples/project fetch
  --unshallow` once, and the same for `external/zstd` (cloned before #370).
  A change to the samples is a PR in aegis-samples (branch from its `origin/main`); the engine PR
  bumps the `samples` pointer to that commit and says to merge the samples PR first.
- Vendored files (`external/ktx`, the editor's Lucide icon font and its generated
  `aegis/editor/lucide.cppm`) change only through their `tools/update-*` script, committed on
  its own.

> **Vulkan SDK 1.4.363.0 is pinned** (CI uses the same): the RHI imports the SDK's Vulkan-Hpp module
> (`import vulkan`), which still changes between releases, and configure rejects other SDKs. Set
> `Vulkan_ROOT` to the pinned SDK once in your user environment (it wins over `$VULKAN_SDK`, so new
> worktrees configure without flags); on Linux, `source <sdk>/setup-env.sh`. A build dir that already
> found another SDK needs `--fresh`:
>
>     cmake --preset windows-clang-debug --fresh -DVulkan_ROOT="C:/VulkanSDK/1.4.363.0"

### `import std`
The engine uses `import std` instead of standard headers, on both standard libraries. Rules:
- **`import std;` in every translation unit that uses the standard library**, as the first import
  after the module declaration (plain `.cpp`: after its includes). Modules never re-export it, so
  importing an engine module doesn't make `std` visible.
- **Standard headers only for their macros.** `import std` exports no macros, so keep `#include
  <cassert>` for `assert`, `<cfloat>` for `FLT_MAX`, `<cstdio>` for `stderr` and so on, in the
  global module fragment of each translation unit that uses them. Nothing else.
- **Qualify C names:** `import std` exports `std::uint32_t`, `std::size_t`, `std::sin`, not the
  global `uint32_t`, `size_t` or `sin`.
- **Includes before imports.** In a translation unit, `#include` every header (standard or
  third-party) *before* `import std;` or any module that imports it. The reverse order gives
  "redefinition" errors with the MSVC STL. A module unit's global module fragment satisfies this.
- **Keep std-using headers out of interface fragments when a module will do.** If a module
  interface *uses* a header's std-based code from its global module fragment (entt
  templates), Clang hides standard concepts from that module's importers:
  `missing '#include <concepts>'; 'derived_from' must be declared before it is used`, with a note
  "declaration here is not visible". Import a module instead (entt names come from
  `aegis.world`), move the code into a `.cpp`, or, as a last resort, `#include` the named
  header in the failing file's global module fragment.
- A fresh build prints about 110 `-Winclude-angled-in-module-purview` warnings under "Diagnostics
  while scanning dependencies" for `std.ixx` and `vulkan.cppm`. They come from the dependency scan
  and don't fail the build; ignore them.

Vulkan-Hpp (no exceptions, `VULKAN_HPP_USE_STD_EXPECTED`) returns `std::expected` from calls with one
success code: test `has_value()`, read `.error()` and `*result`. Calls with several success codes
(`acquireNextImage`, `QueryPool::getResults`) still return `vk::ResultValue` (`.result`, `.value`).

### Examples and games
The examples are two games built by `aegis_add_game` (`cmake/AegisGame.cmake`, whose header lists
the targets and the headless `device`/`editor` tests it adds), each in `games/<lowercased-name>`
with a runtime and an `-Editor` flavor: `Template`, an empty project (the safest smoke test), and
`Examples`, whose project is the samples submodule (`games/examples/project/`) and whose examples are
its scene files, picked with `--scene <path>` relative to its `content/` (default
`scenes/simple.scene`, the simplest):

    cmake --build --preset windows-clang-debug --target Examples
    build\windows-clang-debug\games\examples\Examples.exe --scene scenes/sponza.scene

Report whether it built clean and the window rendered; on a build failure, surface the first real
error, not the downstream cascade. A windowed example has no pass/fail signal (validation errors only
go to stderr), so verify RHI work with `aegis-test` first (see "Verifying device code").
`tools/capture-window.ps1` saves a windowed run as a PNG and reports stderr's `[error]` lines.

### Shaders
Shaders compile with `slangc` from the pinned SDK on every build of `aegis-engine` or
`aegis-render-passes`, through `aegis_add_shaders` (`cmake/AegisShaders.cmake`), into
`build/<preset>/shaders/<path>.slang.spv`. slangc warnings are errors, as in C++, and each `.spv` is
checked with `spirv-val`; invalid SPIR-V fails the build. Shader conventions: `shaders/modules/README.md`.

### Assets and paths
`aegis.paths` (`aegis/paths/`) is the one reader of the shader, content, project and cache
folders; code names assets through `AssetLibrary` by path inside it (`assets.id("models/basic/cube.obj")`)
and never adds another `-D` path. A new source file in the samples gets a `.meta` sidecar: commit it
there. Loaded assets are shared, so code that changes one object's material copies it first
(`assets.copyMaterial(ref)`). Before changing asset loading, read `docs/asset-system.md` ("Working with
it today").

## Code style (most important)
**Match the conventions of the code you are editing.** The engine follows the rules below, except
the **legacy** rest, which keeps older conventions until it is rewritten: `aegis/launch/`, `math/`
and `games/`. There, misuse is an `assert` and `math` is one `.cppm` with no partitions.

- *Errors:* `std::expected<T, Error>` built with `makeError`, never thrown.
- *Modules:* one module per folder, a `.cppm` interface and `.cpp` implementation units, split into
  partitions (`import :partition;`); importers write one import, never a dotted submodule.

Everywhere: lowercase `snake_case` namespaces (`aegis::rhi`, `aegis::world`) and module names
(`aegis.rhi`, `aegis.world`), 4-space indent with un-indented namespace bodies, `m_`-prefixed
camelCase members, Allman braces, `snake_case` filenames. New RHI also
uses trailing return types, `[[nodiscard]]`, `Desc` nested config structs, and packed generational
handles (`ResourceHandle<T>` / `BufferHandle` / `ImageHandle`) as the public currency instead of raw
pointers. Descriptors are bindless with fixed slots; **buffers use buffer device address, not
descriptors**. Every pipeline shares one layout (the bindless set plus 128 bytes of push constants),
so `Pipeline::*Desc` names no layout; shaders get addresses and descriptor indices through push
constants.

**`…Handle`, `…Descriptor` and `…Id` (naming rule):** each suffix always means its glossary term.
A resource *owns* its descriptors (`ImageView::sampledDescriptor()`, `Sampler::descriptor()`). Never
call a slot index or a vector index a handle. The rule covers GPU resources; CPU assets are shared
through `shared_ptr`.

**Vulkan stays inside rhi:** use `import vulkan` and `vk::raii`, never `#include <vulkan/vulkan.h>`,
and only inside the new RHI modules; everything else goes through `aegis::rhi`. rhi classes expose
native `vk::` handles for interop (`Device::device()`, `Buffer::vk()`, ...); the vulkan module is
linked PRIVATE, so code outside rhi cannot name `vk::` types. ImGui's Vulkan backend sits behind `rhi::ImGuiBackend`.

**Known deviations (current state):** the `Error` types differ per module (rhi struct `{code}`,
`renderer` enum, `render_graph` `{message, rhi?}`, `assets` struct); `platform::Window` asserts
instead of returning one, and `world` and `scripting` assert on misuse that could be recoverable;
components, `Profiler` and `Engine` sit directly in `aegis`; `engine/` holds several modules
(`aegis.engine`, `aegis.game`, `aegis.defaults`, `aegis.project_file`, `aegis.scene_description`) instead of
partitions, and some of its errors are still `std::string`; `Window::glfwWindow()` returns a raw `GLFWwindow*` for the ImGui backend.

**Foundation (`aegis.core`, `aegis/core/`):** logging, flag enums, alignment, `match` and the
profiler; any module may import it.
- *Logging:* `aegis::log::{info,warn,error,fatal}` take a `std::format_string` and work with no
  setup; `log::setSink` redirects them (tests capture output with it). Library code logs only
  degraded behaviour that carries on; errors are returned and only the top level logs them.
  `std::print` stays in `aegis-test`, examples and tools.
- *Flag enums:* opt in with `AEGIS_FLAGS(Enum)` (`#include "aegis/core/flags.h"`) directly after the
  enum, inside its namespace, so ADL finds the operators with no using-directive; an enum without
  it has no `|`. `core::hasFlag` tests bits.

**Target style (decided, not yet the current state):**
- *Errors:* one shape per module, `{ErrorCode code; std::string message; std::optional<rhi::Error>
  rhi}` (rhi keeps `{code, debug location}`), built with `makeError(code, msg)` /
  `makeError(lowerError, msg)` and printed only through the module's `describe(const Error&)`. A
  layer adds context and copies the cause; it never re-stringifies.
- *Static/dynamic entities:* entity create/destroy and
  `Mesh`/`Material` add/remove bump a static-set version through the entt signals and the static set
  is rebuilt. Moving a static entity is explicit: the editor calls `markStaticDirty(entity)` after
  gizmo/inspector edits, and scripts that move their entity use `DynamicTag`.

**Link visibility:** link a library `PUBLIC` when its `.cppm` module interfaces import it (or export
its names, like EnTT from `aegis-world`), `PRIVATE` when only `.cpp` implementation units use it.
Linux fails on a missing transitive link that Windows hides, so declare what you import yourself.

### Cross-platform rules
Windows (Clang + MSVC STL) and Linux (Clang + **libstdc++ 15**; Ubuntu 24.04's 14.2 fails on the
modules) must both build; CI proves the Linux build on every PR.
- **Paths are case-sensitive on Linux**: asset paths must match the file names exactly.
- **Platform headers stay behind a guard.** Windowing and input go through `aegis.platform` (GLFW);
  the only Win32 code is `aegis-test`'s synthetic-input check, behind `#ifdef _WIN32`.

### Formatting (clang-format)
One root `.clang-format` (clang-format **22.x**); `ColumnLimit: 0` keeps your line breaks. **The
whole tree is formatted and stays that way:** run `tools/format.sh` before committing (it formats
the files changed on your branch). If it reformats lines you didn't touch, commit that as a separate
formatting commit before your change. A tree-wide reformat goes in its own commit and into
`.git-blame-ignore-revs`. Guard deliberately hand-aligned blocks (e.g. enum tables) with
`// clang-format off` / `on`.

**Line endings are LF** in the repository and in every checkout, Windows included; scripted edits
(sed, perl) can match `\n` alone.

## Commits and pull requests
Keep messages short: one imperative subject line (e.g. `Add rhi sampler`), no body unless the why
is non-obvious. **No `Co-Authored-By` or other agent/tool attribution trailers**, in commits or PR
descriptions.

**Code changes go through a PR** (anything under `aegis/`, `shaders/`, `tests/`, `games/`, `tools/`,
CMake or CI). Docs-only changes may be committed to `main` directly. `/implement <issue>` takes an
issue to an open PR.

**Planned work:** GitHub issues are the work items, grouped into milestones, with "Blocked by"
relations. **The assignee is the status:** whoever starts an item assigns it (`gh issue edit <n>
--add-assignee @me`), and an assigned issue is taken. Leave `needs-design` (an open decision) and
`idea` issues alone unless the owner asks; never run two items with the same `area/*` label in
parallel. Before starting an item, read its issue and the ADRs it cites (`docs/adr/`), then
re-check them against the current code. Finding IDs (`A-RENDER-05`) name the October audit, which is
out of the tree: `git show 144e655c:docs/audit/findings-a-render.md` (`verify-*.md` corrects them). Milestones run in two lanes of different areas, `render` and `content`,
each with an Active and a Planning milestone. An issue you file goes to its lane's Planning milestone
or `Backlog`, not an Active one, unless that Active milestone can't finish without it.
`/plan-milestone` holds the milestone rules, the lanes' areas and the issue format.

**Decisions are ADRs** in `docs/adr/`, indexed in its README; the `domain-modeling` skill writes
them. Cite one by its file name (`docs/adr/0007-engine-hosts-the-game.md`, linked in docs), never
by a bare number. An ADR may land in the feature PR that makes the decision; when parallel PRs pick
the same number, the one that merges later renumbers. `docs/roadmap.md` keeps each milestone's goal
and order.

1. **One worktree per PR.** `tools/new-worktree.sh <branch> [base]` creates
   `../aegis-engine.worktrees/<branch>` from `origin/main` with submodules, then configures and
   builds it; after the PR merges, `tools/remove-worktree.sh <branch> --delete-branch`. Name branches
   by kind: `fix/`, `test/`, `refactor/`, `cleanup/`, `style/`, `build/`, `feat/`, `ci/`, `docs/`. Run the
   `tools/*.sh` scripts from Git Bash (in PowerShell, `bash` is usually WSL:
   `& "C:\Program Files\Git\bin\bash.exe" tools/new-worktree.sh <branch>`). A Claude Code worktree
   (`.claude/worktrees/`) gets its submodules and a configured preset from the `SessionStart` hook
   (`tools/session-start.sh`) but still needs a build, and starts on a throwaway `claude/…` branch:
   `git branch -m <kind>/<name>` before the first commit. Keep worktree paths short on Windows (260
   character limit; some files sit about 140 characters below the root).
2. **Stack dependent PRs** instead of waiting: pass the unmerged branch as `base`, open the PR
   against it (`gh pr create --base <that-branch>`), and retarget it once the base merges
   (`gh pr edit --base main`). Keep stacks to two or three PRs.
3. **Commit in reviewable steps.** Keep mechanical changes (whole-file reformats, renames, deletions,
   signature churn) in their own commits, separate from logic changes.
4. **Verify each commit:** it builds clean and `ctest -LE device` passes. **Before pushing**, and for
   any change to device code, run the full `ctest` *including* the `device` tests (see "What CI
   covers"), and look at the `--screenshot` output after render changes. Across worktrees, run one
   `ctest` with device tests at a time per machine.
5. **Push and open the PR** with `gh pr create`; the body says `Closes #<issue>`. Fix CI failures
   with new commits; never force-push a PR under review.
6. **The owner merges.** Agents never merge a PR or enable auto-merge; they stop once CI is green and
   the PR is ready. PRs land as merge commits, so commit hashes survive: report the hashes of
   mechanical commits for `.git-blame-ignore-revs`.

## Unit tests
doctest covers **pure, GPU-independent logic** (no live `rhi::Device`), one executable per module,
`aegis-tests-<module>`. Before adding tests or extracting logic to test, read `tests/README.md`:
layout, naming, and the traps of test-visible signatures (no `vk::` types, `export import` from the
umbrella, partition cycles).

    ctest --preset windows-clang-debug -LE device         # skips the device checks (no GPU needed)
    ctest --preset windows-clang-debug -L rhi             # one module, by label
    ctest --preset windows-clang-debug -R "<case words>"  # cases by their TEST_CASE text

**A `TEST_CASE` name has no `;`**: CTest discovery splits on it and both halves run zero cases while
showing green.

## Verifying device code
Green unit tests are *not* sufficient when device code changed. `tests/device/` (`aegis-test`) is
the only `main()` that checks a real `rhi::Device` for failures; `games/` run the same stack but
nothing fails them on a regression. Debug builds enable core **and synchronization** validation, and
`aegis-test` exits non-zero on any failed check or validation error of severity *error* (messages go
to stderr; warnings print but don't fail, and the count is process-wide) — a run is only verified if
it exits 0.

- `aegis-test --headless` renders offscreen: no window, no surface, no swapchain, and stops after
  600 frames (or `--frames <n>`). `ctest` runs it as a `device`-labelled test. Use this by default.
  Its asset and IBL checks load the samples; without them `ctest` runs it with `--no-samples`, which skips those.
- `--scene` / `--gpu-driven` load the test scene file (`scenes/tests/aegis_test.scene` in the samples;
  `--scene-file <path>` picks another, such as `scenes/sponza.scene`) and run it through the app's `aegis::Engine` (CPU- / GPU-driven geometry) instead of the test triangle; `--screenshot <png>` saves the last headless frame — look
  at it after render changes. `ctest` runs both paths too.
- Headless does not touch surface, swapchain, acquire or present. When that code changed, also run
  `aegis-test --frames 600`: it opens a window briefly and exits by itself. On Windows it also checks
  `platform::Input` with synthetic key and mouse messages, so run it when platform code changes too.

**What CI covers.** The `Linux device tests (lavapipe)` job runs every `device` test on Mesa's lavapipe,
a software Vulkan driver, with validation (`ctest -L device -LE golden`). lavapipe has mesh shaders, so
both geometry paths run. It does not cover, so run these on a real GPU before pushing:
- the reference images (`golden`; software rendering differs in the last bits),
- the `slow` tests: the games shard of the lavapipe job skips them (`-LE slow`) to cut its wall time. They are the
  Examples editor variants (`-default`, `-play`, `-play-stop`, `-full`, `-folded`) whose Template twins run in CI,
  so only the Inspector flyout of a selected entity (`game-examples-editor-folded`) goes unchecked there. A local
  `ctest` runs them; `ctest -L slow` runs only them,
- windowed runs: surface, swapchain, acquire, present and `platform::Input` (`aegis-test --frames 600`),
- Windows drivers, and anything timing- or performance-related.
