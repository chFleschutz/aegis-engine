# Findings, lens C: conventions

## 1. Convention table
Verdicts: KEEP / CHANGE / DROP. Cost: S = docs or under 5 files, M = one module (5-30 files), L = cross-module.

| # | Convention | Stated in | Followed? | Verdict | Reason | Cost |
|---|---|---|---|---|---|---|
| 1 | Trailing return types | AGENTS.md (new RHI) | New stack: near-total (5 leading-style hits in rhi/renderer/platform/assets/render_graph `.cppm`). Legacy mixed, about 93 leading vs 66 trailing by regex (`LayerStack::push` returns `T&`, `ScriptManager::operator=`, `void run()`). | KEEP, extend to all | Costs nothing and legacy is half converted. Mixed state is the only cost. | S per module, rides along with other edits |
| 2 | `[[nodiscard]]` | AGENTS.md | 374 hits in 61 files, also in legacy. Applied to trivial getters and to `expected` returns alike. | CHANGE | Valuable on `expected`/handle/ownership returns (`std::ignore = m_device->flushUploads()` in `engine.cppm` shows it works). Noise on getters. Rule: mandatory for fallible and ownership returns, optional for getters. No mass edit. | S (docs) |
| 3 | `Desc` nested config structs | AGENTS.md | 27 `Desc` structs, used at nearly all rhi/renderer/assets creation sites. Exceptions: `StagingBuffer::create(Device&,size)`, `Ibl::create(device)`, `MaterialInstance::create(device,template,framesInFlight)`, `StaticMesh::create(device,data,name)`, `DefaultAssets::create(device,framesInFlight)`, `Swapchain::create` x3 overloads. Names differ (`DefaultPassesDesc`, `TextureDesc`/`BufferDesc`, `Pipeline::GraphicsDesc`). | KEEP | Readable call sites. Rule: Desc when 2+ non-obvious params or any optional one; a single obvious dependency stays positional. | S (rule), M if exceptions normalised |
| 4 | `create()` returns `std::expected` | AGENTS.md | GPU-owning types yes. `platform::Window` asserts, `Engine` ctor asserts, `ScriptManager`/`Scene` n/a. | KEEP | Only way to make setup failure testable and release-safe. | M (Window, Engine) |
| 5 | Per-module `Error` + `makeError` | AGENTS.md | Four shapes: `rhi::Error{code, debug source_location}` (no text), `assets::Error{code,message,optional<rhi::Error>}`, `render_graph::Error{message,optional<rhi::Error>}` (no code), `renderer::Error` bare enum. Separate `makeError` overload sets. | CHANGE | See C-02. `Engine::check<T,E>` reads `.message`, which `rhi::Error` lacks. | M |
| 6 | Never throw | AGENTS.md | Zero `throw`/`catch` outside ai/. | KEEP | Consistent. | none |
| 7 | `AGX_ASSERT*` in legacy | AGENTS.md | 45 uses in 9 files (`registry.cppm` 18, `engine.cppm` 8). Compiled out under NDEBUG. | DROP | See C-01. | M |
| 8 | `assert()` for invariants in new code | AGENTS.md | 63 hits in 18 files. | KEEP, single mechanism | Two assert systems coexist. Keep plain `assert`; delete `core/assert.h` after legacy port. | S after #7 |
| 9 | Packed generational handles as public currency | AGENTS.md | Used for Buffer/Image/ImageView/Sampler. Violations: `Window::glfwWindow()` returns `GLFWwindow*`; assets hold `rhi::Device&` + `shared_ptr` (M16). | KEEP, narrow wording | Right for GPU resources. CPU assets may use `shared_ptr`; say so. | S |
| 10 | `Handle` vs `Descriptor` naming | AGENTS.md | rhi follows it. `render_graph::ResourceHandle{index}` is a plain index sharing the name (M10). | CHANGE (small) | Add `...Id` for plain indices; rename the render_graph one. | S-M (~15 files) |
| 11 | Bindless + BDA + one layout | AGENTS.md, shaders README | Followed throughout. | KEEP | Why `Pipeline::*Desc` is small and a new pass needs no layout work. | none |
| 12 | `vk::` never crosses public API | AGENTS.md | False (M1). 14 public accessors return `vk::`: `vk()` on Buffer/Image/ImageView/Sampler/QueryPool, `handle()` on CommandBuffer/CommandPool/Pipeline/Swapchain/Fence/Semaphore, `operator->` on Device/Queue/CommandPool/Semaphore, `Device::device()`, `create(const vk::raii::Device&...)`. Only outside user: `ui_pass.cpp`. | CHANGE | See C-05. | S docs, M rename |
| 13 | `import vulkan_hpp` only in rhi | AGENTS.md | 30 rhi files + `render_graph/passes/ui_pass.cpp`. | KEEP (name the exception) | ImGui backend legitimately needs raw handles. | S |
| 14 | `.cppm` + `.cpp` | AGENTS.md | New stack mostly yes. Legacy and `engine.cppm` (340 lines) all inline; 0 `.cpp` in core/scene/scripting/editor/ui/utils/math. | KEEP | Rule: non-template bodies over one line go in `.cpp`. | M-L |
| 15 | Partitions vs dotted submodules | AGENTS.md | rhi, render_graph, assets, renderer, platform, math = partitions (math contradicts the AGENTS table, M4). core/scene/scripting/utils = dotted. `aegis.editor.panels` mixes both. Strays: `aegis.physics.motion_dynamics` (in scripting/), `aegis.defaults` (file `scene_defaults.cppm`), `aegis.editor` (file `editor_layer.cppm`). | CHANGE: partitions everywhere | One import per subsystem for tests and CMake; dotted names cause the PRIVATE-link mismatch in map imports section 5. | M per module |
| 16 | Umbrella re-export | tests/README | `rhi.cppm` re-exports 23 of 26 partitions. | KEEP | Umbrella is the public surface. Add: every partition is exported or commented "internal". | S |
| 17 | `aegis::rhi::detail` for helpers | tests/README | Used for `chooseSwapchainExtent`, `calcMipLevels`, `assets::detail::*`, `render_graph::detail::grownCapacity`. Other tested helpers are not in detail (`rhi::bytesPerTexel`, `formatName`, `copyAlignment`, `utility::alignTo`). | CHANGE (clarify) | Two homes exist with no rule. Decide: test-only exports go in `detail`; reusable math goes in `utility`. | S |
| 18 | "Extract rather than mock" | tests/README | Proven (RingAllocator, swapchain policy, schedule, frustum, DrawBatchRegistry). Legacy `Registry` logic is pure and has zero tests. | KEEP | Strongest convention here; apply to renderer pacing/resize, input state. | S per extraction |
| 19 | One test exe per module | tests/README | 4 exes (rhi, render_graph, assets, math). None for renderer/platform/core/scene/scripting/ui/editor/utils. | KEEP | `aegis-tests-render_graph` already links the legacy scene (U2). | S per module |
| 20 | Test naming, CHECK over REQUIRE | tests/README | Suite strings drift: `"aegis::math basis vectors"`, `"rhi::swapchain policy"`. Real rule is `<ns>::<type or function>`, without `aegis::`. | CHANGE (loosen) | Fix README wording and 2 outliers. Suite names are only for reading (`-R` does not work). | S |
| 21 | `m_` members | AGENTS.md | Only violation: `LayerStack::m_Layers` (6 uses). | KEEP | Cheap, nearly fully followed. | S |
| 22 | snake_case namespaces/modules | AGENTS.md | Violations: `Profiler`, `ScopeProfiler`, `Logging`, `Engine` and all components in bare `aegis` (M13); `export namespace glm`/`entt` shims; `aegis::physics` for a scripting file. | CHANGE | Fix per module when ported. Document glm/entt reopening as a deliberate shim or move it. | M |
| 23 | snake_case filenames | AGENTS.md | Followed. `scene_defaults.cppm`/`scene_description.cppm` don't match their module names. | KEEP | Add: file name = last module or partition name. | S |
| 24 | PascalCase example targets | AGENTS.md | `Simple-Scene` etc. vs `aegis-engine`, `aegis-tests-*`. | DROP | Cosmetic. Don't rename (breaks docs and CI); stop calling it a convention. | S |
| 25 | Allman / 4 spaces / un-indented namespaces | AGENTS.md, .clang-format | Tool-enforced; legacy reformatted in b5877948. | KEEP | Owned by clang-format. | none |
| 26 | Include every std header per TU | AGENTS.md | Only CI catches violations; scanner never checked in (agent-notes idea 4). | KEEP + automate | A rule people must remember should be a CI job. | S-M |
| 27 | `clang-format off` guards | AGENTS.md | 6 uses in 3 files. | KEEP | Used sparingly. | none |
| 28 | Diff-only formatting | AGENTS.md | `git clang-format` broken on this machine; legacy whole-file formatted anyway. | CHANGE | Tree is now normalised: "keep tree formatted, CI runs `clang-format --dry-run`". | S |
| 29 | Commit/PR rules | AGENTS.md | `.git-blame-ignore-revs` does NOT exist at repo root (only in `external/meshoptimizer`) although b5877948, 561096dc, 5c4b20d9 are mechanical. Per-commit full ctest incl. device is manual. The "no Co-Authored-By" rule conflicts with the harness-injected trailer. | KEEP; fix | Create the file with the 3 hashes at merge. Owner decides the trailer conflict. | S |
| 30 | Shader conventions | shaders README, AGENTS.md | Scalar and row-major set in `shaders/CMakeLists.txt` (not for the test shader, M7). Push constant variable: `push` (8 shaders), `pc` (5), `roundTrip` (test); README says `constants`/`DrawConstants`. C++ mirrors checked by nothing beyond `sizeof<=128`. | CHANGE | Standardise on `push` + `PushConstants`. Add `static_assert(sizeof/offsetof)` per mirror. | S docs, M asserts |
| 31 | Comments / doc comments | unwritten | Legacy: `///` with `@brief/@note`, used by no tool. New stack: sparse "why" comments. Stale: "old stack", "(Phase 3)" (M15). | CHANGE | One style: plain `//`, state contracts (ownership, preconditions, errors), no Doxygen tags, no migration notes. | S |
| 32 | println vs a logger | unwritten | `ALOG` only in `core/logging.cppm` + `engine.cppm`. Render stack and `aegis-test` use std print/cout/cerr (41 hits in 8 files; 28 in `aegis-test`). `assert.h` prints via `std::cout` with `TODO: Replace this with logging`. | CHANGE | Lower layers can't log (no dependency on core) and shouldn't. Return errors; log only at the app edge. | S |

## 2. Findings

### C-01 Release builds dereference results the asserts were meant to guard
- Where: `src/aegis/engine.cppm`, `Engine::createRenderer` and `loadDefaultAssets`; `core/assert.h`
- Observed: `AGX_ASSERT_X(context, ...); m_context = std::move(*context);` and the same for device, renderer and `ibl` (then `m_ibl.emplace(std::move(*ibl))`). `AGX_ASSERT_X` is `static_cast<void>(0)` under NDEBUG. `Engine::check` returns `T{}` after the assert.
- Inference: the assert is used for runtime conditions the environment can cause (no GPU, missing file). This is the root of engine-architecture problem 2.
- Severity: bug (release only). Confidence: high.
- Failure scenario: a release build on a machine with no usable Vulkan device. `Context::create` returns `unexpected`, the assert is gone, `*context` on an empty `expected` is UB and crashes with no message.
- Affects: all examples; any future test of the app layer (cannot construct without aborting).
- Proposal: `std::expected` for fallible operations and plain `assert` for invariants. Replaces `core/assert.h` and `Engine::check`.

### C-02 Four incompatible Error types
- Where: `rhi/error.cppm`, `renderer/renderer.cppm` (`enum class Error`), `assets/error.cppm`, `render_graph/error.cppm`
- Observed: see table row 5. `rhi::Error` carries no message. `render_graph::Error` carries no code.
- Inference: each layer invented its own wrapping, so crossing layers needs manual translation and loses detail. No caller can write one `if (!r) log(r.error())`.
- Severity: design problem. Confidence: high.
- Affects: any feature crossing layers; stable error codes in render_graph tests; app-level exit codes.
- Proposal: one `aegis::Error{ErrorCode code; std::string message; std::source_location where (debug)}` in a leaf module `aegis.error` below rhi, with one `makeError(code, fmt, args...)`. One `ErrorCode` enum with domain groups. Wrapping appends context. Replaces 4 types and about 9 `makeError` overloads. Cost is only on the error path.

### C-03 Legacy ownership: leaked scripts and a no-op reset
- Where: `scripting/script_manager.cppm` `addScript`; `scene/scene.cppm` `reset`; `scene/registry.cppm` `destroy`
- Observed: `T* script = new T(...)` held in `std::vector<ScriptBase*>`, never deleted (the destructor only calls `end()`). `Scene::reset()` body is `// TODO: Clear old scene`. `Registry::destroy` has a TODO about destroying scripts.
- Inference: no ownership convention; raw owning pointers in an otherwise `unique_ptr` codebase.
- Severity: bug. Confidence: high.
- Failure: call `Engine::loadScene` twice. Systems are added twice and the first scene's scripts keep updating.
- Proposal: `unique_ptr` ownership inside the World redesign (engine-architecture step 3). Don't patch in place.

### C-04 Header-only legacy modules force rebuilds and block tests
- Where: `engine.cppm` and all legacy modules
- Observed: no `.cpp` in 7 legacy dirs. agent-notes: a comment-only `.cppm` edit rebuilds all importers (about 180 steps for render_graph). `aegis.scene` has 19 importers.
- Inference: build-time and test-linking problem, not style.
- Severity: design problem. Confidence: medium (build numbers from notes, not measured).
- Proposal: apply the "bodies in `.cpp`" rule while porting, not as a separate pass.

### C-05 The `vk::` boundary rule is false as written; accessors inconsistent
- Where: `rhi/*.cppm` accessors; AGENTS.md sentence "linked PRIVATE so vk:: never crosses the public API"
- Observed: see table row 12. Three spellings: `vk()`, `handle()`, `operator->`.
- Inference: the "PRIVATE link" argument hides a real escape hatch; the spelling variety suggests no decision was made.
- Severity: polish. Confidence: high.
- Proposal: one `native()` accessor; `operator->` only on `Device`; document the `ui_pass` interop exception; fix the AGENTS wording.

### C-06 Factory signatures defeat the `Desc` rule
- Observed: see table row 3. `framesInFlight` is threaded as a loose uint through 3 factories.
- Severity: polish. Confidence: medium.
- Proposal: write the rule and apply it when files are touched. No mass edit.

### C-07 Doc facts are stale
- Observed: tests/README says 104 cases, "one device test", and a layout listing only rhi/math. Reality: 108 cases, 3 device tests, 4 test dirs (M8). AGENTS.md shader and build claims are wrong (M5, M6).
- Severity: polish. Confidence: high.
- Proposal: drop counts from prose (section 5).

### C-08 `aegis-test` is a ~1400-line ad hoc main
- Where: `src/aegis/test/main.cpp`
- Observed: about 1430 lines, many direct `m_device->` calls, hand-rolled if-fail checks, its own private `Engine` class. CTest sees 3 entries for dozens of checks.
- Inference: the only GPU check sits outside doctest, so the naming and reporting rules don't apply and failures aren't reported per check.
- Severity: design problem. Confidence: medium (skimmed by grep).
- Affects: unit-testability goal.
- Proposal: move the checks to `tests/device/*.test.cpp`, label `device`, with a shared `DeviceFixture` (replaces the private Engine class once the app has `create()`).

### C-09 Dead macros in a module interface
- Where: `core/profiler.cppm` lines 12-13
- Observed: `AGX_PROFILE_SCOPE` is defined inside a module interface (macros don't cross module boundaries). `##__LINE__` pastes the literal token. Zero users.
- Severity: polish. Confidence: high.
- Proposal: delete.

### C-10 Foundation modules include `core/assert.h` by path
- Where: `math/perlin_noise.cppm`, `utils/color.cppm`
- Observed: upward layering (U3).
- Inference: removing AGX_ASSERT (C-01) also removes the edge.
- Severity: polish. Confidence: high.

### C-11 Shader convention drift
- Observed: `push`/`pc`/`roundTrip` vs the README's `constants`; test shader lacks scalar layout (M7).
- Severity: polish. Confidence: high.
- Proposal: the README states `push` + `PushConstants`; share the flag set with the test shader.

### C-12 Types in bare `aegis` namespace
- Where: `core/logging.cppm`, `core/profiler.cppm`, `scene/components.cppm`, `scene/component_traits.cppm`
- Observed: types sit in `aegis` while their modules are `aegis.core.*` or `aegis.scene.*`.
- Inference: `aegis::Name` being a scene component is surprising.
- Severity: polish. Confidence: high.
- Proposal: move components to `aegis::scene` and Profiler/Logging to `aegis::core` when each module is ported.

## 3. Target code style (replaces "Two code styles")
1. One module per subsystem `aegis.<name>`, internals as partitions `:name` (file name = partition name), umbrella re-exports public partitions. No dotted submodules.
2. `.cppm` declares; non-trivial bodies live in `.cpp`. Templates and one-line accessors stay inline.
3. Namespaces `aegis::<module>`; nothing in bare `aegis` except the `Engine`/app facade. Test-only helpers go in `aegis::<module>::detail`.
4. Trailing return types everywhere. `[[nodiscard]]` on every function returning `expected`, a handle or an owner; optional on getters.
5. Fallible operations return `std::expected<T, aegis::Error>`. Never throw. Never `assert` something the environment can cause.
6. Invariants use plain `assert`. `AGX_ASSERT*` and `core/assert.h` are deleted.
7. ONE shared `aegis::Error{code, message, where}` in a leaf module with one `makeError(code, fmt, ...)`. Wrapping a lower error appends context. No module-specific Error types.
8. `static create(args) -> expected<T,Error>` with a nested `Desc` when there are 2+ non-obvious params or any optional one; else positional.
9. GPU resources are generational handles resolved via `Device::get`. CPU assets use `shared_ptr`/`unique_ptr`. No raw owning pointers.
10. `Handle` = checked pooled reference; `Descriptor` = bindless slot; `Id` = plain index.
11. Members `m_camelCase`, statics `s_`; plain data structs unprefixed.
12. No singletons or static accessors; dependencies are constructor or `create` parameters (`SceneContext&`).
13. `vk::`/`import vulkan_hpp` only in rhi (plus `ui_pass.cpp`); the native-handle accessor is spelled `native()`.
14. Logging only at the app edge; lower layers return errors. `std::print` is allowed in `aegis-test` and examples.
15. Comments: plain `//`, explain why and contracts; no Doxygen tags; no migration or phase notes.
16. Pure logic is separated from device/GLFW/ImGui and reachable from the umbrella; test-only helpers go in `detail`.
17. Include every std header used (global module fragment); a CI scanner checks it.
18. Shaders: scalar layout, row-major, `struct PushConstants` + `[[vk::push_constant]] ConstantBuffer<PushConstants> push;`. Each C++ mirror has `static_assert(sizeof)` and a comment naming the shader.
19. Tests: `tests/<module>/<unit>.test.cpp`, `TEST_SUITE("<ns>::<unit>")` without `aegis::`, present-tense `TEST_CASE`, `CHECK` over `REQUIRE`.
20. Whole tree clang-formatted and CI-checked; mechanical commits are listed in `.git-blame-ignore-revs`.

Legacy mapping:
- `AGX_ASSERT_X(fallible)` becomes `if (!x) return makeError(...)`.
- `AGX_ASSERT_X(invariant)` becomes `assert(invariant && "msg")`.
- `AGX_UNREACHABLE` becomes `std::unreachable()` plus `assert(false)`.
- `Engine::instance()` and the static accessors become services passed in.
- Dotted submodules become partitions.
- Inline-in-`.cppm` becomes `.cppm` + `.cpp`.
- `ALOG` anywhere becomes logging in the app layer only.
- `new` + raw `ScriptBase*` becomes `unique_ptr` owned by the World.
- `check<T,E>` returning `T{}` becomes `expected` propagated to `main`.
- `///` with `@brief` becomes `//`.
- Bare `aegis::Name` becomes `aegis::scene::Name`.

## 4. Migration order
Principle: don't convert code the app-layer redesign replaces; convert foundations first so the redesign lands in the target style.
0. Docs (AGENTS.md, tests/README, shaders README) and `.git-blame-ignore-revs`. Docs-only, can go straight to main.
1. New shared `aegis.error`; migrate the rhi, renderer, assets and render_graph Error types. Logic change, one PR per module.
2. `math` and `utils`. Leaf modules; only 4 math functions and the profiler's use of utils have callers. Remove the `core/assert.h` include, convert to partitions or delete dead parts. Mechanical rename PR, then a delete-dead-code PR.
3. `core`. Delete `assert.h` once unused, drop `Asset`/`AssetManager` (engine-architecture step 2), rename `m_Layers`, move Profiler/Logging into `aegis::core`. Rename PR (mechanical), then logic.
4. `platform`. `Window::create -> expected`, needed by the app `create()`. Logic.
5. `scene`. Kept by the redesign (World reuses it). Convert asserts, partitions, components to `aegis::scene`, and add Registry tests (parent/child logic is pure and untested). Mechanical PR, logic PR, tests PR.
6. `scripting`. `unique_ptr` ownership, folded into engine-architecture step 3. Logic.
7. `engine`, `scene_defaults`, `scene_description`, `ui`, `editor`. Don't port; rewrite as `aegis.app` in the target style (engine-architecture steps 1-4). Editor goes last because it becomes optional. Logic.
8. `examples/`. Follow the app API; each becomes a `device` CTest. Done together with step 7.
9. Remove `render_graph` -> `scene` (SceneView), engine-architecture step 5, after step 5 here.

Mechanical (own PRs and commits, hashes into `.git-blame-ignore-revs`): dotted-to-partition renames, namespace moves, the `m_Layers` rename, `PushConstants` shader naming, the render_graph `ResourceHandle` rename. Everything else changes behaviour.

## 5. Proposed edits
AGENTS.md
- Replace the "Two code styles" table and paragraph with the section 3 rule list. Keep the legacy mapping table as a transitional note until step 7 is done.
- Fix the shader facts: output is `<rel>.slang.spv` found via `AEGIS_SHADER_DIR`; compiled only when `COMPILE_SHADERS` is ON; `aegis-test` has its own shader/asset paths (M5, M6).
- Reword the `vk::` sentence: "vk:: types appear in rhi's public headers for interop (accessor `native()`); outside rhi only `ui_pass.cpp` imports `vulkan_hpp`."
- Reword the handle rule: Handle = pooled and checked, Descriptor = bindless slot, Id = plain index. Say CPU assets use `shared_ptr`.
- State the layers (math, utils < core, platform < rhi < renderer < assets < render_graph < scene ...) and list U1-U3 as known debt.
- Fix M4: `math` uses partitions.
- Replace "adopt diff-only formatting" with "tree is formatted, CI runs `clang-format --dry-run`". Add a working `tools/format.sh` or fix `git clang-format` first.
- Create `.git-blame-ignore-revs` with the 3 mechanical commits.
- Drop "PascalCase example targets" as a convention; just list the targets.
- Add: release builds are part of verification until `AGX_ASSERT` is gone.
- Add the `[[nodiscard]]` and comment rules.
- Owner decision needed: the "no Co-Authored-By" rule conflicts with harness-injected trailers.

tests/README.md
- Remove all counts and the stale "later render_graph/assets" and "one device test" lines (M8). List `rhi/`, `math/`, `assets/`, `render_graph/` in the layout.
- Scope: say renderer and platform have no tests (M9). List next candidates: scene `Registry`, renderer resize/pacing policy, input state, `Error`.
- `TEST_SUITE` row: "`<ns>::<unit>`, unit is a type or free function, omit `aegis::`". Fix the two outliers.
- Add a "where does the helper live" table: stateful type -> partition; test-only stateless helper -> `detail`; reusable math -> `utility`.
- Note that `aegis-tests-render_graph` links the legacy scene (U2) as a known exception.
- Add a "device tests" section for the planned move of `aegis-test` checks into doctest cases (C-08).
- Replace "Mirror new modules as they land" with a new-module checklist (CMake line, label, umbrella export).
