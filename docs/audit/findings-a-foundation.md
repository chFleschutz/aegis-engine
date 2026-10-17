# Findings A: Foundation (architecture lens)

Scope: `src/aegis/math`, `src/aegis/utils`, `core/assert.h`, logging (`modules/aegis-log`, `core/logging.cppm`), the per-module `Error` types (rhi, renderer, render_graph, assets), `rhi :utility`, timers/profiling, file reading, hashing. Read-only; nothing built or run. Map facts were re-checked against the code.

**Map corrections.** (1) `decomposeTRS` is called with the *quat* overload: `Transform::rotation` is `glm::quat` (`scene/components.cppm:23`), so `editor_layer.cppm:106` resolves to `decomposeTRS(..., glm::quat&, ...)`. Production uses only the quat overloads of `forward/right/up/tranformationMatrix/decomposeTRS`. The whole vec3/Euler family has no non-test caller. (2) There are three flag-enum implementations, not one (A-FOUND-05).

## 1. Findings

### A-FOUND-01: Errors have no shared way to be described, so the low-level cause is dropped at every report site
- **Where:** `rhi/error.cppm` `rhi::Error{code, location}` + `makeError`; `renderer/renderer.cppm` `enum class renderer::Error`; `render_graph/error.cppm` `Error{message, optional<rhi::Error> rhi}`; `assets/error.cppm` `Error{code, message, optional<rhi::Error> rhi}`. Consumers: `engine.cppm` `Engine::check`, `Engine::createRenderer`; `render_graph/ibl.cpp`; `renderer.cpp` `Renderer::renderFrame`; `test/main.cpp`.
- **Observed:**
  - Four shapes. rhi has a code plus a debug-only `source_location` and no message. renderer is a bare enum. render_graph has a message and an optional rhi cause but no code. assets has a code, a message and an optional rhi cause.
  - Nothing formats an `rhi::ErrorCode` or reads `Error::rhi` or `rhi::Error::location`. A grep for `.rhi`, `.location`, `toString`, `formatter<` finds no reader.
  - Every report site prints `.message` only. Examples: `Engine::check` (`ALOG::fatal("Failed to {}: {}", what, result.error().message)`) and `ibl.cpp:84,137,168,198,260`. The `ibl.cpp` sites re-wrap with `makeError(std::format("...: {}", x.error().message))`, which also drops the assets `ErrorCode` and the nested rhi cause.
  - `Engine::check` needs `.message`, so it can't take rhi or renderer errors. Context, device and renderer creation therefore use `AGX_ASSERT_X(context, "Failed to create the Vulkan context")`, with no cause and nothing in release. `aegis-test` does the same (`return std::unexpected{ "Failed to create rhi context" }`, `test/main.cpp:151`).
  - `renderer` collapses every rhi failure into `RHIInitializationFailed` or `FrameBeginFailed` (`renderer.cpp:31,43,47,261,267,291,301,323`). `QueueSubmitFailed` and `SwapchainPresentFailed` are declared but never produced.
  - `Renderer::renderFrame` returns `void` and does `if (!frameInfo) return;` without logging. `Renderer::createTexture` returns `rhi::Error`, so the renderer's own API uses two error types.
- **Inference:** Having one error type per module is fine. What is missing is a shared rule for wrapping a lower layer's error and a shared way to describe one. So each layer re-stringifies by hand and the cause is lost.
  - Scenario 1: VMA runs out of device memory in `ResourcePool::createResources`. The engine logs "Failed to compile the frame graph: Failed to create frame graph texture 'X'" with no OutOfDeviceMemory. In release the assert is gone and the engine runs on with an uncompiled graph.
  - Scenario 2: `Queue::wait` fails with DeviceLost. `beginFrame` returns `FrameBeginFailed`, and `renderFrame` returns silently every frame from then on. The window freezes with no output at all.
- **Severity:** design problem (the silent `renderFrame` path is a bug on device loss)
- **Confidence:** high
- **Affects:** every new module or feature that returns errors (each copies one of four shapes); the planned `aegis.app` "std::expected all the way up" (engine-architecture.md step 1); tests can only assert codes in rhi and assets, while render_graph errors are message-only.
- **Proposal:** Keep per-module types but give them one shape and one rule.
  1. Each `Error` is `{ErrorCode code; std::string message; std::optional<Lower::Error> cause}`, with no cause for rhi. render_graph gains a code. renderer's enum becomes such a struct, or forwards `rhi::Error`.
  2. Two `makeError` overloads per module, as assets already has: `(code, msg)` and `(lowerError, msg)`.
  3. Each module exports `describe(const Error&) -> std::string`, which appends the cause. Example: `"compile frame graph: create texture 'X': out of device memory (device.cpp:422)"`.
  - Top-level code (`Engine::check`, `aegis-test`, later `aegis::app::run`) prints `describe()`. `renderFrame` returns `std::expected<void, Error>`.
  - Replaces about 15 hand-written `std::format("...: {}", e.message)` sites and the cause-less asserts in `createRenderer`.
  - Unlocks: actionable errors, one `check` for every module, and pure `describe` unit tests.
  - Deliberately not proposed: a shared `Error` base, `std::error_code` categories, or type erasure. Four 10-line `describe` functions suffice. Size S.

### A-FOUND-02: The foundation layer has the wrong shape: L0 is legacy and mostly dead, the live generic helpers sit in the RHI
- **Where:** `src/aegis/math` (`aegis.math`, partitions), `src/aegis/utils` (dotted submodules), `rhi/utility.cppm` (`alignTo`, `roundUpTo`, `overloads`/`match`, `FlagEnum`), `core/assert.h`, `core/logging.cppm`, `core/profiler.cppm`; CMake `math/`, `utils/`, `core/CMakeLists.txt`.
- **Observed:**
  - `aegis.utils` has a single importer, `core/profiler.cppm` (`Timer`, `RollingAverage`). `Color` and `hashCombine` sit in non-exported namespaces. `file::readBinary` and `ScopeTimer` have no caller.
  - `aegis.math`'s live API is the glm whitelist, `world::UP`, and the quat overloads `forward/right/up/tranformationMatrix/decomposeTRS`, plus `inFOV` (tests only). Everything else is unused (A-FOUND-06).
  - The generic helpers that *are* used live in `rhi/utility.cppm`, whose own TODOs say "Put this in some core module" and "move the flag enum concept stuff to core or its own module". `assets/material.cpp:83` uses `rhi::utility::alignTo` for CPU-side parameter packing.
  - math and utils `#include "core/assert.h"` (L0 to L1) through `target_include_directories(... ${CMAKE_CURRENT_SOURCE_DIR}/..)`, marked "TODO: remove this once everything is a module". `aegis-math` and `aegis-utils` link `Aegis::Log` but never use it.
- **Inference:**
  - The modules named "foundation" hold almost none of the cross-cutting code. The cross-cutting code (assert, log, flags, alignment, variant match) is spread over `core` (legacy, app-level), `rhi` (GPU) and a header.
  - A new subsystem that sits below rhi or beside it, such as `platform`, a job system or a streaming module, has nowhere to get these without importing rhi or legacy core.
  - The only upward edge in L0 (U3) is caused by two dead files, `perlin_noise.cppm` and `color.cppm`.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new low-level module, platform, the legacy-to-new conversion (agent-notes item 8), `tests/` (no home for foundation tests besides `rhi`).
- **Proposal:** See §3. Create one small new-style module for cross-cutting primitives, keep `aegis.math` as the glm boundary, delete `aegis.utils`, and move `Timer`/`RollingAverage` next to their only consumer (the profiler). Size M (mostly mechanical moves and deletes).

### A-FOUND-03: Logging is an engine-owned singleton the render stack cannot use, so there are two output styles
- **Where:** `core/logging.cppm` `aegis::Logging`; `modules/aegis-log/include/aegis-log/helper/singleton.h` `Singleton::instance`; `log.h` `ALOG::*`. Print sites: `renderer.cpp:191,223,240,349,359`, `rhi/context.cpp:55` (stderr), `render_graph/passes/gpu_driven.cpp:40`, `lighting_pass.cpp:90`; `core/assert.h` `assertFailed` (`std::cout`).
- **Observed:**
  - `static auto instance() -> T& { return *s_instance; }` with `s_instance = nullptr` until a `Logger` is constructed. Only `aegis::Logging`'s constructor creates one, and only `Engine` owns a `Logging` (`engine.cppm:321`).
  - No new-style module links `Aegis::Log` or calls `ALOG`. They `std::println` to stdout, except the validation callback, which writes to stderr. `assertFailed` writes to `std::cout` with "TODO: Replace this with logging".
  - Logger config is set by macros before each `#include`. `logging.cppm` defines `AEGIS_LOG_FATAL_NO_ASYNC` and, in release, `AEGIS_LOG_DISABLE_DEBUG/TRACE`. `engine.cppm` includes `<aegis-log/log.h>` with none of them, so `Detail::FATAL_ALLOW_ASYNC` differs between the two TUs.
- **Inference:**
  - Any library or unit-test code that calls `ALOG` without an `Engine` dereferences null. That is why the render stack avoided it, and it blocks logging in anything under test.
  - Renderer failures ("Failed to recreate swapchain") go to stdout with no severity, while the planned headless CTest runs judge by stderr and exit code.
  - The per-TU macros give the `Logger::fatal<...>` template two bodies (an ODR hazard). It is harmless today only because the logger runs `NO_THREAD`.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** any new subsystem that warns (jobs, streaming, hot reload); tests that capture or silence output; headless examples as CTests.
- **Proposal:** Add `aegis::log::{info,warn,error}(std::format_string, ...)` in the foundation module.
  - It works with zero setup and writes `[warn] ...` to stderr.
  - `log::setSink(std::function<void(Level, std::string_view)>)` lets the engine forward to aegis-log and lets tests capture output. Guard the sink with a single mutex now, since a job system will log from workers.
  - Replaces the 8 `std::println` sites, the `std::cout` in `assertFailed`, and the direct `ALOG` includes. aegis-log becomes an optional sink configured in one TU.
  - Not proposed: categories, async, per-module loggers. Size S.

### A-FOUND-04: `AGX_ASSERT` lives in the wrong layer, arrives through an include hack, and is used for input validation
- **Where:** `core/assert.h` (`AGX_ASSERT`, `AGX_ASSERT_X`, `AGX_UNREACHABLE`, `assertFailed`). Included by `math/perlin_noise.cppm`, `utils/color.cppm`, `core/asset_manager.cppm`, `engine.cppm`, `scene/registry.cppm` (19 uses), `scene/scene.cppm`, `scene/systems/camera_system.cppm`, `scripting/physics/motion_dynamics.cppm`, `editor/panels/scene_panel.cppm`, `examples/eval-scene/main.cpp`. New-style code uses `<cassert>` (`rhi/utility.cppm` `alignTo`).
- **Observed:**
  - Release: `#define AGX_UNREACHABLE(msg) static_cast<void>(0)`, so control falls through. Debug: print to stdout, `assert(false)`, `__builtin_trap()`.
  - `utils::Color::fromHex` asserts on a malformed string *and* returns `vec4{1}`. `CameraSystem::calcPerspectiveProjection` asserts `std::abs(camera.aspect - epsilon) > 0.0f`, which is true for any aspect except exactly epsilon, so the check guards nothing.
  - Two assert styles (`AGX_ASSERT*` vs `assert`); no written policy on assert vs `std::expected`.
- **Inference:**
  - Macros cannot be exported from a module, so a header is the right form. But it belongs to the lowest layer, on that target's PUBLIC include path, not reached via `..`.
  - The missing assert-vs-expected policy is what lets legacy code treat runtime input as programmer error. The engine-level consequences are already known (engine-architecture.md §2) and not re-reported.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every legacy conversion; release-only behaviour; removing upward header edge U3.
- **Proposal:** Move the header to the foundation target (`aegis/base/assert.h`, PUBLIC include dir) and delete the three `..` include hacks. Route `assertFailed` through `aegis::log::error`, keeping the debug trap. Make `AGX_UNREACHABLE` call `std::unreachable()` in release (C++23) instead of falling through. One assert macro family engine-wide, with the policy in §3. Size S.

### A-FOUND-05: Three flag-enum implementations; the generic one needs `using namespace rhi::utility` and applies to every enum
- **Where:** `rhi/utility.cppm` `FlagEnum`, `operator| & ~ |= &=`, `hasFlag`; `rhi/common.cppm` `isFlagEnum<BufferUsage/ImageUsage>`; `platform/input.cppm` hand-written `operator|/&` for `Modifiers`; `editor/panels/menu_bar_panel.cppm` plain `enum MenuFlags`. Using-directives: `renderer.cpp:276`, `resource_pool.cpp:206`, `post_effects.cpp:77`, `assets/texture.cpp:53`, `static_mesh.cpp:32`, `material.cpp:98`, `test/main.cpp` (7).
- **Observed:**
  ```cpp
  template<typename T>
  concept FlagEnum = std::is_enum_v<T> /*&& isFlagEnum<T>*/;
  ```
  - The operators live in `aegis::rhi::utility` and the enums in `aegis::rhi`, so ADL never finds them. Combining usage flags needs a using-directive (13 of them).
  - `platform` (below rhi) re-implements `|`/`&` for `Modifiers`.
  - `MenuFlags` defines `None = 1 << 0`, so `None` is a set bit.
- **Inference:**
  - Each new flag enum picks one of three patterns.
  - Inside any scope with the directive, *every* enum gets bitwise operators: `rhi::Format::R8_UNORM | rhi::Format::RGBA8_SRGB` compiles and yields a non-enumerator, which falls into `default:` of the vk conversion switches. No such misuse was found today.
- **Severity:** design problem
- **Confidence:** high (lookup rules); low that a misuse exists today
- **Affects:** new flag enums (pipeline stages, input modifiers, feature toggles, job priorities); every file building rhi descs.
- **Proposal:** Move one implementation to the foundation module with an explicit opt-in that ADL can see. Either:
  - a one-line `AEGIS_FLAGS(EnumType)` macro in the enum's namespace that defines the operators there, or
  - restore the `isFlagEnum<T>` gate and add `using aegis::base::operator|;` etc. once inside each owning namespace.

  Replaces the 13 using-directives and the `Modifiers` operators. Makes `Format | Format` a compile error, and lets platform use the shared version. Size S.

### A-FOUND-06: Most of `aegis.math` and all of `aegis.utils` is dead or invisible; delete rather than move
- **Where:** `math/perlin_noise.cppm` (`PerlinNoise1D` in non-exported `namespace aegis::math`); `math/interpolation.cppm` (`lerp`, `percentage`, `sigmoid01`, `fastSigmoid01`, `tanh01`); `math.cppm` vec3 overloads of `forward/right/up/tranformationMatrix/normalMatrix/decomposeTRS`, `normalMatrix(quat)`; `utils/color.cppm`, `utils/file.cppm`, `utils/utils.cppm` (`hashCombine`, not exported), `utils/timer.cppm` `ScopeTimer`.
- **Observed:**
  - No non-test caller for any of the above (grep over src/examples, excluding `ai/`).
  - `PerlinNoise1D` and `Color` cause the only L0 includes of `core/assert.h`.
  - `math::lerp` duplicates `std::lerp`, and `PerlinNoise1D` itself calls `std::lerp`.
  - `file::readBinary` returns an empty vector for both "missing" and "empty" files.
  - `Timer` uses `high_resolution_clock`, while the engine and frame graph use `steady_clock`.
  - The public name `tranformationMatrix` is misspelled.
- **Inference:** Dead foundation code costs reading time, misleads about what is supported, and causes U3. Nothing in the planned features needs it. Perlin and sigmoid curves can come back from git history when a feature needs them.
- **Severity:** polish
- **Confidence:** high
- **Affects:** U3 edge, the math test surface, readers of 36 importing files.
- **Proposal:** Delete the listed items and the `aegis-utils` target. Move `Timer` (on `steady_clock`) and `RollingAverage` into `core/profiler.cppm`, their only consumer. Rename `tranformationMatrix` to `transformMatrix`. Size S.

### A-FOUND-07: `aegis.math` mixes two Euler conventions in one overload set (latent bug)
- **Where:** `math/math.cppm` `forward/right/up(const glm::vec3&)`, `tranformationMatrix(..., const glm::vec3& rotation, ...)`, `normalMatrix(const glm::vec3&, ...)`, `decomposeTRS(..., glm::vec3& rotation, ...)`; `tests/math/math.test.cpp`.
- **Observed:**
  - `right(vec3)` returns `(cy*cz, cy*sz, -sy)`, the first column of Rz·Ry·Rx. That is glm's convention, which is also the one `glm::quat(vec3)` and `glm::eulerAngles` use.
  - `tranformationMatrix(vec3)` builds Ry·Rx·Rz: c1/s1 from `rotation.y`, c2/s2 from `.x`, c3/s3 from `.z`, third column `(c2*s1, -s2, c1*c2)`. `normalMatrix(vec3)` copies the same formula.
  - `decomposeTRS(vec3&)` returns `glm::eulerAngles(...)`, which is the ZYX convention.
  - Tests cover `forward/right/up(vec3)` (zero and one orthonormality case) and the quat versions only at identity. Nothing tests the matrix functions or round trips.
- **Inference:** Scenario: `r = {0, 0.5, 0.5}`.
  - `forward(r) = (-0.479, 0.878, 0.0)`, but column 1 of `tranformationMatrix({}, r, {1,1,1})` is `(-0.421, 0.878, 0.230)`.
  - `tranformationMatrix(decomposeTRS(M))` does not reproduce M whenever x or y is non-zero together with z.
  - Production uses only the quat path today, so nothing is visibly broken. The first feature that stores Euler angles (editor rotation fields, a serializer) will pick the wrong overload.
  - The quat path the engine actually uses has no non-trivial test.
- **Severity:** bug (latent; no production caller)
- **Confidence:** high (derived from the formulas; not executed)
- **Affects:** editor transform editing, scene serialization, any Euler-angle feature; `tests/math`.
- **Proposal:** Delete the vec3/Euler overloads (A-FOUND-06). If Euler input is needed, convert once via `glm::quat(eulerRadians)`. Add tests for the quat path: `transformMatrix`/`decomposeTRS` round trip with a non-trivial rotation and scale, and `forward(q) == transformMatrix(q)[1]`. Size S.

### A-FOUND-08: The used camera math sits in an ECS system, untested; its inverse view matrix is wrong
- **Where:** `scene/systems/camera_system.cppm` `CameraSystem::calcViewMatrix`, `calcPerspectiveProjection`; `scene/components.cppm:130` `Camera::inverseViewMatrix`; normal matrix inline in `render_graph/passes/gpu_driven.cpp:52` and `static_mesh_render_system.cpp:56`; `render_graph/frustum.cppm`.
- **Observed:**
  - The view matrix has rows s, u, -f (`viewMatrix[0][2] = -f.x; ... viewMatrix[3][2] = glm::dot(f, transform.location)`).
  - The inverse is built with column 2 = **+f** (`inverseViewMat[2][0] = f.x; [2][1] = f.y; [2][2] = f.z`). The inverse of rotation rows (s, u, -f) has columns (s, u, -f).
  - `inverseViewMatrix` has no reader today.
  - The projection is hand-built inside the system (RH, depth [0,1], Y flipped).
  - The normal matrix is `glm::inverse(glm::mat3{model})` at two sites, while `math::normalMatrix` goes unused.
- **Inference:**
  - `inverseViewMatrix * viewMatrix != I`: the forward axis is mirrored. The first feature that reads it (world-position reconstruction, mouse picking, view-space lighting) gets wrong results.
  - The math that is actually used is pure, but it can only be reached by building a scene with entt.
  - This is the textbook case for "math in math, with tests".
- **Severity:** bug (latent)
- **Confidence:** high
- **Affects:** picking, deferred position reconstruction, SSAO and other screen-space effects; unit tests for camera math.
- **Proposal:** Move these into `aegis.math` as free functions, leaving `CameraSystem` with one-line calls:
  - `viewMatrix(position, forward, up) -> mat4` and its inverse via `glm::inverse` or the transpose-based form;
  - `perspectiveZeroToOne(fovY, aspect, near, far) -> mat4`;
  - `normalMatrix(mat4) -> mat3`.

  Add tests: `inverse * view == I`, projection maps near to 0 and far to 1, and `normalMatrix` agrees with the inverse-transpose. Replaces 40 lines in `CameraSystem` and the two inline normal-matrix sites. Size S.

### A-FOUND-09: Process globals in the foundation: global RNG, profiler singleton, logger singleton
- **Where:** `math/random.cppm` `Random::s_generator` (`inline static std::mt19937`, seeded from `random_device`) and `generator()`; `core/profiler.cppm` `Profiler::instance()`, `ScopeProfiler`; aegis-log `Singleton` (A-FOUND-03).
- **Observed:**
  - `Random` is used only by the examples (`eval-scene` reaches into `generator()` about 20 times, `simple-scene` 3) and the dead `Color::random`.
  - `ScopeProfiler` builds a `std::string` per scope and writes into a global `unordered_map` from `Scene::update` (`scene.cppm:66`) and the engine loop.
  - `AGX_PROFILE_SCOPE/FUNCTION` are `#define`d inside a module interface, so they cannot reach importers. They are unused, and `profiler##__LINE__` does not expand `__LINE__`.
- **Inference:**
  - Today none of these blocks a unit test of pure logic, but each one does in a specific situation. A test of `Scene::update` leaks timings into a global shared across cases. Code that draws random numbers is non-deterministic unless someone remembers `seed()`.
  - Once work runs on threads, all three are data races: unsynchronized `mt19937`, `unordered_map::operator[]`, and the logger's `s_instance`.
- **Severity:** design problem
- **Confidence:** medium (impact depends on the job feature)
- **Affects:** extension B (jobs), deterministic tests, scene tests.
- **Proposal:**
  - Delete `math::Random`; examples own a `std::mt19937 rng{seed}` (NOW, trivial).
  - Delete the dead macros.
  - Leave `Profiler` as is until the app redesign: engine-architecture.md already plans to remove globals, and two profilers is a known item. Make it per-thread or pass it explicitly WITH THE FEATURE. Size S.

### A-FOUND-10: SPIR-V and binary file reading is written three times and the shared one is unused
- **Where:** `render_graph/shader.cpp` `loadShader`; `test/main.cpp:47` `loadSPIRV`; `utils/file.cppm` `file::readBinary`.
- **Observed:** `loadShader` and `loadSPIRV` are near-identical (open with `ate`, check size % 4, read into `vector<uint32_t>`) but return different error types (`render_graph::Error` vs `std::string`). `readBinary` returns `vector<char>`, can't distinguish failure from an empty file, and has no caller.
- **Inference:** Small duplication, but it is the one file-IO path the engine has. The divergence (a size-0 check in one copy only) is how the copies drift.
- **Severity:** polish
- **Confidence:** high
- **Affects:** shader hot-reload, `aegis-test`.
- **Proposal:**
  - Split `render_graph::loadShader` into `loadSpirv(const std::filesystem::path&)`, which takes an absolute path, plus the existing directory wrapper.
  - `aegis-test` calls `loadSpirv`.
  - Delete `utils::file`.
  - Add a generic `readFile -> std::expected<std::vector<std::byte>, ...>` only when a second non-shader caller appears. Size S.

### A-FOUND-11: glm is a well-contained dependency, but the GPU-layout conversion is repeated per pass
- **Where:** `math/math.cppm` (glm GMF, `export namespace glm { using ... }` whitelist); `gpu_driven.cpp:52-58` `makeInstance`, `static_mesh_render_system.cpp:55-61`, `scene_update_pass.cpp:34-36`, `screen_passes.cpp:87`.
- **Observed:**
  - glm is `#include`d only in `math.cppm`'s GMF, with one set of `GLM_FORCE_*` defines, and exported through a whitelist.
  - Every glm symbol used outside math is in the whitelist (23 distinct symbols; checked by grep).
  - The CPU/GPU interface uses glm types directly: `.model = glm::mat3x4{ glm::rowMajor4(model) }` plus `normalRow0..2` from `glm::inverse(glm::mat3{model})`, identical in two passes, and `rowMajor4` in four places.
- **Inference:**
  - Single inclusion is good: there is no risk of two glm configurations, and therefore no layout mismatch between TUs.
  - The repeated "row-major affine plus normal rows" conversion is correctness-sensitive (the transpose is implied by the layout), and every new instanced pass will copy it.
- **Severity:** polish
- **Confidence:** high
- **Affects:** new geometry passes, the CPU/GPU struct checks (build group).
- **Proposal:** Keep the whitelist. Add one pure helper next to the GPU structs (render_graph `detail`), for example `toGpuTransform(const glm::mat4&) -> {mat3x4 model; vec3 normalRows[3]}`, and test it. A math-level "GPU matrix" type is not needed. Size S.

## 2. Extensibility: what the foundation must provide, NOW vs WITH THE FEATURE

General features (any new subsystem):
- Shared error shape, wrapping rule and `describe` (A-FOUND-01): **NOW**. Every new module will otherwise copy one of four shapes, and `aegis.app`'s error plumbing depends on it.
- A setup-free log facility (A-FOUND-03): **NOW**. New code otherwise adds more `std::println`.
- Assert header in the foundation, plus policy (A-FOUND-04): **NOW**, together with the module move. It is cheap and removes U3.
- Shared flag enums (A-FOUND-05): **NOW**. Every new desc or enum hits it.
- Camera, transform and normal-matrix math in `aegis.math` with tests (A-FOUND-07/08): **NOW**. It is small, fixes two latent bugs, and editor or picking work will build on it.
- Generic file IO, hashing (`hashCombine`), colour palette, interpolation curves, noise: **NOT NEEDED**. Re-add with the first real caller.

Extension B, multithreaded or job-based frame work. Today the foundation has *no* concurrency primitive. The only atomic in the tree is `rhi/context.cpp` `g_validationErrors`. Nothing is thread-safe, which is honest: nothing claims to be.
- Thread pool or job system (submit, wait, parallel-for): **WITH THE FEATURE**. Design it against the first real workload: per-pass recording, asset decode, or scene extract.
- Per-thread scratch or frame allocators: **WITH THE FEATURE**.
- Thread-safe profiler (per-thread buffers or an explicit profiler object): **WITH THE FEATURE**.
- Thread-safe deletion and upload queues and command pools per thread: **WITH THE FEATURE** (render group's scope).
- The log sink guarded by one mutex: **NOW**, but only as part of A-FOUND-03 (about one line of extra cost).
- Removing the global RNG: **NOW**. Trivial, and it prevents the first worker-thread race; examples own their RNG.
- Lock-free queues, fibers, a custom atomic or spinlock layer, a task-graph framework ahead of a workload: **NOT NEEDED**. `std::jthread`, `std::mutex`, `std::latch` and `std::atomic` are enough until a profile says otherwise.
- Prerequisite that is not foundation: render passes can't run in parallel while they read `scene::Scene&` (U2). engine-architecture.md's `SceneView` is the real enabler.

Testability blockers in the foundation today:
- (1) The ALOG null singleton: any library code that logs crashes under doctest.
- (2) Pure math trapped in `CameraSystem` and in passes.
- (3) Global `Profiler` and `Random` state leaks between test cases.
- (4) `AGX_ASSERT` traps the process, the same as `assert`. That is acceptable for programmer errors and wrong for input validation (`Color::fromHex`).

## 3. Target shape

**Modules.**
- **`aegis.base`** (name open; `aegis.core` is taken by legacy app code): a new-style module with no dependencies. It holds:
  - `assert.h` (a header, because macros can't be exported; PUBLIC include dir);
  - `:log` (`info/warn/error`, `setSink`);
  - `:flags` (`FlagEnum` with opt-in, operators, `hasFlag`);
  - `:align` (`alignTo`, `roundUpTo`, moved from `rhi::utility`);
  - `:variant` (`overloads`, `match`).

  That is roughly 250 lines. `tests/base` takes over `tests/rhi/utility.test.cpp` and adds flags/log tests.
- **`aegis.math`** stays the glm boundary (whitelist kept) and depends only on `aegis.base`. It contains the quat transform functions (renamed `transformMatrix`), `decomposeTRS(quat)`, the camera view/projection, `normalMatrix(mat4)`, `world::*`, and `inFOV`, each with tests.
- `rhi`, `platform` and everything above import `aegis.base`.
- **Deleted:** `aegis.utils` (target and all five units), `math:interpolation`, `math:perlin_noise`, `math:random`, the vec3/Euler overloads, the three `..` include-dir hacks, and the unused `Aegis::Log` links in math, utils and scripting.
- `Timer` and `RollingAverage` move into `core/profiler.cppm`.
- Each module keeps its own `Error` partition, so no cross-module error type is needed.

**Why not more.**
- No shared `Error` base class or error categories: four modules, each knowing its own codes.
- No logger hierarchy, no allocator framework, no job system, no hashing or IO utilities without a caller.
- Each item in `aegis.base` has at least two importing modules today (flags: rhi, platform; align: rhi, assets, render_graph; log: rhi, renderer, render_graph, engine; assert: legacy layers plus math). Each removes a concrete duplication or upward edge.

**Error policy.**
- Anything that depends on input, files, the device, the OS or the user returns `std::expected<T, Module::Error>`.
- Each `Error` is `{code, message, optional cause}`, built with `makeError(code, msg)` or `makeError(lower, msg)`, and printed only via `describe()`.
- A layer adds context and never re-stringifies. It never discards an error silently: `std::ignore` needs a one-line reason.
- Only the top level (`aegis::app::run`, `aegis-test` `main`, examples) logs the description and turns it into an exit code.
- `void` frame functions that can fail (`renderFrame`) return `expected`.

**Log policy.**
- Library code logs only recoverable, degraded behaviour ("no environment; skipping lighting") via `aegis::log::warn`.
- Errors are returned, not logged, except at the top level.
- Default output is stderr with a severity tag, so the headless CTests see it. The engine may install an aegis-log sink and tests a capturing one.
- Vulkan validation goes through `log::error` and keeps its counter.

**Assert policy.**
- Asserts are for programmer errors and invariants only: an index in range, a handle not stale, a precondition the caller controls.
- One family (`AGX_ASSERT`, `AGX_ASSERT_X`, `AGX_UNREACHABLE`): debug-only, routed through `log::error`, then trapping. `AGX_UNREACHABLE` becomes `std::unreachable()` in release.
- Never assert on file contents, user input or device results. Those return `expected`, which replaces the "assert, then return a fallback" pattern.
- `<cassert>` `assert` in new-style code migrates to `AGX_ASSERT` when touched; this is not a sweep.

## 4. What works well and should not change

- **rhi `Error` + `makeError` with a debug-only `std::source_location`.** It is cheap, has value semantics, and callers can branch on codes (`upload.cppm` retries on `NotReady`, `renderer` resizes on `OutOfDate`, `test/main.cpp:308` checks `FormatNotSupported`). Keep it and add `describe`.
- **The assets `Error`** (`code + message + optional<rhi::Error>`, two `makeError` overloads) is already the target shape. Use it as the template for the other modules.
- **glm included once, behind a whitelist.** One configuration, no layout drift across TUs, and an obvious place to add a symbol.
- **`std::expected` everywhere in the new stack and no exceptions.** Keep it; the gap is only description and propagation.
- **The extract-don't-mock pattern** (`RingAllocator`, `rhi::detail` helpers, tested `alignTo`/`roundUpTo`) and one doctest executable per module. `tests/base` and the camera-math tests should follow exactly this pattern.
- **No threads and no pretend thread-safety.** Containers that are unsynchronized by design are the right default until the job feature defines real sharing.
