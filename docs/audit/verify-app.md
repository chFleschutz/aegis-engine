# Verification: app layer

Verifier, phase 3. Every cited symbol was re-read in the code. The Registry list logic, `entityCount`, the `Entity` → `uint32_t` conversion, the transform composition and the camera inverse view were reproduced in standalone clang++ programs against `external/entt` and `external/glm` (`build/audit/verify/app/repro.cpp`, `invview.cpp`). There were no GPU runs: no app-layer claim needed one, and the Simple-Scene teapot can't be shown headless without changing source. That claim was traced end to end instead.

## 1. Summary counts

| File | Bug / design findings checked | Verified | Unverified | Refuted (whole) | Partly refuted / fact corrected | Reclassified | Polish kept |
|---|---|---|---|---|---|---|---|
| findings-a-app.md | 12 (01–12) | 12 | 0 | 0 | 2 (A-APP-01 inference 3; A-APP-09 Add-menu fact) | 0 | 2 (13, 14) |
| findings-b-app.md | 13 (01–10, 12–14) | 13 | 0 | 0 | 3 (B-APP-03 (c); B-APP-04 "jump"; B-APP-13 release half) | 0 | 2 (11, 15) |
| findings-e-app.md | 11 (01–10, 12) | 11 | 0 | 0 | 1 (E-APP-03 sibling detail) | 2 (E-APP-08 bug→polish; E-APP-10(a) bug→polish) | 2 (11, 13) |

No whole finding is refuted. Four inferences are refuted in part (section 3).

## 2. Table

| ID | Short title | Severity (filed → verified) | Verdict | Evidence | Canonical |
|---|---|---|---|---|---|
| A-APP-01 | Static/dynamic rule not enforced; edits don't reach renderer | bug → bug | VERIFIED; inference 3 REFUTED as worded | transform_system.cppm:49 view needs `DynamicTag`; gpu_driven.cpp `sceneInitialized` writes static set once. Inference 3: editor "Default Material" adds `Material{}` with null `instance` (scene_panel.cppm:309, assets/components.cppm:18), and draw_batch_registry.cpp:111 skips it, so no count changes. | B-APP-01 (parts 1,2); B-RENDER-04 (parts 3,4) |
| A-APP-02 | Hierarchy ignores parent rotation/scale | bug → bug | VERIFIED (reproduced) | transform_system.cppm:39-41, 61-63. Repro: parent rot 90° Z + scale 2, child (1,0,0) gives (1,0,0); correct is (0,2,0). | B-APP-02 |
| A-APP-03 | Re-parenting corrupts sibling lists | bug (latent) → bug (latent) | VERIFIED (reproduced) | registry.cppm:130-153, 155-191. Repro scenario 1: `B{count=1: c1 c2}`; `removeChildren(B)` nulls c2's parent while A still lists c2. | B-APP-03 |
| A-APP-04 | Destroy leaves dangling scripts/slots/gizmo | bug → bug | VERIFIED | editor_layer.cppm:85 unguarded `get<Camera>`; registry.cppm:46 TODO; script_base.cppm `get()` → `Registry::get` with only `AGX_ASSERT_X` (registry.cppm:65) | B-APP-05 |
| A-APP-05 | Gizmo edits local as world | bug → bug | VERIFIED | editor_layer.cppm:89-90, 100-107: `transform.matrix()` (local) with world view/proj, decomposed back into `Transform` | B-APP-04 |
| A-APP-06 | No input arbitration ImGui/hotkeys/scripts | bug → bug | VERIFIED | no `WantCapture` in src/aegis (grep); editor_layer.cppm:34-46 Q/W/E/R + LeftControl; kinematic_movement_controller.cppm:23-37 (W/A/S/D/E/Q, LeftControl ×0.5, right mouse) | B-APP-08 |
| A-APP-07 | No frame phases; editor mutates mid-render | design → design | VERIFIED | engine.cppm:170-185; scene_defaults.cppm:19-20 Camera before Transform; test/main.cpp:783-784 reverse order and fixed 1/60; scene_update_pass.cpp:30 writes aspect; `System::onAttach/onDetach` have no callers | A-APP-07 |
| A-APP-08 | Scripts beside the ECS | design → design | VERIFIED | script_manager.cppm `addScript` returns void and owns raw `new`; script_base.cppm two-phase `init` | A-APP-08 |
| A-APP-09 | No component table | design → design | VERIFIED, one fact corrected | scene_panel.cppm:171-237 inspector list and 302-309 Add list; no Environment inspector | A-APP-09 |
| A-APP-10 | Scene logic untestable for structural reasons | design → design | VERIFIED (link-visibility consequence not built) | scene/CMakeLists.txt links core/math/EnTT PRIVATE while registry.cppm:11 does `export import aegis.math`; `Input(Window&)` is the only constructor; no tests/scene | A-APP-10 |
| A-APP-11 | Editor built in, coupled to render types | design → design | VERIFIED | engine.cppm:60; editor_layer.cppm:21-29, 51-69; menu_bar_panel.cppm:15-22 `MenuFlags`; lighting_pass.cpp:146 `drawUI` | A-APP-11 |
| A-APP-12 | Facade re-exports whole engine | design → design | VERIFIED | engine.cppm:22-41, 20 `export import`; test/main.cpp imports no `aegis.engine` | A-APP-12 |
| A-APP-13, A-APP-14 | Dead scripting/core code; Layer vs System | polish | KEPT | dynamic_movement_controller.cppm:24 `get<physics::MotionDynamics>()` while MotionDynamics is a ScriptBase (motion_dynamics.cppm:72); profiler.cppm:12 macro | — |
| B-APP-01 | Static entities never update GlobalTransform | bug → bug | VERIFIED (traced) | Teapot: obj_loader.cpp:66-68 puts Mesh/Material on the root, no tag added; simple-scene main.cpp:44-47 adds Rotator only. Only writers of `GlobalTransform` are transform_system.cppm:35-41 (onBegin) and :49-63 (DynamicTag only). CPU path (static_mesh_render_system.cpp:42) and GPU path (gpu_driven.cpp:111) read `GlobalTransform`. Directional light: lighting_pass.cpp:107-112. | **B-APP-01** |
| B-APP-02 | Componentwise hierarchy composition | bug → bug | VERIFIED (reproduced) | See A-APP-02. Asset claim checked: MetalRoughSpheres node 0 is the only non-identity parent and its children have no translation; DamagedHelmet and SciFiHelmet carry TRS on the mesh node. | **B-APP-02** |
| B-APP-03 | Registry list corruption, cycles | bug → bug | VERIFIED (a), (b) reproduced; (c) partly REFUTED | Repro (a): after `setParent(c,A); setParent(c,B)` then adding b1 to B, A lists `c b1`; `destroy(A)` nulls the parents of c and b1, while B still has `count=2`. (c): a cycle hangs `onBegin` (transform_system.cppm:29-33) only if it exists when `begin` runs. The Hierarchy panel does not loop (section 3). | **B-APP-03** |
| B-APP-04 | Gizmo local vs world | bug → bug | VERIFIED; "jumps" inference REFUTED | editor_layer.cppm:89-107. DamagedHelmet node local (0,0,0) under root (-2,0,2) (helmets main.cpp:57), so the gizmo draws at the origin. | **B-APP-04** |
| B-APP-05 | Editor destroy leaves scripts/slots/gizmo dangling | bug → bug | VERIFIED (traced) | Destroy via scene_panel.cppm:280-284 (any selected entity, including Main Camera from Scene Settings, line 153). Next frame, engine.cppm:171 → kinematic_movement_controller.cppm:74 `get<Transform>()` → registry.cppm:65 assert (debug); release calls entt `get` on a dead id, which is UB. Render passes are safe (`has<>` with a version check). | **B-APP-05** |
| B-APP-06 | Structural edits break GPU-driven static set | bug → bug | VERIFIED (traced, not run) | gpu_driven.cpp:108-118 snapshot holds `mesh.meshDataAddress()`; culling.slang:53-55 dereferences `mesh->bounds`; static_mesh.cpp:116-119 frees the buffer on the last ref. Removing Mesh via its inspector close button (scene_panel.cppm:187, 343-347) is a second trigger. | B-RENDER-04 |
| B-APP-07 | Every tree node gets ImGui ID 1 | bug → bug | VERIFIED (reproduced) | entity.cppm:45 implicit `operator bool`, no other conversion; scene_panel.cppm:249, 264. Repro: ids 3 and 4 → `(uint32_t)entity` = 1 and 1; null → 0. imgui.cpp:1600 `ConfigDebugHighlightIdConflicts = true` (1.92.4). | **B-APP-07** |
| B-APP-08 | Game input ignores ImGui focus | bug → bug | VERIFIED | See A-APP-06. Right-click on a panel → kinematic_movement_controller.cppm:172-176 sets `CursorMode::Disabled`. | **B-APP-08** |
| B-APP-09 | Mouse look/pan scaled by dt | bug → bug | VERIFIED | kinematic_movement_controller.cppm:92-99 (`rotate *= deltaSeconds`), 156-162. The numbers check out: 100 px over 1 s → 0.417 rad at 60 FPS, 0.042 rad at 600 FPS. | **B-APP-09** |
| B-APP-10 | Camera view 2 frames old; aspect out of step | bug → bug (low impact) | VERIFIED | Order scene_defaults.cppm:19-20 + engine.cppm:170-171. scene_update_pass.cpp:30 sets the aspect after the projection was built and uploads `GlobalTransform.location` (l.38), one frame newer than the view. lighting_pass.cpp:99-100. Frame 1 and the first frame after a resize use the old aspect. | **B-APP-10** |
| B-APP-11 | Unclamped dt after minimize | polish | KEPT | engine.cppm:156-165: `lastFrameBegin` updated before `waitEvents(); continue;` | **B-APP-11** |
| B-APP-12 | Release setup failures are UB | bug → bug | VERIFIED | assert.h: `AGX_ASSERT_X` is `static_cast<void>(0)` under NDEBUG; engine.cppm:221-235 `*context`/`*device`/`*renderer` with no error logged; 241-245 `*ibl` after a log; `check` returns `T{}` (208-216) → `registry.get<Transform>(Entity{})` (simple-scene:41/45) | **B-APP-12** |
| B-APP-13 | AssetManager never clears singleton | bug → bug (latent) | VERIFIED (debug half); release half corrected | asset_manager.cppm:22-26 asserts `!s_instance`, destructor defaulted. A second `Engine` constructs member `m_assets` (engine.cppm:330) → assert. `AssetManager::instance()` has no callers, so the release dangling pointer is unreachable today. | **B-APP-13** |
| B-APP-14 | Layers detached after their referents die | design → design | VERIFIED (latent) | engine.cppm:321-337 `m_layerStack` second member, so destroyed after scene, device, input, window; `~Engine` destroys the ImGui context first (l.75); layer_stack.cppm:15-21 calls `onDetach` | **B-APP-14** |
| B-APP-15 | Camera inspector degenerate values; vacuous aspect assert | polish | KEPT | scene_panel.cppm:213-217; camera_system.cppm:79 | **B-APP-15** |
| E-APP-01 | No app-layer test target | design → design | VERIFIED | tests/CMakeLists.txt only has assets, math, render_graph, rhi; tests/README.md:3 scope | A-APP-10 |
| E-APP-02 | `removeChild` leaves Siblings | bug → bug | VERIFIED (reproduced, including the step-4 assert) | registry.cppm:189-190 resets only `Parent`. Repro: `removeChild(A,c2)` fires the "not a child" assert. | B-APP-03 |
| E-APP-03 | `addChild` doesn't detach old parent | bug → bug | VERIFIED; sibling detail corrected | registry.cppm:135-152 | B-APP-03 |
| E-APP-04 | Componentwise composition | bug → bug | VERIFIED (reproduced) | transform_system.cppm:61-63 | B-APP-02 |
| E-APP-05 | Non-DynamicTag Transform writes dropped; teapot static | bug/design → bug | VERIFIED | See B-APP-01 | B-APP-01 |
| E-APP-06 | Scripts/editor need a live window for Input | design → design | VERIFIED | input.cpp:11-19 `Input(Window&)`; `keyPressed` → `glfwGetKey`; `Key::Unknown` = -1 (input.cppm:18) → glfw input.c:753-757 raises `GLFW_INVALID_ENUM`; no `glfwSetErrorCallback` in platform/ | A-CONTENT-13 |
| E-APP-07 | Mouse look ×dt | bug → bug | VERIFIED | See B-APP-09; the pan "speed-less forward·dt" detail holds (l.132-162) | B-APP-09 |
| E-APP-08 | Aspect guard never fires; near/far unchecked | bug → **polish** | VERIFIED, reclassified | camera_system.cppm:79. No path sets aspect 0 short of a 0-width extent (minimize is filtered), so the impact is polish, like B-APP-15. | B-APP-15 |
| E-APP-09 | `entityCount` counts destroyed entities | bug → bug (low impact) | VERIFIED (reproduced) | registry.cppm:27 `storage<entt::entity>()->size()`. EnTT 3.15 repro: create 3, destroy 1 → `size()` 3, `free_list()` 2, view iterates 2. statistics_panel.cppm:19. | **E-APP-09** |
| E-APP-10 | Timing/order inline in run; paths disagree | (a) bug → **polish**; (b, c) design → design | VERIFIED | engine.cppm:151-172; test/main.cpp:783-784, :936 | (a) B-APP-11; (b) B-APP-10; (c) A-APP-07 |
| E-APP-12 | Gizmo local vs world | bug → bug | VERIFIED; confidence medium → high | See B-APP-04. Its inference ("delta right only because composition is additive") is the accurate one. | B-APP-04 |
| E-APP-11, E-APP-13 | Profiler singleton/macro; PRIVATE links | polish | KEPT | profiler.cppm:12, private ctor; scene/CMakeLists.txt PRIVATE block | E-APP-11 → B-FOUND-14 / E-FOUND-06; E-APP-13 → A-BUILD-08 |

## 3. Refuted findings (partial refutations)

**A-APP-01, inference 3 ("adding a `Material` in the editor after load raises one batch's `instanceCount`").** The editor's Add Component item calls `registry.add<assets::Material>(e)` (scene_panel.cppm:309, 375). That builds `Material{}` with a null `instance`, and `onMaterialConstruct` only counts `if (material.instance)` (draw_batch_registry.cpp:111). No batch count changes. The real runtime triggers of the snapshot/batch mismatch are "Destroy Selected Entity" on a static mesh entity, removing `Mesh` with its inspector close button, and `Engine::loadModel` after `loadScene`. The editor can't remove `Material` directly, because there is no Material inspector header. The null-`Material` path does cause a different bug, E-RENDER-06 (counter underflow through `DynamicTag`). Also, inference 4's example "Simple-Scene does exactly that" is harmless in Simple-Scene: `paintingMat` and `metalMat` come from `defaults().pbrTemplate`, the same template as the loader's default material, so no batch is miscounted there. The general claim (a cross-template in-place swap goes stale) stands.

**B-APP-03 (c), cycle consequences.** It is true that `setParent(A, B)` with B a descendant of A creates a cycle that nothing rejects. But "the Hierarchy panel's sibling walk loops forever" does not happen. `drawHierachy` starts only from entities with a null `Parent` (scene_panel.cppm:76-83), and both cycle members have parents, so the subtree just disappears from the panel. If A was still linked under an old parent P (because of the no-detach bug), the walk only descends through nodes the user has opened, so it stays bounded. The `onBegin` hang (transform_system.cppm:29-33) is real, but only if the cycle exists when `Scene::begin` runs, i.e. during scene initialization. `onUpdate` and `destroy` don't walk ancestor chains.

**B-APP-04, "once global transforms are recomputed the child jumps by its parent's transform."** ImGuizmo applies a delta to the matrix it is given, which is the local one, and the result is written back as local + delta. Under today's additive composition and the identity-rotation/scale loader root, the child moves by exactly the dragged delta and does not jump. The visible defect is the misplaced gizmo: it is drawn at the local offset, e.g. the world origin for DamagedHelmet's node. Drags only go wrong after B-APP-02 is fixed and the parent has rotation or scale (E-APP-12 states this correctly). Combined with B-APP-01, nothing moves on screen for static entities anyway.

**B-APP-13, release half.** "In release, `AssetManager::instance()` returns a dangling pointer" is unreachable today: `instance()` has no callers (grep). The debug-assert half holds. A second `Engine` in one process asserts when it constructs member `m_assets`, before the `Engine` constructor body runs.

## 4. Merged duplicates

- **B-APP-01** (static entities never re-transformed; teapot doesn't rotate) ← A-APP-01 (inferences 1, 2, and the "rule lives in one comment" design point), E-APP-05.
- **B-RENDER-04** (GPU-driven static snapshot vs live batch registry) ← B-APP-06, A-APP-01 (inferences 3, 4), A-RENDER-01. Related, not a duplicate: E-RENDER-06 (null-instance Material underflow).
- **B-APP-02** (componentwise composition) ← A-APP-02, E-APP-04.
- **B-APP-03** (Registry list corruption) ← A-APP-03, E-APP-02, E-APP-03.
- **B-APP-04** (gizmo local vs world) ← A-APP-05, E-APP-12.
- **B-APP-05** (destroy leaves scripts, slots and gizmo dangling) ← A-APP-04. Related: C-03 and EA-5 (script leak, no-op reset).
- **B-APP-08** (no input capture arbitration) ← A-APP-06, B-CONTENT-07.
- **B-APP-09** (mouse look ×dt) ← E-APP-07, B-CONTENT-08.
- **B-APP-10** (camera lag, aspect out of step; bug facet) ← E-APP-10(b).
- **A-APP-07** (no frame phases; design) ← E-APP-10(c); the `camera.aspect` write overlaps A-RENDER-07.
- **B-APP-11** (dt after minimize; polish) ← E-APP-10(a); the profiler spike overlaps B-FOUND-05.
- **B-APP-12** (release UB in setup) ← C-01, D-27.
- **B-APP-15** (camera validation; polish) ← E-APP-08, B-FOUND-06.
- **A-APP-10** (app layer not unit-testable) ← E-APP-01, E-APP-13, A-BUILD-08 (link visibility).
- **A-CONTENT-13** (Input welded to Window/GLFW) ← E-APP-06; the `InputState` part of A-APP-10.
- **A-APP-13** (dead code, polish) ↔ B-FOUND-14 and E-APP-11 for the `AGX_PROFILE_SCOPE` macro; E-APP-11 ↔ E-FOUND-06 for the Profiler private constructor and B-FOUND-05 for RollingAverage.
- Unique (no duplicate found): B-APP-07, B-APP-13, B-APP-14, E-APP-09, A-APP-08, A-APP-09, A-APP-11, A-APP-12, A-APP-14.

## 5. Corrections to facts

1. **A-APP-09:** "The Add menu offers `Name` and `Transform`." The two items are in the code (scene_panel.cppm:302-303), but `drawAddComponentItem` returns early when `has<T>` holds (l.370-371). They are dead entries and never shown.
2. **E-APP-03 / B-APP-03(a):** after `setParent(c, A); setParent(c, B)`, only `c.Siblings.prev` points into B's list. `c.next` keeps its old A-list value; it was null in the repro. The walk from A only runs into B's list after a later child is appended to B, which then sets `c.next`. B-APP-03's "B's later children" wording is the exact one.
3. **B-APP-11 / E-APP-10(a):** the first dt after a restore equals the duration of the *last* `waitEvents()` block plus the loop. With no events while minimized, that is roughly the whole minimized time, as claimed. If events arrive while minimized, it is shorter.
4. **B-APP-04:** the gizmo position error is confirmed: DamagedHelmet's mesh node has local translation 0, so the gizmo draws at the world origin. See section 3 for the refuted "jump".
5. **E-APP-08 proposal:** its proposed test "view * inverseView ≈ I" would *fail* today. `CameraSystem::calcViewMatrix` builds the inverse with +f in column 2 (camera_system.cppm:64-74), and a numeric repro gives max |view·inverseView − I| = 2.0. This confirms A-FOUND-08 and should be fixed together with E-APP-08's extraction.
6. **B-APP-13:** the second-Engine assert fires during member initialization of `m_assets`, before `Engine`'s own `s_instance` check matters. `Engine::~Engine` does reset `Engine::s_instance` (engine.cppm:76).
7. **A-APP-01 inference 3 / B-APP-06:** an editor-reachable path is missing from both. Removing `Mesh` with the inspector close button (scene_panel.cppm:187, 343-347) on a static entity also frees the `StaticMesh` the snapshot still points at.
8. **B-APP-01:** `Engine::loadModel` and `ScriptManager::addScript` add no `DynamicTag` (engine.cppm:109-122; script_manager.cppm `addScript`), and no other code writes `GlobalTransform`. That rules out every alternative path by which the teapot could rotate. It is static on both the CPU and the GPU-driven path.

## 6. Run log

| Command | Exit | Key output |
|---|---|---|
| `clang++ -std=c++23 -Wno-unused-result -I external/entt/src -I external/glm build/audit/verify/app/repro.cpp -o repro.exe` (first attempt failed: non-const `storage<entt::entity>()` returns a reference, the const overload a pointer; fixed in the scratch file only) | 0 | — |
| `build/audit/verify/app/repro.exe` | 0 | `create 3, destroy 1: entityCount()=3, free_list()=2, view…=2`; `B{count=1: c1 c2}` and `parent(c2)=null` after `removeChildren(B)`, then `removeChild(A,c2)` asserts; `A{count=1: c b1} B{count=2: c b1}`, after `destroy(A)` `parent(c)=null parent(b1)=null`; `(uint32_t)entity = 1 1 ; null -> 0`; `engine=(1.00,0.00,0.00) correct=(0.00,2.00,0.00)` |
| `clang++ -std=c++23 -I external/glm build/audit/verify/app/invview.cpp -o invview.exe && invview.exe` | 0 | `max \|view*inverseView - I\| = 2.000` |
| `python` read of MetalRoughSpheres, DamagedHelmet and SciFiHelmet .gltf node transforms | 0 | Only MetalRoughSpheres node 0 has a non-identity parent matrix (scale 0.4, −90° X), and its children have no translation |
| GPU (`aegis-test`) | not run | Not needed: no app-layer claim depends on device behaviour, and the teapot case can't be shown headless without source changes. The GPU lock was never taken. |
