# Foundation backlog

The work from [report.md](report.md) §7, as a checklist of PRs. Each entry is one PR (or one numbered
step of a larger proposal) and has a GitHub issue that tracks its status; the boxes here are ticked
in batches on `main`, not in feature PRs. Issues: #34–#90 (labels `M1`–`M8`).

- **Closes** lists the canonical finding IDs, with merged duplicates in parentheses. The evidence
  (file:line, failure scenario, fix sketch) is in `findings-*.md`; verdicts and corrections are in
  `verify-*.md`, which win where the two disagree.
- **Done when** is the acceptance check. "Test n" refers to the first test batch in report §4.3.
  A test marked *fails today* is written first, as the bug report.
- **Size:** S = under 5 files or about a day, M = one module or a few days, L = crosses modules.
- Baseline: commit `5c4b20d9`. Line numbers in the findings drift as PRs land; re-read the cited
  code at the start of each PR.

## How to work an item

The full workflow is in AGENTS.md "Commits and pull requests". In short:

1. `tools/new-worktree.sh <branch> [base]` with the branch name given; for an item whose
   dependency is still unmerged, pass that branch as `base` (stacked PR).
2. Read only the IDs listed under **Closes**, then re-check them against the current code.
3. Write the *fails today* test first, where there is one.
4. Keep mechanical commits (renames, deletions, signature churn) separate from logic commits.
5. Each commit: builds clean, `ctest -LE device` passes. Before pushing: the full `ctest` including
   the `device` tests; for render changes, compare `aegis-test --headless --scene --screenshot` and
   `--gpu-driven --screenshot`.
6. `tools/format.sh`, then `gh pr create` with `Closes #<issue>` in the body; check CI; leave the
   merge to the owner.
7. After merge: `tools/remove-worktree.sh <branch> --delete-branch`; add mechanical commit hashes
   to `.git-blame-ignore-revs`.

Up to about three items can run in parallel, one worktree each. Don't run two items that edit the
same central file at once (noted under **Depends** or in the milestone intro).

## Decisions

Taken 2026-10-17 (report §8 lists the original questions).

- [x] **D1. Error types: one type per module, same shape.** Each module's `Error` is
  `{ ErrorCode code; std::string message; std::optional<rhi::Error> rhi; }` (rhi keeps
  `{code, debug location}`), built with `makeError(code, msg)` / `makeError(lowerError, msg)` and
  printed only through the module's `describe(const Error&)`. Wrapping adds context and copies the
  rhi cause; it never re-stringifies. Why: callers branch on module codes (`NotReady`, `OutOfDate`,
  `FormatNotSupported`); the problem is lost causes, not the number of types. → 3.2.
- [x] **D2. Static/dynamic: hybrid.** Static entities stay frozen after `begin`. Structural changes
  (entity create/destroy, `Mesh`/`Material` add/remove) bump a static-set version automatically
  through the existing entt signals, and the static set is rebuilt when it changes. Moving a static
  entity is explicit: the editor calls `markStaticDirty(entity)` after gizmo/inspector edits;
  scripts that move their entity use `DynamicTag`. The rule is documented at `DynamicTag` and in
  AGENTS.md. Why: all writes go through component references, which entt can't observe; automatic
  tracking would need a setter API everywhere. Revisit with `InstanceStore` (7.1). → 1.7, 1.9.
- [x] **D3. Logging: drop the `aegis-log` dependency.** Replace it with the simple `aegis::log`
  in the foundation module (stderr with a severity tag, no setup, one `setSink` for tests). It can
  come back later as an app-installed sink if file or async output is wanted. → 3.1.
- [x] **D4. Foundation module: `aegis.base` now, renamed `aegis.core` once the legacy core is
  gone** (core is the better name). The profiler lives in it (public ctor, `steady_clock`, one
  documented default instance). → 3.1, 6.7.
- [x] **D5. Sponza alpha-mask/double-sided materials: a feature of the material system,** not
  foundation work. 5.2 carries the alpha and sidedness data so the feature only adds the pipeline
  key and the masked variant. → moved to "After the foundation".
- [x] **D6. Preserve mirrored transforms.** `decomposeTRS` keeps negative scale; check winding of
  mirrored instances in the same PR. → 1.8c.

Smaller decisions:
- [x] **Subproject use:** make paths subproject-safe now (3.5). How a game consumes the engine
  (subproject, package, install rules) is a future feature and decision (F1 below).
- [x] **Keep the CPU geometry path** as the fallback for devices without mesh shaders (and the
  visual reference for the GPU-driven path). 7.1 makes both draw from the same data.
- [x] **Formatting: the whole tree is formatted;** drop the diff-only rule from AGENTS.md (0.1).
  A CI check is optional (8.3).
- [x] **Reference images: local comparison** against images from the dev GPU (8.1). lavapipe CI
  is not a priority (moved to "After the foundation").
- [x] **Commit trailers:** keep the AGENTS.md rule, no `Co-Authored-By` on any commit.

Future decisions (not blocking):
- [ ] **F1. How a game uses the engine:** subproject, installed package, or engine-as-host. Decides
  install rules, asset/shader location at runtime, and whether `aegis.app` owns `main`.

---

## M0. Docs (direct to `main`)

- [ ] **0.1 Fix stale docs** · S
  - Closes: map M1–M11, M13–M15; report §5.2–5.3 (AGENTS.md, tests/README.md), incl. slangc
    `-depfile` support and the "no `;` in a TEST_CASE name" rule.
  - Also records the decisions: replace "adopt diff-only formatting" with "the whole tree is
    clang-formatted; format whole files"; the D2 static/dynamic rule; the D1 error shape and the
    D3 log policy as the target style (report §2.3).
  - Done when: every row of map.md "Where code and docs disagree" is fixed or marked as a goal.
- [ ] **0.2 `.git-blame-ignore-revs`** · S
  - Content: b5877948, 561096dc, 5c4b20d9, after `style/legacy-conversion` merges.

## M1. Independent bug fixes

Parallel except where **Depends** says otherwise. 1.9, 1.10 and 1.11 all touch `engine.cppm`:
merge in that order or rebase.

- [ ] **1.1** `fix/test-case-semicolon` · S · #34
  - Closes: E-BUILD-01.
  - Done when: ctest lists "reads in the same layout need no barrier…" as one or two real cases,
    each running its assertions (today both halves run 0 cases and pass).
- [ ] **1.2** `fix/gpu-driven-shaders` · S · #35
  - Closes: B-BUILD-01, B-BUILD-02 (A-BUILD-03), B-BUILD-03, B-BUILD-04, culling bound check
    (B-RENDER-04 part a).
  - Done when: `spirv-dis` of `task.slang.spv` shows `OpEmitMeshTasksEXT` outside any branch and
    the compaction uses a groupshared counter, not wave ops; culling takes scale from columns;
    culling skips `drawID >= batch.count`; the AO factor is applied or removed from both sides;
    CPU and GPU-driven screenshots still match.
- [ ] **1.3** `fix/depth-write-access` · S · #36
  - Closes: B-RENDER-09.
  - Done when: `DepthWrite` includes depth-attachment read access; device tests pass.
- [ ] **1.4** `fix/resource-pool-stable-storage` · S · #37
  - Closes: A-RENDER-02.
  - Done when: test 13 passes ("a returned reference keeps its address while the pool grows").
- [ ] **1.5** `fix/renderer-error-paths` · S–M · #38
  - Closes: B-RENDER-01, B-RENDER-03 (a, b; ImGui `ImageCount` residue), B-RENDER-02 (E-RENDER-12).
  - Done when: the new swapchain is created before old views are freed and a failed recreate is
    fatal; `renderFrame` and `resize` return `expected` and callers stop on error; a failed submit
    recreates `imageAvailable`; ImGui gets `maxFramesInFlight`; mip counts are clamped to the
    extent (pure `detail` helper with a test); a zero extent skips the frame. Headless and windowed
    `--frames 600` pass.
- [ ] **1.6** `fix/draw-batch-registry` · S · #39
  - Closes: E-RENDER-06, B-RENDER-05.
  - Done when: tests 11 (*fails today*) and 12 pass; the 257th template returns an error instead
    of asserting.
- [ ] **1.7** `fix/gpu-driven-static-set` · M · #40
  - Closes: B-RENDER-04 (A-RENDER-01, B-APP-06, A-APP-01 parts 3–4).
  - Depends: 1.2, 1.6.
  - Design (D2): a static-set version, bumped by the entt construct/destroy signals for entities,
    `Mesh` and `Material`, and by `markStaticDirty(entity)`. `InstanceUpdatePass` rebuilds the
    static instances into a new buffer when the version changes and frees the old one through the
    deletion queue; the transform system recomputes marked static entities.
  - Done when: a new `aegis-test --scene --gpu-driven` check that destroys a static mesh entity
    at frame 60 and loads a model after scene load runs clean under validation and renders both
    changes; static material edits reach the GPU.
- [ ] **1.8a** `test/scene-target` · S · #41
  - Closes: B-APP-03 (A-APP-03, E-APP-02, E-APP-03), E-APP-09, A-APP-10 (E-APP-01), link
    visibility in `scene/CMakeLists.txt` (A-BUILD-08 polish).
  - Done when: `aegis-tests-scene` exists; tests 1–3 pass (1 and 3 *fail today*); cycles are rejected.
- [ ] **1.8b** `fix/transform-composition` · S · #42
  - Closes: B-APP-02 (A-APP-02, E-APP-04), B-APP-04 (A-APP-05, E-APP-12).
  - Depends: 1.8a.
  - Done when: tests 4 (*fails today*) and 5 pass, plus a `toLocal` inverse test; the gizmo
    manipulates the world matrix; bundled scene screenshots unchanged.
- [ ] **1.8c** `fix/camera-and-decompose-math` · S · #43
  - Closes: A-FOUND-08, B-FOUND-01 (E-FOUND-02).
  - Depends: 1.8a.
  - Design (D6): preserve mirrored transforms. On a negative determinant negate one scale axis
    and its column before `quat_cast`, normalize the quaternion, keep the previous rotation when a
    scale is zero.
  - Done when: tests 6 and 7 pass; camera math lives in `aegis.math` and `CameraSystem` calls it;
    a mirrored glTF node (or inspector scale x = -1) renders with correct winding on both paths
    (if back faces get culled, add the front-face flip for mirrored instances in this PR).
- [ ] **1.9** `fix/editor-safety` · S · #44
  - Closes: B-APP-07, B-APP-05 (A-APP-04) guards, B-APP-01 (A-APP-01 parts 1–2, E-APP-05) stopgap.
  - Depends: 1.7 for the static-version part.
  - Done when: `Entity::operator bool` is `explicit` and tree nodes use the entity id; destroying
    Main Camera or the teapot in Simple-Scene neither asserts nor leaves scripts running; the
    teapot has `DynamicTag` and rotates on screen; gizmo and inspector edits on static entities
    call `markStaticDirty` and are visible; the D2 rule is documented at `DynamicTag`.
- [ ] **1.10** `fix/camera-input` · S · #45
  - Closes: B-APP-08 (A-APP-06, B-CONTENT-07), B-APP-09 (E-APP-07, B-CONTENT-08), B-APP-10
    (E-APP-10 b), B-APP-11 (E-APP-10 a).
  - Done when: game input is gated on ImGui `WantCapture*`; mouse look is not scaled by dt;
    Transform runs before Camera and the aspect is set before the scene update; dt is reset after
    a minimize and clamped. Typing in the inspector no longer moves the camera.
- [ ] **1.11** `fix/release-setup` · S · #46
  - Closes: B-APP-12 (C-01, D-27) stopgap, B-FOUND-02.
  - Done when: a failed context/device/renderer/IBL creation logs its cause and exits instead of
    dereferencing; `assertFailed` writes to stderr and traps at the call site (check with stdout
    redirected to a file).
- [ ] **1.12** `fix/gltf-import-bugs` · S · #47
  - Closes: B-CONTENT-02, B-CONTENT-03, B-CONTENT-05, B-CONTENT-06 (E-CONTENT-06); check
    E-CONTENT-05 (unverified).
  - Done when: VEC4 `COLOR_0` loads in debug; data-URI images load; non-triangle topology is
    rejected with a clear error (or converted); only the default scene's nodes are instantiated.
    Unit tests follow in 5.2 (test 18). For E-CONTENT-05, take a Simple-Scene screenshot and flip
    OBJ v only if the painting is mirrored.
- [ ] **1.13** `fix/material-and-texture-validation` · S · #48
  - Closes: B-CONTENT-01 (E-CONTENT-03, A-CONTENT-09 half), B-CONTENT-12 (E-RENDER-07, E-CONTENT-09).
  - Done when: tests 16 and 17 pass; `setParameter`/`addParameter` return `expected`;
    `detail::planImageUpload` exists and `Texture::create(desc, mip0)` rejects a wrong size with
    `ErrorCode::InvalidArgument`.
- [ ] **1.14** `fix/device-ranking` · S · #49
  - Closes: E-RENDER-09 (unverified).
  - Done when: `detail::rankPhysicalDevice` and `detail::pickQueueFamilies` take plain values and
    have tests (integrated beats CPU at equal limits; compute/transfer fall back to graphics,
    present never does); `Device::Desc` gains an optional device override.

## M2. Mechanical cleanup

Own PRs, blame-ignored. Follow the dead-code verdicts in `verify-conventions.md` §3. Parallel with
M1, except 2.4 after 1.11 and 2.7 after M1.

- [ ] **2.1** `cleanup/dead-utils` · S · #50
  - Closes: A-FOUND-06 (E-FOUND-03, D-12, D-20), B-FOUND-12, B-FOUND-13.
  - Content: delete `utils::Color`, `hashCombine`, `file::readBinary`, `ScopeTimer`, the
    `aegis.utils` primary; fold `Timer`/`RollingAverage` into `core/profiler`; delete the
    `aegis-utils` target and the unused link at `scripting/CMakeLists.txt:10`.
- [ ] **2.2** `cleanup/dead-math` · S · #51
  - Closes: A-FOUND-07 (B-FOUND-03, E-FOUND-01), A-FOUND-09 (E-FOUND-04), D-13, B-FOUND-08..11.
  - Content: delete `perlin_noise` and `interpolation` together (incl. `math.cppm:18-19`), both
    `normalMatrix` overloads, the vec3 Euler overloads, `make_mat4`, `inFOV` and its tests,
    `math::Random` (examples get a local `std::mt19937`). Keep `row`, `angleAxis`.
- [ ] **2.3** `cleanup/dead-scripting-and-ai` · S · #52
  - Closes: A-APP-13 (D-14), the `ai/` verdict.
  - Content: delete `dynamic_movement_controller`, `world_border`, `physics/motion_dynamics`
    (3 CMake lines) and `src/aegis/ai/`; remove the `ai/` exception from AGENTS.md.
- [ ] **2.4** `cleanup/dead-core` · S · #53
  - Closes: D-16, D-17, B-APP-13, B-FOUND-14 (C-09), D-21.
  - Depends: 1.11.
  - Content: delete `core::SHADER_DIR`, `INVALID_HANDLE` (then the `BUILD_DIR` definition),
    `AssetManager`, `Engine::assets()`, `m_assets`, `IsLoadable`, the `AGX_PROFILE_*` macros,
    the commented blocks. **Keep** `TARGET_FPS`, `ENGINE_DIR`, `core::Asset`.
- [ ] **2.5** `cleanup/remove-aegis-gltf` · S · #54
  - Closes: A-BUILD-09 (D-15).
  - Content: remove the **git submodule** `modules/aegis-gltf`: `.gitmodules` entry, gitlink,
    `add_subdirectory`.
- [ ] **2.6** `cleanup/unused-queue-accessors` · S · #55
  - Closes: A-RENDER-09 NOW part (D-18 part).
  - Content: delete `Device::computeQueue()`/`transferQueue()`; keep the queues.
- [ ] **2.7** `style/renames` · S · #56
  - Closes: D-25 (rename part only; keep `Settings`), C row 20, C-11 naming, M10.
  - Content: `tranformationMatrix` → `transformMatrix`, `m_Layers` → `m_layers`,
    `render_graph::ResourceHandle` → `ResourceId`, the 3 test-suite outliers, shader `pc` → `push`.
- [ ] **2.8** `style/format-tree` · S · #57
  - Decision: whole-tree formatting.
  - Content: `tools/format.sh --all` over the 14 files the diff-only rule never touched (about 94
    lines, mostly rhi; check whether the aligned `#define`s in `rhi/vma_impl.cpp` deserve a
    `// clang-format off` guard instead).
  - Done when: `tools/format.sh --all --check` passes; the commit is in `.git-blame-ignore-revs`.
    Land it early: until then, touching one of those files reformats unrelated lines.

## M3. Foundation policy

- [ ] **3.1** `feat/aegis-base` · M · #58
  - Closes: A-FOUND-02, A-FOUND-03 (B-FOUND-04, D-23), A-FOUND-05, B-FOUND-05 (E-FOUND-05),
    B-FOUND-07 (D-10), E-FOUND-06 (E-APP-11).
  - Depends: 2.1, 2.4.
  - Done when: the `aegis-log` dependency is gone (D3): the `modules/aegis-log` **git submodule**
    removed (`.gitmodules` entry, gitlink, `add_subdirectory` in `modules/CMakeLists.txt`; the
    library itself lives on in its own repository), plus `core/logging.cppm`, the `ALOG` calls in
    `engine.cppm` and the `Aegis::Log` links; `aegis.base` (named per D4)
    holds `:log` (stderr, sink, mutex), `:flags` (opt-in, ADL-visible;
    `Format | Format` no longer compiles; the 13 using-directives and `Modifiers` operators are
    gone), `:align`, `:match`, the profiler (public ctor, `steady_clock`); `tests/base` covers
    flags, the log sink and `RollingAverage` before the window fills.
- [ ] **3.2a–d** `refactor/errors-{rhi,renderer,render-graph,assets}` · M (four PRs) · #59, #60, #61, #62
  - Closes: A-FOUND-01 (C-02, D-07, D-22), A-FOUND-04 (C-10), B-RENDER-06 (lost batch error).
  - Depends: 3.1.
  - Done when: each module's `Error` has the D1 shape and a tested `describe()`; the
    `println` sites become returns or `log::warn`; `flushUploads` propagates; validation messages
    go through `log::error`.
- [ ] **3.3** `build/shader-helper-spirv-val` · S · #63
  - Closes: A-BUILD-06 (E-BUILD-05, B-BUILD-10, B-BUILD-07), A-BUILD-10 NOW part, B-BUILD-11
    (spirv-val part), A-BUILD-11.
  - Done when: `aegis_add_shaders` compiles both `shaders/` and the test shader, runs `spirv-val`,
    tries `DEPFILE`; device tests register only with shaders; the GPU-driven test has a
    `mesh-shaders` label and skip code; orphaned `.spv` are cleaned at configure; CI fails on
    invalid SPIR-V.
- [ ] **3.4** `refactor/gpu-contract` · M · #64
  - Closes: A-BUILD-01 (E-BUILD-02, D-03), A-BUILD-02 (B-BUILD-08, B-CONTENT-13, E-BUILD-04),
    B-BUILD-06, E-BUILD-03, E-CONTENT-02, A-FOUND-11 (D-06).
  - Depends: 3.3.
  - Done when: `rhi::TextureDescriptor` and typed descriptors replace the local copies; mirrors
    live in `:gpu_types` with `sizeof`/`offsetof` tests; `addPbrParameters` and PBR name constants
    are tested against `PbrMaterial`; meshlet limits are one constant checked by
    `MeshPreprocessor`; push constants are declared in entry-point files only.
- [ ] **3.5** `build/paths` · S · #65
  - Closes: A-BUILD-05 (D-16 rest), B-BUILD-05, B-BUILD-09.
  - Depends: 2.4.
  - Done when: one `AEGIS_`-prefixed shader/asset path value, PRIVATE, read in one TU, with an
    environment override and `u8` paths; all paths derive from `PROJECT_SOURCE_DIR` /
    `PROJECT_BINARY_DIR`, so reader and writer agree when built as a subproject (check with a
    two-line wrapper project). Install rules wait for F1.
- [ ] **3.6** `build/link-visibility` · S · #66
  - Closes: A-BUILD-08 rest (E-APP-13).
  - Done when: scripting and assets link PUBLIC what their `.cppm` import; EnTT dropped from
    assets, imguizmo from core; the rule is in AGENTS.md.

## M4. Render-graph authoring

- [ ] **4.1** `fix/schedule-order` · S · #67
  - Closes: E-RENDER-05, A-RENDER-05 b (B-RENDER-08), E-RENDER-03.
  - Done when: tests 8 and 9 (*fail today*) and 10 pass; no `default:` in the four `Usage` tables.
- [ ] **4.2** `refactor/graph-declarations` · M · #68
  - Closes: A-RENDER-03 (E-RENDER-01), A-RENDER-04 (E-RENDER-04, E-RENDER-11, D-01, D-08),
    E-RENDER-02.
  - Depends: 4.1, 3.2c.
  - Done when: passes declare each access once (mechanical signature commit, then logic);
    `ResourceDeclarations` + `detail::compilePlan` exist; tests 14 and 15 pass; a checked-in plan
    snapshot of both default graphs; `ResourcePool::format(handle)` replaces format literals;
    shared names are constants.
- [ ] **4.3** `feat/graph-owned-textures` · M · #69
  - Closes: A-RENDER-06 (B-RENDER-07).
  - Depends: 4.2.
  - Done when: the graph owns and frees all its textures; `FrameGraph::resized` recreates them;
    `createResolutionDependentTexture` and `scaleFactor` are gone; rebuilding a graph leaks nothing.
- [ ] **4.4** `refactor/frame-data-extraction` · S · #70
  - Closes: E-RENDER-08, A-RENDER-07 (write-back part).
  - Depends: 4.1 (parallel with 4.2).
  - Done when: `detail::gatherLighting`, `makeCameraData`, `instanceTransform` exist with tests;
    the `camera.aspect` write-back in `SceneUpdatePass` is gone.
- [ ] **4.5** `refactor/descriptor-writes` · S · #71
  - Closes: A-RENDER-11 (B-RENDER-10, D-02).
  - Done when: one private descriptor-write helper; partial failure frees earlier slots;
    `ErrorCode::OutOfDescriptors`.

## M5. Content and platform

Parallel with M4, after 3.2d.

- [ ] **5.1** `feat/image-data-decode` · S · #72
  - Closes: A-CONTENT-08 (E-CONTENT-08), B-CONTENT-15 (E-CONTENT-12).
  - Done when: `ImageData` + `decodeImage(span)` in a CPU-only partition with tests; `Texture`
    has one create-from-`ImageData` path that validates size; extension checks are
    case-insensitive.
- [ ] **5.2** `feat/model-import` · M · #73
  - Closes: A-CONTENT-02 (E-CONTENT-01), E-CONTENT-14, E-CONTENT-07, B-CONTENT-09.
  - Depends: 5.1.
  - Done when: `:import` (no rhi, no scene) produces `ModelData` from glTF bytes and OBJ streams;
    test 18 and its siblings pass on in-source fixtures; missing normals are generated; a missing
    texture is a warning; the asset key `{relative path, subIndex}` is defined.
- [ ] **5.3** `refactor/instantiate-in-app` · M · #74
  - Closes: A-CONTENT-01, A-CONTENT-03 (E-CONTENT-04).
  - Depends: 5.2.
  - Done when: `instantiate(Registry&, const Model&)` lives in the app layer with `Mesh`/`Material`;
    `aegis-assets` no longer links `aegis-scene` or EnTT (upward edge U1 gone); each load gets its
    own default material instance.
- [ ] **5.4** `refactor/remove-core-asset` · S · #75
  - Closes: A-CONTENT-05.
  - Depends: 5.3.
- [ ] **5.5** `feat/key-bindings` · S · #76
  - Closes: B-CONTENT-11 (E-CONTENT-11), A-CONTENT-13 binding part (E-CONTENT-10).
  - Done when: `platform::KeyBindings` with `bind -> id`, `unbind`, snapshot dispatch; test 19 and
    the other binding tests in `tests/platform` pass on CI.
- [ ] **5.6** `feat/input-state` · S–M · #77
  - Closes: A-CONTENT-13 rest (E-APP-06), `Key::Unknown` GLFW errors.
  - Depends: 5.5.
  - Done when: scripts read `const InputState&`; test 20 passes; no GLFW errors per frame.
- [ ] **5.7** `fix/window-create` · S · #78
  - Closes: A-CONTENT-11 (B-CONTENT-10, E-CONTENT-13), A-CONTENT-12 part.
  - Done when: `Window::create -> expected`; GLFW initialized once with an error callback; move
    assignment deleted; "no display" reports GLFW's reason.
## M6. `aegis.app` (P7)

Depends on M3 and 5.6/5.7. One PR per step; examples port one per commit.

- [ ] **6.1** `feat/app-create-headless` · M · #79
  - AppDesc, CLI (reuse `aegis-test` options), `Engine::create -> expected`, headless mode, exit
    codes, `aegis_add_example`; every example runs as a `device` CTest.
- [ ] **6.2** `refactor/app-services` · M · #80
  - Remove the singleton and static accessors; `engine.cpp`; plain service structs; narrow
    re-export (A-APP-12).
- [ ] **6.3** `refactor/world-lifecycle` · M · #81
  - Closes: A-APP-08, B-APP-05 proper, C-03, A-APP-04 slots.
  - Scripts component + `ScriptSystem`, real `reset`, slots become components, opt-in default
    helpers, named scenes (`--scene`).
- [ ] **6.4** `feat/frame-phases` · M · #82
  - Closes: A-APP-07, B-APP-10/11 proper.
  - Phases, `FrameClock` (clamp or fixed step), `InputState` with capture, editor safe point.
- [ ] **6.5** `refactor/presentation-and-editor` · M · #83
  - Closes: A-APP-11, A-APP-14, B-APP-14.
  - ImGui owned by Presentation; optional editor with `Panel`/`EditorContext`.
- [ ] **6.6** `refactor/aegis-test-on-app` · M · #84
  - Closes: D-05, C-08, E-RENDER-13 granularity part.
  - `aegis-test --scene` through `app::Engine::create`; `test/main.cpp` split into `checks/*.cpp`.
- [ ] **6.7** `style/rename-base-to-core` · S (mechanical) · #85
  - Decision D4.
  - Depends: the legacy core is gone (2.4, 3.1, 5.4, 6.5 and the last `AGX_ASSERT`/`globals`
    users rewritten).
  - Done when: `aegis.base` is renamed `aegis.core` (module, namespace, target, `tests/base`), and
    no legacy `core` remains.

## M7. Minimal SceneView, then split (P8)

Before the first terrain, vegetation or shadow pass.

- [ ] **7.1** `feat/frame-scene` · M · #86
  - Closes: A-RENDER-07, B-RENDER-04 structural fix, A-RENDER-10 (path divergence).
  - Depends: 4.2, 6.3.
  - Done when: passes read a `FrameScene`; `InstanceStore` (pure, tested) replaces the registry
    counting and instance collection; `FrameInfo` no longer names `Scene`.
- [ ] **7.2** `refactor/split-render-graph` · S (mechanical) · #87
  - Closes: A-BUILD-07 (E-BUILD-07, D-29).
  - Depends: 7.1.
  - Done when: `aegis-render-graph` (framework) and `aegis-render-passes` are separate targets;
    render_graph tests link no scene, ImGui or shaders.

## M8. Verification tiers (P9)

- [ ] **8.1** `feat/reference-images` · S–M · #88
  - Closes: E-RENDER-13 (A-BUILD-10) image part.
  - Depends: 1.2.
  - Done when: `aegis-test --compare <png> --tolerance` (diff image on failure); references for
    the triangle, `--scene` and `--gpu-driven` frames are generated on the dev GPU and stored in
    `tests/golden/`; they run locally under a `golden` label that CI excludes (no GPU there).
    Regenerate the references deliberately when a render change is intended.
- [ ] **8.2** `ci/missing-include-scanner` · S · #89
  - Done when: CI runs the std-header scanner (agent-notes idea 4).
- [ ] **8.3** `ci/clang-format-check` · S (optional) · #90
  - Depends: 2.8.
  - Done when: CI runs `tools/format.sh --all --check` (clang-format 22.x).

---

## Polish: fix when touching the file

No PR of their own. Pick them up when a PR already edits the file.

- [ ] B-RENDER-11: skipped lighting leaves SceneColor undefined; process-wide `static reported`.
- [ ] B-BUILD-12: IBL bakes sample texel corners, not centers (all four IBL shaders).
- [ ] B-CONTENT-14: RGBA32F textures linear-filtered without a format-feature check.
- [ ] B-APP-15 (E-APP-08, B-FOUND-06): camera inspector accepts degenerate FOV/near/far; vacuous
  aspect assert.
- [ ] A-APP-09 (dead "Name"/"Transform" Add-menu entries); component table itself waits for editor
  growth.
- [ ] E-RENDER-10: `static_assert` invariants in `:vulkan_conversions`.
- [ ] A-CONTENT-04 dedupe of images shared by several glTF textures (with the asset cache).
- [ ] C-05: one native-handle accessor spelling (needs the formatting/naming decision).

## After the foundation

Not part of the foundation work.

- [ ] **Material system, first feature:** alpha-mask and double-sided materials (D5).
  - Closes: B-CONTENT-04 (A-CONTENT-07 items 3–4), A-CONTENT-06 glTF-sampler part, A-RENDER-10.
  - Depends: 5.2 (alpha/sidedness in the import data), 4.2.
  - Done when: a per-template pipeline key on both paths, a masked shader variant, per-material
    cull mode; Sponza's MASK/doubleSided materials render correctly (reference image).
- [ ] **lavapipe device tests in CI** (not a priority): a Linux debug job running
  `ctest -L device -LE mesh-shaders` on lavapipe. Depends on 1.2, 1.14, 3.3.
- [ ] **F1 follow-up:** install rules and runtime asset/shader lookup once the game-usage model is
  decided.

Deferred until the feature (report §3): persistent and imported graph images, pooled pipelines and
`ShaderLibrary`, multi-queue submit, asset cache and streaming, component table with serialization
and undo, job system, transient aliasing.
