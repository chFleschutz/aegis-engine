# Findings: app layer, lens e (testability)

Scope: `src/aegis/engine.cppm`, `scene_defaults.cppm`, `scene_description.cppm`, `core/`, `scene/`, `scripting/`, `editor/`, `ui/`. Consumers: `examples/*`, `src/aegis/test/main.cpp`, `tests/CMakeLists.txt`. Read-only; nothing was built or run. Every fact used was re-read in the code. Problems already listed in `docs/engine-architecture.md` (singleton, `check()`, compile-time config, member order, `Scene::reset` TODO, leaked scripts, `render_graph` → `scene`) are not re-reported as findings; they appear only where they block a specific test, with the seam that removes the block.

## 1. What is tested today

**Unit tests: nothing in this group.** `tests/CMakeLists.txt` adds `assets`, `math`, `render_graph`, `rhi` only. There is no `tests/scene`, `core`, `scripting`, `editor`, `ui` or `engine`. The `render_graph` tests link `aegis-render-graph` (which links `aegis-scene` PUBLIC) but construct no `scene::Registry` or `Scene`.

**Device runs (`aegis-test`, 3 ctests, label `device`, skipped by CI).** What they exercise of this group (`src/aegis/test/main.cpp`):

| Exercised | How | Not checked |
|---|---|---|
| `Registry::create/add/get/view` | `Engine::createScene` (l.774) builds camera, 2 lights, point light, sky; `assets::Loader::load` builds the helmet with `addChild`/`setParent` (`gltf_loader.cpp` l.240-253) | no result is asserted; reparenting, `removeChild`, `destroy` never run |
| `Scene::addSystem/begin/update` | Transform then Camera system (l.783-784); `m_scene->update(1.0f / 60.0f)` per frame (l.936) | order differs from `createDefaultScene` (Camera then Transform) |
| `TransformSystem::onBegin` | static entities only; no harness entity has `DynamicTag` | `onUpdate` never does work |
| `CameraSystem::onUpdate` | indirectly via the rendered frame | projection values |
| `Profiler::instance()` | via `ScopeProfiler` in `Scene::update` | — |

Never exercised with a pass/fail signal: `aegis::Engine` (the harness has its own `Engine` and never imports `aegis.engine`), `createDefaultScene`, `SceneDescription`, `ScriptManager`, every script (`KinematicMovementController`, example scripts), `LayerStack`, `ui::UI`, `EditorLayer` and panels, `Logging`, `AssetManager`, `Engine::run`'s loop and timing. The five examples are not in CTest.

## 2. Pure logic worth testing

| Unit (file, symbol) | Why it matters | Needs GPU/window? |
|---|---|---|
| `scene/registry.cppm` `Registry::create/destroy/addChild/removeChild/setParent/removeParent/removeChildren` | hierarchy is a hand-written intrusive linked list over components; loaders, editor and TransformSystem rely on it; two bugs (E-APP-02, -03) | no |
| `Registry::entityCount` | Statistics panel; wrong today (E-APP-09) | no |
| `scene/systems/transform_system.cppm` `TransformSystem::onBegin/onUpdate` | every rendered position comes from `GlobalTransform`; composition wrong (E-APP-04); static/dynamic split silent (E-APP-05) | no |
| `scene/systems/camera_system.cppm` `calcViewMatrix`, `calcPerspectiveProjection` | every pass consumes them; vacuous aspect guard (E-APP-08) | no |
| `scene/scene.cppm` `Scene::addSystem/begin/update` | registration order is the only scheduling mechanism | no |
| `scripting/script_manager.cppm` `ScriptManager::addScript/update` | begin-once-then-update contract, script lifetime | no GPU; needs `platform::Input&`, so a window today (E-APP-06) |
| `scripting/movement/kinematic_movement_controller.cppm` | default camera of every example; frame-rate-dependent mouse look (E-APP-07) | GLFW-polled input (E-APP-06) |
| `scene_defaults.cppm` `createDefaultScene` | what every example starts from | needs a `ScriptManager`, so needs Input |
| `core/layer_stack.cppm` `LayerStack` | attach/update/detach order of the app extension point | no |
| `core/profiler.cppm` `Profiler`, `ScopeProfiler` | CPU numbers the editor shows | singleton with private ctor (E-APP-11) |
| `engine.cppm` `Engine::run` dt computation and update order | every script/system consumes `frameTimeSec`; minimize spike | inline in loop, needs window + device (E-APP-10) |
| `editor/editor_layer.cppm` `drawGizmo` → `math::decomposeTRS(transform.matrix())` | gizmo round trip; the math half is testable today in `aegis-tests-math` | ImGui for the rest; space bug E-APP-12 |

Not worth unit tests: `ui::UI`, panel drawing, `Logging` (thin wrappers over ImGui/ALOG).

## 3. Findings

### E-APP-01: The app layer has no test target, and the test policy excludes it
- **Where:** `tests/CMakeLists.txt` (subdirs `assets`, `math`, `render_graph`, `rhi`); `tests/README.md` intro and "What we test"; `src/aegis/scene/CMakeLists.txt`
- **Observed:** No `aegis-tests-scene`, `-core` or `-scripting`. The README scopes unit tests to "the new RHI stack (`aegis.rhi` / `renderer` / `platform`, later `render_graph` / `assets`)". `aegis-scene` links only `aegis-core`, `aegis-math` and EnTT, with no GPU and no window. `Registry`, `Scene` and both systems are default-constructible.
- **Inference:** The most bug-dense pure logic in the engine (E-APP-02..05, -08, -09) sits in a module that is already GPU-free. Only the target and the policy are missing. The "legacy, will be rewritten" framing kept it out, but `Registry`, the systems and the components are listed under "What stays" in `engine-architecture.md`.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every ECS extension (components, systems, hierarchy users: loaders, editor); all tests in §5
- **Proposal:** Add `tests/scene/CMakeLists.txt` with `aegis_add_test(scene SOURCES registry.test.cpp transform_system.test.cpp camera_system.test.cpp scene.test.cpp LIBS aegis-scene)`. Widen the README scope to "pure logic in any module". Fix the link declaration in the same commit (E-APP-13). Size S.

### E-APP-02: `Registry::removeChild` leaves the child's `Siblings` set, so reparenting splices the old siblings into the new parent
- **Where:** `scene/registry.cppm` `Registry::removeChild`, `Registry::addChild`
- **Observed:** `removeChild` relinks the neighbours, then resets only the parent: `// Remove parent at the end` / `get<Parent>(child) = Parent{};`. The child's own `Siblings{next, prev}` are untouched. `addChild` on a childless parent sets `children.first = child; children.last = child` and never writes `get<Siblings>(child)`. On a parent with children it sets only `.prev`.
- **Inference (failure scenario):**
  1. `A` has children `c1, c2`. Call `removeParent(c1)`; `c1.Siblings.next` is still `c2`.
  2. Call `setParent(c1, B)` on a childless `B`. `B.Children` becomes `{first=c1, last=c1, count=1}`, but walking from `first` via `Siblings.next` (as `ScenePanel::drawHierachy` l.100-109 and `removeChildren` do) yields `c1, c2, …`.
  3. Call `destroy(B)`. Its `removeChildren(B)` resets `c2`'s `Parent` and `Siblings` while `c2` is still in `A`'s list.
  4. A later `removeChild(A, c2)` asserts in debug and corrupts the list in release.
- **Severity:** bug
- **Confidence:** high (path read end to end, not executed)
- **Affects:** editor reparenting, scene reset/destroy, loaders building hierarchies; `registry.test.cpp`
- **Proposal:** Add `get<Siblings>(child) = Siblings{};` at the end of `removeChild`, and pin it with test #2. Size S.

### E-APP-03: `addChild`/`setParent` do not detach from the previous parent
- **Where:** `scene/registry.cppm` `Registry::addChild`
- **Observed:** `auto& parent = get<Parent>(child); if (parent.entity == entity) return; parent.entity = entity;`, then it appends to the new list. No `removeChild` is called for a non-null old parent, and nothing prevents cycles (only `parent != entity` is asserted in `setParent`).
- **Inference:** After `setParent(c, A); setParent(c, B)`, `c` is still in `A`'s list (`A.Children.count == 1`, `first == c`) and also in `B`'s. `c.Siblings` now links into `B`'s list, so walking `A` continues into `B`'s children. The hierarchy API is only correct for the build-once path the loaders use.
- **Severity:** bug
- **Confidence:** high
- **Affects:** any reparenting feature (editor drag and drop, attachment, prefabs); `registry.test.cpp`
- **Proposal:** Add `if (parent.entity) removeChild(parent.entity, child);` before linking, plus a debug precondition that `entity` is not a descendant of `child`. Test: "moves a child from its old parent to the new one". Size S.

### E-APP-04: `TransformSystem` composes transforms componentwise; a parent's rotation and scale do not move its children
- **Where:** `scene/systems/transform_system.cppm` `TransformSystem::onBegin`, `onUpdate`
- **Observed:** `globalTransform.location = parentGlobal.location + transform.location;` in `onUpdate`, and the same fold in `onBegin`. Rotation and scale are multiplied independently. The glTF loader builds the node hierarchy with `scene.addChild(entities[i], entities[child])` (`gltf_loader.cpp` l.246-247).
- **Inference (failure scenario):** With parent scale 2 and child local location (1,0,0), the child's global location is (1,0,0) instead of (2,0,0). With the parent rotated 90° about Z, the child stays at (1,0,0) instead of (0,1,0). Any glTF whose non-leaf nodes carry rotation or scale renders with misplaced parts. The current test assets may hide this (identity parents).
- **Severity:** bug
- **Confidence:** high for the math; medium that a shipped example shows it visibly
- **Affects:** model loading, attachments, editor gizmo on children; `transform_system.test.cpp`
- **Proposal:** Compose as `location = parent.location + parent.rotation * (parent.scale * local.location)`. Extract the fold as a pure `aegis::scene::detail::compose(const GlobalTransform& parent, const Transform& local) -> GlobalTransform` in the same module (the `tests/README.md` stateless-helper rule). Both `onBegin` and `onUpdate` then share one definition, and tests can call it without a registry. Size S.

### E-APP-05: Writes to `Transform` of an entity without `DynamicTag` are silently dropped after `begin`; Simple-Scene's teapot does not rotate
- **Where:** `TransformSystem::onUpdate` (view `<Transform, GlobalTransform, Parent, DynamicTag>`); `examples/simple-scene/main.cpp` `Rotator`, `SimpleScene::initialize`; `editor/editor_layer.cppm` `drawGizmo`; `scene/components.cppm` `DynamicTag`
- **Observed:**
  - Only `DynamicTag` entities get `GlobalTransform` recomputed after `onBegin`. Render passes read only `GlobalTransform` (`gpu_driven.cpp` l.111/122, static mesh render system), and static instances are written once per scene.
  - Simple-Scene does `auto teapot = Engine::loadModel(...); scripts.addScript<Rotator>(teapot);` with no `DynamicTag`. Nothing under `src/aegis/assets` adds one. `Rotator::update` writes `transform.rotation`.
  - The editor gizmo and inspector write `Transform` on any entity.
  - The contract exists only as a comment: "IMPORTANT: This component can ONLY be added/removed during scene initialization".
- **Inference:** The teapot's rotation never reaches the screen, and editor transform edits on static entities have no visible effect. A feature author who adds a moving script gets no signal.
- **Severity:** bug (example, editor) / design problem (silent contract)
- **Confidence:** high for the code path; not observed visually
- **Affects:** every script or system that moves entities, editor editing, runtime spawning; `transform_system.test.cpp`
- **Proposal:** Now: add `DynamicTag` to the teapot in Simple-Scene, and add test #9 to document the contract. Later, with the `World`/SceneView redesign, replace the init-time tag with a dirty mark set by whoever writes `Transform`. The real constraint is the static/dynamic split in `DrawBatchRegistry`, so that is where it belongs. Don't add debug-time snapshot checks; they'd cost more than they catch. Size S.

### E-APP-06: Scripts and the editor read a GLFW-polled `platform::Input` that only a live window can construct
- **Where:** `platform/input.cppm` `Input::Input(Window&)`; `platform/input.cpp` `keyPressed` (`glfwGetKey(m_window.glfwWindow(), ...)`); `scripting/script_base.cppm` `ScriptBase::init/input()`; `scripting/script_manager.cppm` `ScriptManager(scene::Scene&, platform::Input&)`; `editor/editor_layer.cppm` `onUpdate`
- **Observed:** `Input` is concrete, its only constructor takes `Window&`, and every query calls GLFW. `ScriptManager` requires an `Input&` even for scripts that never read input (`Rotator`, `ColorChanger`). Scripts do not reach `Engine::*` (good); the example scene descriptions do (`Engine::loadModel` etc., a known engine-architecture.md problem). `KinematicMovementController::KeyMappings` defaults four look keys to `Key::Unknown` (= `GLFW_KEY_UNKNOWN`, -1), which goes to `glfwGetKey` every frame.
- **Inference:** `ScriptManager`'s lifecycle, `createDefaultScene` and `KinematicMovementController` can't be built in a unit test without a GLFW window, and CI has no display. Headless mode has the same problem: no window means no `Input`, so no `ScriptManager`. GLFW rejects key -1 as an invalid enum, so four errors are raised per frame, unseen because no GLFW error callback is installed in `platform/`.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** new scripts, rebinding, gamepad, headless mode; tests §5 #11-#15, #17
- **Proposal:** Add one value type, not an interface: `platform::InputState { std::bitset<...> keys; std::bitset<8> buttons; CursorPosition cursor; }`. It is captured once per frame in `Window::update`, or by `Input::snapshot()`. Scripts read `const InputState&`. Cursor-mode changes stay a command on a nullable `Input*` (null when headless). `ScriptManager` takes `const InputState&`. Tests build an `InputState` literally, and headless passes an all-released state. `Key::Unknown` queries become a plain bitset miss. Size M.

### E-APP-07: Mouse look and pan are multiplied by the frame time, so sensitivity depends on frame rate
- **Where:** `scripting/movement/kinematic_movement_controller.cppm` `applyRotation`, `applyMovement`
- **Observed:** Look: `rotate.x -= (cursorPos.y - m_previousCursorPos.y) * m_mouseSensitivity;` … `rotate *= deltaSeconds;`. Pan: `moveDir -= rightDir * (cursorPos.x - ...)` … `transform.location += moveDir * deltaSeconds;`, where `moveDir` still holds the keyboard direction.
- **Inference (failure scenario):** A cursor delta is already per frame. Moving 100 px over one second rotates 100·0.25/60 ≈ 0.42 rad at 60 FPS but ≈ 0.21 rad at 120 FPS. With the FPS limit compiled off, sensitivity varies with scene load. Holding W while panning adds a second, speed-less `forward * dt`.
- **Severity:** bug
- **Confidence:** high
- **Affects:** default camera of every example; controllers copied from it; tests #14-#15
- **Proposal:** Scale only key-driven rotation by dt. Use the raw cursor delta × sensitivity for mouse look, and a separate vector for pan. Test once E-APP-06 lands: the same total cursor delta over 1 vs 2 frames gives the same orientation. Size S.

### E-APP-08: `CameraSystem`'s aspect guard can never fire, and near/far are unchecked
- **Where:** `scene/systems/camera_system.cppm` `CameraSystem::calcPerspectiveProjection`; `render_graph/passes/scene_update_pass.cpp` l.28-30
- **Observed:**
  - The guard is `AGX_ASSERT(std::abs(camera.aspect - std::numeric_limits<float>::epsilon()) > 0.0f);`. It is false only when aspect equals epsilon exactly, so `aspect == 0` passes.
  - Near and far are used raw (`camera.far / (camera.near - camera.far)`), and the editor inspector lets `Near = 0` and `Far < Near`.
  - `camera.aspect` is written by `SceneUpdatePass` during rendering ("The camera system computes the projection on the next scene update, from this aspect"), so frame 1 uses the default 1.0.
- **Inference:** `aspect == 0` gives `inf` in `[0][0]`, and `near == far` divides by zero. The intended guard was `std::abs(aspect) > epsilon`.
- **Severity:** bug (polish-level impact)
- **Confidence:** high
- **Affects:** camera features (orthographic, multiple cameras, render-to-texture); `camera_system.test.cpp`
- **Proposal:** Extract `aegis::scene::detail::perspective(float fovY, float aspect, float near, float far) -> glm::mat4` and `detail::lookAtView(const GlobalTransform&)` returning view and inverse view. `CameraSystem` keeps the loop. Tests compare against `glm::perspectiveRH_ZO` with the Y flip and check `view * inverseView ≈ I`. Fix the guard. Moving the aspect write out of the render pass belongs to the SceneView step, not here. Size S.

### E-APP-09: `Registry::entityCount` counts destroyed entities
- **Where:** `scene/registry.cppm` `Registry::entityCount`; caller `editor/panels/statistics_panel.cppm` l.19
- **Observed:** `return m_registry.storage<entt::entity>()->size();`. EnTT is 3.15 (`external/entt/src/entt/config/version.h`). Its entity storage keeps released ids in the packed array and tracks the live count as `free_list()` (`storage.hpp` l.1169: `for(const auto sz = base_type::size(); first != last && base_type::free_list() != sz; ...)`).
- **Inference:** Create 3 entities and destroy 1, and `entityCount()` still returns 3. The Statistics panel overstates after any editor "Destroy Selected Entity".
- **Severity:** bug (polish-level impact)
- **Confidence:** high
- **Affects:** statistics, and any future capacity or serialization logic using the count; test #4
- **Proposal:** Return `->free_list()` and pin it with "does not count destroyed entities". Size S.

### E-APP-10: Frame timing and update order are inline in `Engine::run`, so neither is testable and the two setup paths disagree
- **Where:** `engine.cppm` `Engine::run`; `scene_defaults.cppm` `createDefaultScene`; `src/aegis/test/main.cpp` `Engine::createScene` l.783-784 and l.936
- **Observed:** `run` computes `frameTimeSec` from `steady_clock` and sets `lastFrameBegin = currentFrameBegin` before the minimized branch (`m_window.waitEvents(); continue;`). It then calls `m_scene.update`, `m_scriptManager.update` and `m_layerStack.update` in that order. `createDefaultScene` registers `CameraSystem` then `TransformSystem`. `aegis-test` registers Transform then Camera, and feeds a fixed `1.0f / 60.0f`.
- **Inference:**
  - **(a) No dt clamp.** After a minimize, all the blocked time lands in the next frame's dt. Restoring after 10 s with W held jumps the camera 10 s × speed; future physics would blow up.
  - **(b) Camera lag.** Scripts write `Transform` after the systems run, and the camera system runs before the transform system. The camera view therefore trails its script by two frames in the examples but one in `aegis-test`, so the harness doesn't test what the examples run.
  - **(c) Hidden order contract.** "Systems → scripts → layers" is the engine's scheduling contract, and it exists only as three lines inside an inline class.
- **Severity:** bug (a) / design problem (b, c)
- **Confidence:** high for (a) and the order facts; the visible effect of (b) is inferred
- **Affects:** any time-dependent feature (physics, animation, fixed step); headless determinism; tests #10, #16
- **Proposal:** Two small pieces, no scheduler:
  1. `core::FrameClock`: pure, `tick(time_point now) -> float`, with a max-dt clamp and an optional fixed dt for headless/CTest.
  2. A free function `stepSimulation(scene::Scene&, scripting::ScriptManager&, core::LayerStack&, float dt)` that holds the order and is shared by `Engine::run` and later the harness.

  Register Transform before Camera in `createDefaultScene`. Size S.

### E-APP-11: `Profiler` is a singleton with a private constructor, and the profiling macro cannot be used twice in a scope
- **Where:** `core/profiler.cppm` `Profiler::instance`, `ScopeProfiler`, `AGX_PROFILE_SCOPE`
- **Observed:**
  - `Profiler() = default;` is private, and `ScopeProfiler`'s destructor always writes to `Profiler::instance()`.
  - `#define AGX_PROFILE_SCOPE(name) aegis::ScopeProfiler profiler##__LINE__(name)`: `##` suppresses expansion of `__LINE__`, so the variable is always named `profiler__LINE__`. The macros have no users.
  - `utils::RollingAverage::average()` divides by N from the first sample.
- **Inference:** Tests can only observe the shared global, so state leaks between cases and names must be unique. Two `AGX_PROFILE_SCOPE` uses in one scope would not compile. Startup averages read 1/50 of the true value on frame 1.
- **Severity:** polish
- **Confidence:** high
- **Affects:** profiling features; test #19
- **Proposal:** Make the constructor public, keeping `instance()` as the default sink. Give `ScopeProfiler` an optional `Profiler&` defaulting to `instance()`. Fix the macro with a two-level concat, or delete the unused macros. Size S.

### E-APP-12: The editor gizmo edits the local `Transform` as if it were world space
- **Where:** `editor/editor_layer.cppm` `EditorLayer::drawGizmo`
- **Observed:** `glm::mat4 transformMatrix = transform.matrix();` (local `Transform`) goes to `ImGuizmo::Manipulate` with the camera's world view and projection, and the result is decomposed back into the local `Transform`. Every glTF node without a parent is re-parented to the loader root (`gltf_loader.cpp` l.250-253 `scene.setParent(entity, m_root)`), so most selectable meshes are children.
- **Inference:** For a child of an entity at (10,0,0) with local (1,0,0), the gizmo draws at (1,0,0) while the mesh renders at (11,0,0). Drags apply the right delta only because composition is currently additive (E-APP-04). Once that is fixed, drags on children of rotated or scaled parents go the wrong way.
- **Severity:** bug
- **Confidence:** medium (reasoned, not observed)
- **Affects:** editor transform editing; test #7
- **Proposal:** Manipulate `GlobalTransform::matrix()`, then convert back with a pure `detail::toLocal(parentGlobal, world)` next to `compose` (E-APP-04), tested as its inverse. Size S.

### E-APP-13: `aegis-scene` declares as PRIVATE what its interface re-exports
- **Where:** `src/aegis/scene/CMakeLists.txt` (`PRIVATE aegis-core aegis-math EnTT::EnTT`); `scene/registry.cppm` and `scene/components.cppm` (`export import aegis.math;`); `scene/scene.cppm` (`import aegis.core.profiler;`); same pattern in `scripting/CMakeLists.txt` (scene, math PRIVATE)
- **Observed:** Every current consumer also reaches `aegis-math` and `aegis-core` through a PUBLIC link (`aegis-assets` links both PUBLIC), so no existing target depends on the PRIVATE edges.
- **Inference (reasoned, not built):** A test target linking only `aegis-scene` would most likely still compile. CMake collates a consumer's module map from its link closure, and a static library's PRIVATE dependencies stay in that closure as `$<LINK_ONLY:...>`. A test TU needs no EnTT include path, because `scene/entity.cppm` re-exports the `entt` names it uses. Nothing in the repo proves this either way today. The declaration is wrong regardless, since re-exported modules are part of the interface.
- **Severity:** polish (risk to E-APP-01)
- **Confidence:** medium that the declaration is wrong; low on whether a scene-only test would fail today
- **Affects:** `aegis-tests-scene`, `aegis-tests-scripting`; the README rule "each binary links only its own library"
- **Proposal:** In `aegis-scene`, make `aegis-math` PUBLIC (it is re-exported) and `aegis-core` PUBLIC (the `aegis.scene` interface imports it). EnTT can stay PRIVATE (`aegis-render-graph` links it itself). Do the same in `aegis-scripting`. Land it with the commit that adds `tests/scene`, so the first build proves it. Size S.

## 4. Blockers and the smallest seam for each

| Blocker | Blocks | Seam (replaces → with) | Unlocks | Over-engineering check |
|---|---|---|---|---|
| No `tests/scene` target (E-APP-01) | Registry, systems, Scene | add target; PUBLIC links (E-APP-13) | #1-#10 | pure CMake |
| `Engine` singleton + static `Engine::loadModel/...` in scene descriptions | `SceneDescription::initialize` content | the doc's `SceneContext&` plan; `createDefaultScene` itself does not use `Engine` | default-scene composition tests | nothing beyond the doc's plan |
| `ScriptManager`/scripts need `Input&`, so a window (E-APP-06) | ScriptManager, KinematicMovementController, createDefaultScene | `platform::Input&` → `const platform::InputState&`, plus nullable `Input*` for cursor mode | #11-#15, #17; headless | a value type, not an interface |
| Scripts `new`'d, never deleted, no removal; `Registry::destroy` has `// TODO: Destroy all scripts attached to the entity` | lifetime tests (a leak sanitizer would flag every test) | `std::vector<ScriptBase*>` → `std::vector<std::unique_ptr<ScriptBase>>`; `removeScripts(Entity)` called before `destroy` | #13 | the doc's World-owned scripts, first step |
| `Profiler::instance()`, private ctor (E-APP-11) | Profiler, ScopeProfiler | public ctor + optional `Profiler&` | #19 | one parameter |
| Loop inline in `Engine::run` (E-APP-10) | timing, update order | `FrameClock` + `stepSimulation` | #10, #16 | two functions, no scheduler |
| Hierarchy math inline (E-APP-04) | composition, gizmo inverse | `detail::compose` / `detail::toLocal` | #6, #7 | README "stateless helper in `detail`" rule |
| Projection inline (E-APP-08) | projection/view | `detail::perspective` / `detail::lookAtView` | #8 | same rule |
| `assert`-only preconditions (`Registry::get/add/remove/removeChild`; `AGX_ASSERT_X` → `assert(false)` aborts in debug, nothing in release) | misuse tests | none for tests: test valid paths and post-conditions. Separately, turn user-reachable ones into early returns (`removeChild` on a non-child; `ScriptBase::get` on a destroyed entity) | — | keep asserts for programmer errors |
| entt types in `Registry::view/onConstruct` | not a test blocker (`entity.cppm` re-exports the names) | — | — | coupling issue owned by the SceneView step |
| `ui::UI` creates the ImGui context in its ctor | EditorLayer, UI | none: test `LayerStack` alone with a test `Layer`; leave UI/editor to headless device runs | #18 | — |

## 5. Candidate tests, ranked

| # | TEST_SUITE / TEST_CASE | Unit | Seam | Value | Effort |
|---|---|---|---|---|---|
| 1 | `scene::Registry` / "creates an entity with the required components" | `create` | target only | pins the Name/Transform/GlobalTransform/Parent/Siblings/Children invariant | S |
| 2 | `scene::Registry` / "keeps both child lists consistent when a child is reparented" | `removeParent`, `setParent`, `addChild` | none | catches E-APP-02, -03 | S |
| 3 | `scene::Registry` / "unlinks a removed child and keeps its siblings linked" (SUBCASE first / middle / last) | `removeChild` | none | linked-list edges | S |
| 4 | `scene::Registry` / "does not count destroyed entities" | `entityCount` | none | catches E-APP-09 | S |
| 5 | `scene::Registry` / "turns the children of a destroyed entity into roots" | `destroy`, `removeChildren` | none | editor destroy path | S |
| 6 | `scene::TransformSystem` / "applies the parent's rotation and scale to a child's location" | `onBegin` / `detail::compose` | compose optional | catches E-APP-04 | S |
| 7 | `scene::TransformSystem` / "converts a world transform back to the local one" | `detail::toLocal` | E-APP-12 | gizmo correctness | S |
| 8 | `scene::CameraSystem` / "builds a Vulkan projection with flipped Y and [0,1] depth"; "keeps the inverse view matrix consistent with the view matrix" | `calcPerspectiveProjection`, `calcViewMatrix` | none (1-entity registry) or `detail::perspective` | every pass consumes these | S |
| 9 | `scene::TransformSystem` / "updates only entities with DynamicTag after begin" | `onUpdate` | none | documents E-APP-05 | S |
| 10 | `scene::Scene` / "runs systems in registration order" | `Scene::begin/update` | none (recording test System) | scheduling contract | S |
| 11 | `scripting::ScriptManager` / "calls begin once before the first update" | `addScript`, `update` | E-APP-06 | script lifecycle | S after seam |
| 12 | `scripting::ScriptManager` / "begins a script added during an update on the next frame" | `handleNewScripts` | E-APP-06 | scripts spawning scripts | S after seam |
| 13 | `scripting::ScriptManager` / "stops updating the scripts of a removed entity" | lifetime | E-APP-06 + unique_ptr + `removeScripts` | fixes leak and dead-entity `get<T>` | M |
| 14 | `scripting::KinematicMovementController` / "turns by the same angle for the same mouse movement at any frame rate" | `applyRotation` | E-APP-06 | catches E-APP-07 | S after seam |
| 15 | `scripting::KinematicMovementController` / "moves at the configured speed along the camera's forward axis" | `applyMovement` | E-APP-06 | default camera everywhere | S after seam |
| 16 | `core::FrameClock` / "clamps the delta after a long stall"; "returns the fixed delta when one is set" | `FrameClock` | E-APP-10 | minimize spike; deterministic headless | S |
| 17 | `aegis::defaults` / "creates a camera, two lights and a sky and fills the scene slots" | `createDefaultScene` | E-APP-06 | baseline every example builds on; guards duplicate defaults once `reset` exists | S after seam |
| 18 | `core::LayerStack` / "attaches on push, updates in push order, detaches on destruction" | `LayerStack` | none (test Layer) | app extension point | S |
| 19 | `core::Profiler` / "averages the last N samples per name" | `Profiler`, `ScopeProfiler` | E-APP-11 | editor numbers | S |
| 20 | `math` / "decomposeTRS inverts tranformationMatrix" | `math::decomposeTRS` | none (aegis-tests-math) | gizmo round trip | S |

#1-#10 need only the target and catch four real bugs. #11-#17 each need one small seam that the engine-architecture.md plan needs anyway.

## 6. Strategy

**Scene and scripting need neither a GPU nor a fake.** `Registry`, `Scene`, both systems and `LayerStack` already construct with no arguments. The one dependency in the way is `platform::Input`, and the fix is a value snapshot, not a mock: a script is a function of components, input state and dt, and all three can be literals in a test. Don't add an `IInput` interface or a fake device. No test in §5 needs asset components. `createDefaultScene` takes an `Environment` by value, and an all-null one is fine for counting entities.

**Headless engine mode: what it takes in this group.**
1. `AppDesc { title, extent, headless, frames, screenshot, gpuDriven, fixedDt }` and `Engine::create(desc) -> std::expected<...>` (the doc's step 1). The renderer already supports `window = nullptr` plus `offscreen.extent`, and `aegis-test` uses both.
2. `Engine` stops constructing `Window` and `Input` unconditionally. Today `m_window{...}` and `m_input{m_window}` are by-value members; make them `unique_ptr`, null when headless, as `aegis-test` already does.
3. ImGui: skip `ImGui_ImplGlfw_InitForVulkan` and `ImGui_ImplGlfw_NewFrame` when headless, and set `io.DisplaySize` from the renderer extent (exactly `aegis-test`'s `buildUi`). `EditorLayer` and the camera script read an all-released `InputState` (E-APP-06).
4. `FrameClock` with a fixed dt (E-APP-10), so headless frames are deterministic and screenshots comparable.
5. `run()` returns an exit code: non-zero on setup failure or a non-zero validation error count (the harness's rule).
6. `examples/*/CMakeLists.txt`: `add_test(NAME example-<name> COMMAND <exe> --headless --frames 120)`, labelled `device`. Content scenes need the `aegis-assets` submodule, so skip them when it is absent.

**What it catches:**
- setup regressions in the real `Engine` path (today only the harness's copy is checked);
- validation errors across the full pass list with real content (Sponza, helmets, 10k dynamic cubes);
- crashes in scene descriptions, scripts and the editor's `onUIRender`, which runs inside `UiPass::execute`;
- with `--screenshot` plus later reference images, wrong output such as E-APP-05's static teapot.

**What it doesn't catch:**
- the hierarchy and math bugs in §3, which are visually subtle and belong to unit tests;
- input, swapchain and present, which still need the windowed run.

**Reusing engine setup in `aegis-test`.** `aegis-test`'s `Engine::createScene` duplicates device → defaults → scene → IBL → frame graph, with a different system order and its own ImGui context. Once `Engine::create(AppDesc)` exists, its `--scene` / `--gpu-driven` paths become `Engine::create({ .headless = true, ... })` plus a scene function. The harness keeps only what the engine doesn't do: the RHI `verify*` checks and the triangle pipeline. Order:
1. Build the headless `Engine::create`; this alone gives the examples CTests.
2. Switch the harness's scene path to it. This deletes about 80 lines of duplicated setup and the system-order mismatch.
3. Keep the triangle mode as the minimal RHI-only check.

**Verdict:** A headless `Engine` is cheap to reach: the renderer, offscreen target and ImGui-without-GLFW recipe all exist in `aegis-test`. The real blockers are the unconditional `Window`/`Input` members and the window-bound `Input`, not the GPU.
