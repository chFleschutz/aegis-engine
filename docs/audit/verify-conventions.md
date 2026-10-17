# Verification: conventions and consistency

Scope: `docs/audit/findings-c.md` (lens c) and `docs/audit/findings-d.md` (lens d). I checked every cited symbol in code and recounted with Grep over `src/` (excluding `src/aegis/ai/`), `tests/`, `examples/`, `shaders/` and CMake. `findings-b-render.md` is still missing, so render bugs map to the a- and e-lens files.

## 1. Summary counts

| File | Bug / design problem findings | Verified | Unverified | Refuted | Reclassified | Polish (skimmed) |
|---|---|---|---|---|---|---|
| findings-c.md | 5 (C-01, C-02, C-03, C-04, C-08) | 5 | 0 (the C-04 rebuild cost is from notes only) | 0 | 0 | 7 skimmed, none refuted; counts corrected in C-05, C-07 and table rows |
| findings-d.md | 13 (D-01, 02, 03, 05, 06, 07, 08, 17, 22, 23, 24, 27, 29) | 13 (facts hold) | 0 (the D-29 rebuild cost is from notes only) | 0 | 4 (D-02, D-03, D-06, D-17 go to polish) | 1 part refuted (D-25 "Settings to Desc", also cleanup item 7); fact corrections in D-08, D-15, D-21 |

## 2. Findings table

| ID | Short title | Severity (filed → verified) | Verdict | Evidence | Canonical ID |
|---|---|---|---|---|---|
| C-01 | Release builds dereference failed `expected` | bug → bug | VERIFIED | `engine.cppm:222-245`: `AGX_ASSERT_X(context,…); m_context = std::move(*context);`, the same for device, renderer and ibl. `core/assert.h` turns it into `static_cast<void>(0)` under NDEBUG. `check()` returns `T{}` (l.208-214). `ALOG::fatal` does not abort (no abort or exit anywhere in `modules/aegis-log/include`). | B-APP-12 |
| C-02 | Four incompatible Error types | design → design | VERIFIED (one count wrong) | 4 types: `rhi::Error{code, debug location}`, `assets::Error{code, message, optional<rhi::Error>}`, `render_graph::Error{message, optional<rhi>}`, `renderer::Error` enum. There are **5** `makeError` overloads (rhi 1, assets 2, render_graph 2), not "about 9". `check<T,E>` reads `.message`, but today it is only instantiated with assets and render_graph errors, so this is a latent constraint, not a defect. | A-FOUND-01 |
| C-03 | Leaked scripts, no-op `Scene::reset` | bug → bug | VERIFIED | `script_manager.cppm` `addScript`: `T* script = new T(...)`, `std::vector<ScriptBase*>`, and the destructor only calls `end()`. `scene.cppm:74-77` `reset()` is a TODO. `Engine::loadScene` calls it (l.198). The leak itself only happens at teardown. The tangible failure is that a second `loadScene` accumulates systems and scripts (no example does this today), and that destroyed entities' scripts keep running. | B-APP-05 (bug), A-APP-08 (design) |
| C-04 | Header-only legacy modules | design → design | VERIFIED (structure) | 0 `.cpp` files in core, editor, math, scene, scripting, ui and utils; `engine.cppm` has 339 lines. `import aegis.scene;` appears in **18** files, not 19. The rebuild cost (about 180 steps) comes from agent-notes and was not measured. | C-04 (partial overlap with A-APP-12) |
| C-08 | `aegis-test` is an ad hoc main | design → design | VERIFIED | `test/main.cpp` has **1490** lines (not about 1430), a private `aegis::Engine` at l.124, 37 `verify` mentions and 3 ctest entries (`tests/CMakeLists.txt:28-35`). | E-RENDER-13 |
| D-01 | G-buffer declared three times | design → design | VERIFIED | `geometry_pass.cpp:13-20` and `gpu_driven.cpp:236-243` declare the same 5 names plus Depth and CameraData. `lighting_pass.cpp:53-57` references them. The formats come from one place (`GeometryPass::gBufferFormats`); only the names and the begin/viewport code are copied. | A-RENDER-04 |
| D-02 | ImageView descriptor writing duplicated | design → **polish** | VERIFIED; the leak is latent | `device.cpp:80-128`: both `createImageView` bodies are identical, and `replace` (165-190) repeats them. The slot leak is real in logic: if `replace` drops a usage bit, the old slot is never freed, because only `free(ImageViewHandle)` returns slots. It is unreachable today: the only caller, `renderer.cpp:227`, passes the same `desc.usage`. | A-RENDER-11 |
| D-03 | `TextureDescriptor` redefined three times | design → **polish** (standalone) | VERIFIED | `ibl.cpp:26`, `lighting_pass.cpp:22` and `post_effects.cpp:106`; `ibl.cpp` and `lighting_pass.cpp` each also define `descriptorOf`. The shader type is `bindless::TextureDescriptor`, so renaming it `SampledTexture` would diverge from the Slang name. | A-BUILD-01 (the design problem it belongs to) |
| D-05 | aegis-test builds its own engine | design → design | VERIFIED | `main.cpp:146-159`: `Context::create`, `createDevice({})`, `Renderer::create`, the same sequence as `engine.cppm:221-235`. The two `aegis::Engine` classes live in different translation units, so there is no ODR problem. | D-05 (startup duplication); granularity part → E-RENDER-13 |
| D-06 | Normal matrix computed by hand twice | design → **polish** | VERIFIED | `gpu_driven.cpp:52` and `static_mesh_render_system.cpp:56` both use `glm::inverse(glm::mat3{model})`. `math::normalMatrix` (l.171, l.200) has no callers. "Different convention" is about storage only: `normalMatrix` returns (RS)^-T (correct per B-FOUND), and the passes upload its transpose into rows. No wrong output today. | A-FOUND-11 |
| D-07 | Four Error types | design → design | VERIFIED | Same evidence as C-02. **Its proposal contradicts C-02's** (align the shapes, no global module, versus one `aegis.error`). The owner decides under A-FOUND-01. | A-FOUND-01 |
| D-08 | Resource-name literals repeated | design → design | VERIFIED (counts corrected) | There are **49** `add{Texture,Buffer,Reference}("…")` sites, not 47. Depth appears in **4** files (geometry, gpu_driven, post_effects, screen_passes), not 3. CameraData: 4 files and 5 sites. SceneColor: 3 files. Final: 2. | A-RENDER-04 (+ E-RENDER-04) |
| D-17 | AssetManager is dead | design → **polish** (dead code) | VERIFIED | `Engine::assets()` (l.89) has no callers. `AssetManager::get`, `garbageCollect` and `instance()` have no callers. `IsLoadable` has no users. `Asset::path()` is never read anywhere. The real bug in this area, the singleton never being cleared, is B-APP-13. | B-APP-13 (bug); A-CONTENT-05 (core::Asset) |
| D-22 | Four error-handling styles | design → design | VERIFIED | Print and return (as listed): `renderer.cpp:191/223/240/349/359`, `context.cpp:55`, `gpu_driven.cpp:40`, `lighting_pass.cpp:90`. `renderFrame` returns `void`, so submit and present failures are only printed. `std::ignore` also appears at `upload_manager.cpp:297`, `debug.cppm:31` and `test/main.cpp:933` (not listed in D-22). | A-FOUND-01 (+ B-APP-12) |
| D-23 | Two logging styles | design → design | VERIFIED | `ALOG` appears only in `engine.cppm` (6) and `core/logging.cppm` (3). The rhi, renderer and render_graph code has 8 `println` sites. `assert.h` uses `std::cout`. | A-FOUND-03 |
| D-24 | Ownership differs per layer | design → design (duplicate) | VERIFIED | `shared_ptr{new …}` at `texture.cpp:94`, `static_mesh.cpp:105` and `material.cpp:112-113`. `new` with a private constructor is the normal way to reach a private ctor (`make_shared` cannot), so it is not a defect by itself. The script part is C-03. | A-CONTENT-04 (+ B-APP-05) |
| D-27 | createRenderer dereferences a failed expected | bug → bug | VERIFIED | Same as C-01. | B-APP-12 |
| D-29 | render_graph is framework, passes, IBL and UI in one | design → design | VERIFIED (structure) | `render_graph/CMakeLists.txt:3-14` links aegis-scene and EnTT PUBLIC, so `tests/render_graph` (LIBS `aegis-render-graph`) pulls in the scene stack. The rebuild cost is from notes only. | A-BUILD-07 |
| Polish C-05..C-07, C-09..C-12 | | polish | VERIFIED with count fixes (§7) | C-09: the macros at `profiler.cppm:12-13` have zero users. C-10: `perlin_noise.cppm` and `color.cppm` include `core/assert.h`, and both are dead. C-12: bare `aegis` namespace in `logging.cppm:14`, `profiler.cppm:15`, `components.cppm:11` and `component_traits.cppm:7`. | C-05 ← M1; C-07 ← M5/M6/M8; C-09 ← A-APP-13/B-FOUND-14; C-10 ← A-FOUND-02/04; C-11 ← B-BUILD-10/E-BUILD-05; C-12 ← M13 |
| Polish D-04, D-09..D-16, D-18..D-21, D-25, D-26, D-28, D-30..D-32 | | polish | VERIFIED, except the D-25 part (§5) | D-09: `makeTransferWritesVisible` has one caller (`upload_manager.cpp:212`). D-32: 725, 602, 598 and 382 lines confirmed. | see §6 |

## 3. Dead-code check

| Item | Verdict | Evidence (grep) |
|---|---|---|
| `utils::Color` (`utils/color.cppm`) | SAFE-TO-DELETE | Not exported (`namespace aegis::utils` without `export`). No `Color::` or `utils::Color` anywhere. Remove its line in `utils/CMakeLists.txt`. |
| `utils::hashCombine` and module `aegis.utils` (`utils/utils.cppm`) | SAFE-TO-DELETE | Not exported, and no file imports `aegis.utils;`. The only utils importer is `core/profiler.cppm:9-10`, which imports `rolling_average` and `timer`. |
| `utils::file::readBinary`, both overloads (`utils/file.cppm`) | SAFE-TO-DELETE | 0 hits for `readBinary` outside the file. |
| `utils::ScopeTimer` (`timer.cppm:48-62`) | SAFE-TO-DELETE | 0 hits outside the definition. Keep `Timer`, which `Profiler` uses. |
| Fold utils into core, delete the `aegis-utils` target (D-20) | SAFE, with an extra edit | `scripting/CMakeLists.txt:10` also links `aegis-utils` without importing it. D-20 missed this line, and it must go too. |
| `math::normalMatrix`, vec3 and quat overloads | SAFE-TO-DELETE | No `normalMatrix(` calls. The two hits in render_graph are local variable names. |
| `PerlinNoise1D` / `perlin_noise.cppm` | SAFE-TO-DELETE | The namespace is not exported, and there are 0 hits outside math. The edit must also remove `export import :perlin_noise;` (`math.cppm:19`) and the CMake FILES entry. |
| `interpolation.cppm` (`lerp`, `percentage`, `sigmoid01`, `fastSigmoid01`, `tanh01`) | SAFE-TO-DELETE **only together with perlin_noise** | `percentage` and `tanh01` are used at `perlin_noise.cppm:95,97`, with no other users. Remove `export import :interpolation;` (`math.cppm:18`). |
| glm re-export `make_mat4` | SAFE-TO-DELETE | 0 users. |
| glm re-exports `row`, `angleAxis` | NOT-DEAD | `row`: `render_graph/frustum.cppm`. `angleAxis`: eval-scene and simple-scene. |
| `dynamic_movement_controller`, `world_border`, `physics/motion_dynamics` | SAFE-TO-DELETE | They are imported only by each other. The 3 FILES lines are in `scripting/CMakeLists.txt:21,23,24`. |
| `modules/aegis-gltf` | SAFE-TO-DELETE, **but it is a git submodule** | Nothing links `Aegis::GLTF`. Only `modules/CMakeLists.txt:1` adds it, and its `BUILD_TEST` defaults ON. It is listed in `.gitmodules` (`modules/aegis-gltf`, and it has a `.git` gitdir file). The delete PR must remove the `.gitmodules` entry and the gitlink, not only the directory. |
| `core::SHADER_DIR`, `core::INVALID_HANDLE` | SAFE-TO-DELETE | 0 users outside `globals.cppm`. After this, the `BUILD_DIR` compile definition (`core/CMakeLists.txt:26`) has no user either. |
| `core::ENGINE_DIR` | NOT-DEAD | `examples/eval-scene/main.cpp:58,333`. |
| `core::TARGET_FPS` (listed dead in A-APP-13) | NOT-DEAD | It feeds `TARGET_FRAME_TIME`, which `engine.cppm:311` uses. |
| `AssetManager`, `Engine::assets()`, `m_assets` with its 7 `add` lines, `garbageCollect`, `IsLoadable` | SAFE-TO-DELETE | 0 callers. Edits: `engine.cppm:27` (`export import aegis.core.asset_manager`), l.89, l.248-255 and l.330, plus `core/CMakeLists.txt:16`. Nothing gets `core::Asset` only through that re-export. |
| `core::Asset` | NOT-DEAD | It is the base of `Texture`, `MaterialTemplate`, `MaterialInstance` and `StaticMesh`, and `setPath` is called by the loaders. Its `path()` is never read. |
| `Device::Desc` (empty) | SAFE-TO-DELETE (signature change) | Used only as the parameter of `Context::createDevice` (`context.cppm:50`, `context.cpp:104`), called with `{}` at `engine.cppm:225` and `test/main.cpp:154`. Optional: it is a free extension point. |
| `Device::computeQueue()`, `transferQueue()` accessors | SAFE-TO-DELETE | 0 callers. The **queues themselves are NOT-DEAD**: they are created and owned at `device.cpp:357-363,396-397`. Removing them is a behaviour decision (A-RENDER-09). |
| Commented code at `rhi/utility.cppm` and the ALOG block in `assert.h` | SAFE-TO-DELETE | The utility block is lines **57-62**, a `std::variant` `match` example, not "GPU-driven helper debris". |
| `AGX_PROFILE_SCOPE` / `AGX_PROFILE_FUNCTION` (C-09) | SAFE-TO-DELETE | Only the definitions at `profiler.cppm:12-13`. |
| Shaders (D-19 "none dead") | Accepted | Not re-swept. |

## 4. Convention-table fact check (findings-c §1)

| Row | Filed | Recount | Status |
|---|---|---|---|
| 3 Desc | 27 Desc structs | **26** `struct *Desc` (rhi 17, plus assets, platform, renderer and render_graph). The listed non-Desc factories are confirmed (`defaults.cppm:29`, `material.cppm:80`, `static_mesh.cppm:37`, `ibl.cppm:25`, `upload_manager.cppm:46`, swapchain). | Minor correction |
| 4 create/expected | Window asserts | `platform/window.cpp:28` `assert(m_window && …)` in a public constructor | OK |
| 5 Error types | 4 shapes | 4 confirmed; 5 `makeError` overloads | OK (C-02 count fixed) |
| 6 never throw | 0 | 0 `throw` or `catch(` | OK |
| 7 AGX_ASSERT | 45 uses in 9 files | **42 call sites in 9 files** (registry 18, engine 8, asset_manager 4, eval-scene 4, perlin 3, motion_dynamics 2, color 1, scene_panel 1, camera_system 1). 10 of them are in files the cleanup batch deletes, which leaves 32. | Corrected |
| 8 assert() | 63 in 18 files | **62 in 17 files** | Minor |
| 12 vk:: in public API | 14 accessors | **16**: `vk()` x5, `handle()` x6, `operator->` x4 (Device, Queue, CommandPool, Semaphore), plus `Device::device()`. The rule in AGENTS.md is false as written. | Corrected count; claim OK |
| 13 import vulkan_hpp | 30 rhi files + ui_pass | **29** rhi files + `render_graph/passes/ui_pass.cpp`. `render_graph/CMakeLists.txt:13` links vulkan-module PRIVATE for it. | Minor |
| 14 .cppm+.cpp | 0 .cpp in legacy | 0 `.cpp` in core, editor, math, scene, scripting, ui and utils (math counts as legacy here). New stack: rhi 28/17, render_graph 22/15, assets 11/8, platform 3/2, renderer 2/1 (.cppm/.cpp). | OK |
| 15 partitions vs dotted | as filed | Partitions: rhi 27, render_graph 21, assets 9, editor.panels 6, math 3, platform 2, renderer 1. Dotted: core (7), scene (8), scripting (6), utils (4), plus the strays `aegis.physics.motion_dynamics`, `aegis.defaults` and `aegis.scene_description`. | OK |
| 16 umbrella | 23 of 26 | **23 of 27** rhi partitions re-exported; `debug`, `fwd`, `memory` and `vulkan_conversions` are not | Corrected |
| 20 test naming | 2 outliers | **3**: `"aegis::math basis vectors"`, `"aegis::math::inFOV"`, `"rhi::swapchain policy"`. The use of `detail::` is also inconsistent: suites say `rhi::calcMipLevels`, but the function is `rhi::detail::calcMipLevels` (`image.cppm:18-23`), while `assets::detail::scalar` keeps `detail::`. 108 TEST_CASEs confirmed. | Corrected |
| 21 m_ prefix | `m_Layers` x6 | 6, all in `core/layer_stack.cppm`; no other `m_[A-Z]` | OK |
| 22 namespaces | bare `aegis`, glm/entt shims | Confirmed: `math.cppm:22` `export namespace glm`, `scene/entity.cppm:10` `export namespace entt` | OK |
| 29 .git-blame-ignore-revs | missing at root | Missing; only `external/meshoptimizer/.git-blame-ignore-revs` exists | OK |
| 30 push-constant names | push 8, pc 5 | **push 9**, pc 5 (one of them module-scope in `indirect_draw.slang:43`, see B-BUILD-06), roundTrip 1 | Minor |
| 2 [[nodiscard]] | 374 in 61 files | 374 in 61 files | OK |
| 32 println | 41 hits in 8 files | 43 hits in 8 files, but 3 are inside the commented-out block in `rhi/utility.cppm`. That leaves 40 live hits in 7 files, 28 of them in aegis-test. | Minor |

Context the files don't count: there are 25 using-directives (13 `using namespace rhi::utility`, 10 `using namespace aegis`); the `rhi::utility` part is covered by A-FOUND-05.

## 5. Refuted findings

- **D-25 / cleanup item 7, "rename Settings to Desc" (polish, REFUTED in part).** `BloomPass::Settings` (`post_effects.cppm:27`) and the struct at `screen_passes.cppm:41` are runtime tunables, edited through a mutable `settings()` accessor and `drawUI`. They are not creation configuration passed to `create`. Renaming them `Desc` would break the Desc meaning that the target style (C §3 rule 8) relies on. Keep `Settings`. The `m_Layers` rename and the `tranformationMatrix` typo fix stand.
- No bug or design-problem finding was refuted. The partial corrections (C-02's overload count, D-06's "different convention", D-17's severity) are in §7.

## 6. Merged duplicates (canonical ← others)

- **B-APP-12** ← C-01, D-27, the release-UB part of D-22 (also A-FOUND-04 for "AGX_ASSERT used for environment conditions")
- **A-FOUND-01** ← C-02, D-07, D-22, map M2. Conflicting proposals in C-02 and D-07: owner decision.
- **B-APP-05** (bug) / **A-APP-08** (design) ← C-03, the ScriptManager part of D-24
- **A-CONTENT-04** ← D-24
- **C-04** (canonical; partial overlap with A-APP-12) ← D-31
- **E-RENDER-13** ← C-08, the granularity part of D-05, D-30, the main.cpp part of D-32. Also related: A-BUILD-11 ← D-11 (examples CMake), the device-test registration part of C-08.
- **D-05** stays canonical for the duplicated engine startup (no group finding covers it)
- **A-RENDER-04** ← D-01, D-08 (+ E-RENDER-04, E-RENDER-11 for format literals); A-RENDER-12 covers the copied render-system plumbing
- **A-RENDER-11** ← D-02
- **A-BUILD-01** ← D-03 (+ E-BUILD-04, B-BUILD-11)
- **A-FOUND-10** ← D-04
- **A-FOUND-11** ← D-06 (A-FOUND-08 for the dead `normalMatrix`)
- **B-FOUND-07** / E-FOUND-06 ← the clock part of D-10
- **A-FOUND-06** / E-FOUND-03 ← D-12, D-13, D-20, C-10 (A-FOUND-02 for the U3 layering)
- **A-APP-13** ← D-14, D-16, C-09 (+ B-FOUND-14, E-APP-11 for the macro)
- **A-BUILD-09** ← D-15
- **B-APP-13** (bug) / A-CONTENT-05 ← D-17
- **A-RENDER-09** ← the queue part of D-18
- **A-FOUND-03** ← D-23, convention row 32
- **A-BUILD-07** ← D-29
- **B-BUILD-10** / E-BUILD-05 ← C-11 (test shader flags); C-11 stays canonical for push-constant naming
- Map entries: M1 ← C-05; M4 ← row 15; M5/M6/M8 ← C-07; M13 ← C-12, row 21. C-06, C-12, D-09, D-21, D-25, D-26, D-28 have no group finding and stay canonical.

## 7. Corrections to facts

1. C-02 / C §3: there are 5 `makeError` overloads, not "about 9".
2. C-04: 18 files `import aegis.scene;`, not 19.
3. C-08: `test/main.cpp` has 1490 lines (D-05 has this right).
4. C-05 / row 12: 16 vk-returning accessors, not 14. 29 rhi files import `vulkan_hpp`, not 30.
5. C-07: tests/README is only partly stale. Line 26 already lists all four test executables and "three device-labelled runs". Still stale: "104" (actual 108), line 3 "later render_graph / assets", line 6 "the one device-labelled test", and the layout tree that shows only `rhi/` and `math/`.
6. Row 7: 42 AGX_ASSERT call sites (32 after the cleanup batch). Row 8: 62 in 17 files. Row 3: 26 Desc structs. Row 16: 23 of 27. Row 20: 3 outliers.
7. D-08: 49 literal sites, not 47. "Depth" is in 4 files.
8. D-15: `modules/aegis-gltf` is a registered git submodule (`.gitmodules`), not "1 tracked file plus untracked output". Removing it is a submodule removal.
9. D-20: `scripting/CMakeLists.txt:10` also links `aegis-utils` (unused), so dropping the target needs that edit too.
10. D-21: the commented block is at `rhi/utility.cppm:57-62`, and it is a `std::variant` `match` example, not GPU-driven debris.
11. D-13: removing interpolation and perlin_noise also requires deleting `export import :interpolation;` and `export import :perlin_noise;` at `math.cppm:18-19`.
12. D-02: the slot leak in `Device::replace` cannot be reached today, because the only caller (`renderer.cpp:227`) keeps `usage`.
13. D-06: `math::normalMatrix` is mathematically correct (B-FOUND). The difference is only row versus column packing.
14. D-03 / cleanup item 9: the Slang type is `bindless::TextureDescriptor`. Pick one name with A-BUILD-01 (`rhi::TextureDescriptor`) rather than D-03's `SampledTexture`. The change is not purely mechanical, because it adds public API.
15. A-APP-13 (outside this group): `core::TARGET_FPS` is used through `TARGET_FRAME_TIME` (`engine.cppm:311`), so it is not dead.
16. Row 5 / C-02: "`check` reads `.message`, which `rhi::Error` lacks" is a latent constraint, not a defect. No current call passes `rhi::Error` or `renderer::Error`.
