# Findings A: app layer, architecture lens

Scope: `src/aegis/engine.cppm`, `scene_defaults.cppm`, `scene_description.cppm`, `core/`, `scene/`, `scripting/`, `editor/`, `ui/`. Consumers read: all five `examples/*/main.cpp` and `src/aegis/test/main.cpp`. This is a read-only audit, phase 2. Every fact below was re-checked against the code, because the map came from a quicker pass.

"EA-n" refers to the n-th problem section of `docs/engine-architecture.md`:

1. singleton
2. setup can't fail
3. compiled-in config
4. member order
5. half-built scene lifecycle
6. render code depends on the scene
7. leftovers

Those are confirmed and not repeated here. The findings below are what that document misses or gets wrong.

Severity count: 6 bugs, 6 design problems, 2 polish.

## 1. Findings

### A-APP-01: Ordinary scene edits don't reach the renderer (the static/dynamic rule isn't enforced)
- **Where:**
  - `scene/systems/transform_system.cppm` `TransformSystem::onUpdate`
  - `render_graph/passes/gpu_driven.cpp` `InstanceUpdatePass::sceneInitialized`
  - `render_graph/draw_batch_registry.cpp` signal handlers
  - `editor/panels/scene_panel.cppm` (inspector, Add Component)
  - `editor/editor_layer.cppm` `drawGizmo`
  - `examples/simple-scene/main.cpp` `Rotator`
- **Observed:**
  - `TransformSystem::onUpdate` only visits `view<Transform, GlobalTransform, Parent, DynamicTag>()`. Static entities get a `GlobalTransform` once, in `onBegin`.
  - The CPU path (`static_mesh_render_system.cpp`) and the lighting pass read `GlobalTransform`.
  - The GPU path writes `StaticInstances` once, in `sceneInitialized`, under the comment "Static instances never change".
  - Neither loader adds `DynamicTag` (no match under `src/aegis/assets`).
  - Simple-Scene adds a `Rotator` script to the teapot without a `DynamicTag`.
  - The editor inspector and gizmo write the `Transform` of any selected entity.
  - `DrawBatchRegistry` keeps its batch counts and offsets in step through entt `on_construct`/`on_destroy` of `Material` and `DynamicTag`.
  - Replacing `Material::instance` in place fires no signal. Simple-Scene does exactly that: `registry.get<Material>(floorPlane).instance = paintingMat;`.
  - `DynamicTag`'s comment says it "can ONLY be added/removed during scene initialization". Nothing enforces this, and the editor can add or remove `Material` at runtime.
- **Inference:**
  1. Simple-Scene's teapot doesn't rotate on screen.
  2. Moving any loaded model with the gizmo or the inspector has no visible effect, although the gizmo itself moves.
  3. On the GPU-driven path, adding a `Material` in the editor after load raises one batch's `instanceCount` and shifts `firstInstance` of the later batches. `StaticInstances` keeps the old layout, so later batches draw the wrong instance ranges.
  4. Assigning an instance from a different `MaterialTemplate` in place leaves it counted under the old batch.

  The rule (static data is frozen after `begin`; changes must fire signals) lives in one comment. Every writer can break it, and nothing reports it when they do.
- **Severity:** bug
- **Confidence:** high for 1, 2 and 4; medium for 3 (not run)
- **Affects:** every editor feature that writes to the scene, scripting, scene load into a live engine, and mesh or material hot reload.
- **Proposal:** make the World responsible for the rule instead of a comment.
  - Add a static change counter, `World::markStaticDirty()`. Bump it from editor edits, from `reset()` and from structural edits after `begin`.
  - `InstanceUpdatePass` (the bridge later) rebuilds the static instances when the counter changes, and `TransformSystem` recomputes static transforms on the same trigger.
  - Scripts that move an entity need `DynamicTag`; `addScript` can add it.
  - Material swaps go through `setMaterial`, which removes and re-adds the component so the signals fire. The alternative is to rebuild batches from views.
  - Replaces the comment and the frozen buffer. Unlocks editing static scenes, live scene load and hot reload.
  - Size: S for the counter, M for rebuilding batches from views.

### A-APP-02: Hierarchy transforms ignore parent rotation and scale
- **Where:** `scene/systems/transform_system.cppm` `onBegin` and `onUpdate`.
- **Observed:**
  ```cpp
  globalTransform.location = parentGlobal.location + transform.location;
  globalTransform.rotation = parentGlobal.rotation * transform.rotation;
  globalTransform.scale = parentGlobal.scale * transform.scale;
  ```
  The glTF loader keeps the node hierarchy with local TRS values (`gltf_loader.cpp` `buildScene`: `scene.create(name, translation, rotation, scale)`, then `addChild`).
- **Inference:** a child's offset is never rotated or scaled by its parent.
  - **Scenario 1:** a glTF whose parent node is rotated 90° (common in Y-up to Z-up exports) renders a translated child in the wrong place.
  - **Scenario 2:** setting `registry.get<Transform>(modelRoot).scale = {2, 2, 2}` on a multi-node model scales each part in place but keeps the unscaled offsets, so the model falls apart.
  - **Scenario 3:** rotating a parent in the editor doesn't swing its children around it.

  `onUpdate` also walks entities in storage order, so a child can read its parent's value from the previous frame. The code comment admits this ("lags 1 frame per parent").
- **Severity:** bug
- **Confidence:** high for the math. Whether a shipped asset hits it is not verified.
- **Affects:** loaded models, the editor gizmo and hierarchy, and future animation or physics.
- **Proposal:**
  - Compose properly: `location = pL + pR * (pS * l)`, or multiply matrices.
  - Update parents before children (sort by depth, or walk from the roots).
  - Extract the composition as a pure function in `aegis::scene::detail` and unit-test it. Parent scale 2 with child (1,0,0) must give (2,0,0); parent rotated 90° about Z with child (1,0,0) must give (0,1,0).
  - Size S.

### A-APP-03: `Registry` re-parenting corrupts the sibling lists
- **Where:** `scene/registry.cppm` `Registry::addChild`, `removeChild`, `setParent`.
- **Observed:**
  - `addChild` overwrites `Parent.entity` but never removes the child from its old parent's list. It sets only `Siblings.prev`, so the child's old `Siblings.next` stays.
  - `removeChild` resets the child's `Parent` but not its `Siblings`.
- **Inference:**
  - **Scenario 1:** A has children c1 and c2. After `removeParent(c1)`, `c1.Siblings.next` is still c2. `setParent(c1, B)` on a childless B makes c1 both B's first and last child. Walking B's children then visits c1 and c2, so `removeChildren(B)` clears c2's `Parent` while c2 is still in A's list.
  - **Scenario 2:** `setParent(c, A)` followed by `setParent(c, B)` leaves c in A's list. Destroying A later sets c's parent to null, so c silently leaves B.

  Today only the loader re-parents, and it only parents fresh entities. The first drag-and-drop, prefab or scene-load feature will hit this.
- **Severity:** bug (latent)
- **Confidence:** high
- **Affects:** editor hierarchy editing, scene load, prefabs. Pure CPU logic, so it is the best first test target.
- **Proposal:**
  - `addChild` calls `removeParent(child)` first.
  - `removeChild` also resets the child's `Siblings`.
  - Add `tests/scene/registry.test.cpp`.
  - Size S.

### A-APP-04: Destroying an entity leaves dangling scripts, scene slots and editor references
- **Where:**
  - `scene/registry.cppm` `Registry::destroy` (`// TODO: Destroy all scripts attached to the entity`)
  - `scene/scene.cppm` slots `m_mainCamera`, `m_ambientLight`, `m_directionalLight`, `m_skybox`
  - `editor/panels/scene_panel.cppm` `drawEntityActions` and the close button in `drawComponent`
  - `editor/editor_layer.cppm` `drawGizmo`
- **Observed:**
  - The editor can destroy any entity and can remove any optional component, including `Camera` from the main camera.
  - `drawGizmo` calls `registry.get<Camera>(scene.mainCamera())` unguarded. `get` is guarded only by `AGX_ASSERT_X`.
  - Scripts store a `Registry*` and an `Entity` and call `get<T>()` every frame. There is no way to remove a script.
- **Inference:**
  - **Scenario 1:** remove the main camera's Camera component, or destroy the camera entity, then press W with an entity selected. Debug builds assert; release builds read an invalid storage slot (undefined behaviour).
  - **Scenario 2:** destroy the main camera. Its `KinematicMovementController` keeps calling `get<Transform>()` on a dead entity.
  - The render passes check `has` before reading the slots, which hides the problem.
  - The slots are a side table of entity IDs outside the ECS, which scene serialization would have to special-case.
- **Severity:** bug
- **Confidence:** high
- **Affects:** editor delete and remove actions, scripting, save/load, undo.
- **Proposal:**
  - Make scripts a component (A-APP-08), so they die with their entity.
  - Replace the slots with a `MainCamera` tag and view lookups. `AmbientLight`, `DirectionalLight` and `Environment` are already components.
  - The gizmo and inspector check `valid`/`has` before `get`.
  - Size S–M.

### A-APP-05: The gizmo edits the local transform as if it were world space
- **Where:** `editor/editor_layer.cppm` `EditorLayer::drawGizmo`.
- **Observed:** the gizmo is given `transform.matrix()`, which is the local `Transform`. Its result is decomposed straight back into the local `Transform` with `math::decomposeTRS`. The view and projection matrices are world space.
- **Inference:** every glTF node is parented to the model root, so for those nodes the gizmo sits at the local offset rather than the world position, and a drag writes a world-space delta into local space. Together with 01 and 02, the gizmo can't correctly move parts of a loaded model.
- **Severity:** bug
- **Confidence:** high
- **Affects:** editor gizmo, undo, hierarchy editing.
- **Proposal:** draw the gizmo from `GlobalTransform` and convert the result back through the inverse of the parent's global matrix, reusing the pure helper from 02. Size S.

### A-APP-06: No input arbitration between ImGui, editor hotkeys and scripts
- **Where:**
  - `editor/editor_layer.cppm` `onUpdate` (Q/W/E/R, LeftControl)
  - `scripting/movement/kinematic_movement_controller.cppm` `KeyMappings` (W/A/S/D/E/Q, LeftControl)
  - `engine.cppm` `ImGui_ImplGlfw_InitForVulkan(..., true)`
- **Observed:** `WantCaptureKeyboard`/`WantCaptureMouse` is used nowhere (grep over `src/aegis` outside `ai/`). Scripts and layers read `platform::Input` directly.
- **Inference:**
  - Typing a name such as "Sweden" in the inspector moves the camera and switches the gizmo mode.
  - Pressing W to pick the translate gizmo also moves the camera.
  - Holding LeftControl to snap also slows the camera.
  - Right-dragging over a panel rotates the camera.
- **Severity:** bug (usability)
- **Confidence:** high
- **Affects:** every editor panel with text or drag input, and every input-driven script.
- **Proposal:** each frame, before scripts run, build an `InputState` for the game side that is empty while ImGui wants input capture. The editor reads the raw state. Size S, together with A-APP-10.

### A-APP-07: The frame loop has no phases; order is registration order, and the editor changes the scene mid-render
- **Where:**
  - `engine.cppm` `Engine::run`
  - `scene/scene.cppm` `Scene::update`
  - `scene_defaults.cppm` `createDefaultScene`
  - `render_graph/passes/scene_update_pass.cpp` `SceneUpdatePass::execute`
  - the UI pass, which calls `buildUi` → `UI::render` → `EditorLayer::onUIRender`
- **Observed:**
  - Each frame runs: `scene.update` (systems in the order they were added) → `scriptManager.update` → `layerStack.update` → `flushUploads` → `renderFrame`. The editor UI runs inside `UiPass::execute`.
  - `createDefaultScene` always registers `CameraSystem` before `TransformSystem`, and example systems such as eval-scene's `RotationSystem` come after both.
  - `SceneUpdatePass` writes `camera.aspect` into the ECS during rendering.
  - `aegis-test` registers Transform before Camera, uses a fixed delta of `1.0f / 60.0f`, and flushes uploads before the scene update.
  - `Engine` uses an unclamped wall-clock delta that includes the `waitEvents()` time spent minimized.
  - `System::onAttach`/`onDetach` are never called.
- **Inference:**
  - The camera's view matrix lags the movement script by two writes, and `RotationSystem` output reaches the GPU a frame late.
  - Frame 1 renders with an aspect ratio of 1.0.
  - Editor creates, destroys and component removals land after `InstanceUpdatePass` has written this frame's batches.
  - A new system can't run before transforms are updated, because `loadScene` calls `createDefaultScene` first.
  - `aegis-test --scene` doesn't exercise the engine's frame order.
  - Restoring a minimized window gives scripts a delta of several seconds, so the camera jumps.
- **Severity:** design problem
- **Confidence:** high for the facts; medium for the exact latency effects
- **Affects:** new systems, job-based frame work, editor edits, hot reload, deterministic tests.
- **Proposal:** one function owns the frame, with fixed named phases. Within a phase, systems run in the order they were added.
  1. input: poll and build `InputState` with capture applied
  2. pre-update: apply reload requests and queued editor commands
  3. update: scripts and gameplay
  4. post-update: transforms, then camera, with aspect taken from the renderer extent
  5. extract
  6. render: the UI only reads, or queues structural edits for phase 2
  7. present

  A `Clock` provides the delta: wall clock with a clamp in the app, a fixed step for `--frames` runs and tests. A phase enum is enough; no scheduler is needed. Size M.

### A-APP-08: Scripts live beside the ECS instead of in it
- **Where:**
  - `scripting/script_manager.cppm` `ScriptManager`
  - `scripting/script_base.cppm` `ScriptBase::init`
  - `scene_description.cppm` `initialize(Scene&, ScriptManager&)`
- **Observed:**
  - `addScript<T>` returns `void`, so `KinematicMovementController::setMoveSpeed` can't be reached from a scene.
  - Scripts can't be looked up from an entity, removed, or iterated per entity.
  - `ScriptBase` is constructed first and wired up later through `init`. Its comment warns not to use member functions in the constructor.
  - Every scene must take a `ScriptManager&`.
  - The leak and the missing reset are EA-5.
- **Inference:** the editor can't show scripts, serialization can't save them, and destroying an entity can't remove them. The doc's fix ("World owns scripts as `unique_ptr`") cures the leak but keeps a second container next to the ECS, so all of these gaps remain.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** scripting, inspector, serialization, reset, script tests.
- **Proposal:**
  - Add a `Scripts` component holding `std::vector<std::unique_ptr<ScriptBase>>`. entt supports move-only components.
  - A `ScriptSystem` in the update phase calls `begin` once and then `update`.
  - `add<T>(args...)` returns `T&`, so scenes can configure the script.
  - Scripts get their context per call: `update(ScriptContext&)` with the registry, entity, `const InputState&` and delta.
  - Replaces `ScriptManager` and `init`. Size M.

### A-APP-09: Adding a component means editing several central files; there is no component table
- **Where:**
  - `scene/components.cppm` and `ComponentTraits`
  - `editor/panels/scene_panel.cppm` `drawEntityProperties`, `drawAddComponent`
  - `editor/panels/statistics_panel.cppm`
- **Observed:**
  - The inspector and the Add menu are two separate hand-written lists.
  - The Add menu offers `Name` and `Transform`, which every entity already has.
  - `Environment` has no inspector. `Material` can be added, as an empty instance, but not inspected.
  - `Near` can be dragged to 0 and `Far` below `Near`.
  - `CameraSystem` asserts `std::abs(camera.aspect - epsilon) > 0`, which holds for every aspect except exactly epsilon.
  - No list of component types exists anywhere.
- **Inference:** a new component touches `components.cppm` and two places in `ScenePanel`, optionally `StatisticsPanel` too. Serialization and undo would each add more per-type lists.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** new components, inspector, save/load, undo.
- **Proposal:** a plain table, not reflection. entt::meta is ruled out anyway, because it crashes clang -O3 (per `scene.cppm`).
  - `ComponentInfo { name; required; has; add; remove; drawInspector; }`, with save/load fields added later.
  - Register with `registerComponent<T>({...})` next to the component.
  - `ScenePanel` loops over the table, which replaces both hand lists and `ComponentTraits`.
  - Each inspector entry validates its own fields.
  - Testable: every registered component's add/has/remove can be checked on a bare `Registry`.
  - Size S.

### A-APP-10: Scene logic can't be unit-tested yet, for structural reasons rather than the GPU
- **Where:**
  - `platform/input.cppm` `Input(Window&)`
  - `scene/CMakeLists.txt`, `scripting/CMakeLists.txt` (PRIVATE links)
  - the `#include "core/assert.h"` in `scene/*.cppm`, reached through a `../` include path
  - the wall clock in `Engine::run`
- **Observed:**
  - `Registry`, `Scene`, `TransformSystem` and `CameraSystem` need no GPU, yet `tests/scene` doesn't exist.
  - `aegis-scene` links `aegis-math` and EnTT PRIVATE, although `registry.cppm` does `export import aegis.math`.
  - Every script reads `platform::Input`, whose only constructor takes a GLFW `Window&`.
- **Inference:** a test target that links only `aegis-scene` may not see `aegis.math` (medium confidence; inferred from CMake, not built). Scripts can't be tested without a window, and frame results depend on a wall clock. The "extract, don't mock" rule from `tests/README.md` applies directly.
- **Severity:** design problem
- **Confidence:** high, medium for the link visibility
- **Affects:** all app-layer tests, and the fixes for 02, 03, 05 and 08.
- **Proposal:**
  - Link PUBLIC what the interfaces re-export.
  - Split out an `InputState` value type that `Input` fills each frame.
  - Add `aegis-tests-scene` covering the hierarchy, transform composition, camera matrices, the component table, and scripts driven by a fake `InputState`.
  - Size S each.

### A-APP-11: The editor is built into the engine and coupled to concrete render types
- **Where:**
  - `engine.cppm` constructor: `push<editor::EditorLayer>(m_input, *m_renderer, *m_frameGraph, m_drawBatches, m_scene)`
  - `editor/editor_layer.cppm`
  - `editor/panels/menu_bar_panel.cppm` `MenuFlags`
  - `editor/CMakeLists.txt`
- **Observed:**
  - `EditorLayer` holds five references and one member per panel, and calls each panel by hand. Each panel's `draw` has a different signature.
  - `MenuBarPanel` has a hard-coded flags enum.
  - Every example gets the editor, with no way to turn it off.
  - Render passes carry their own ImGui code: `RenderPass::drawUI`, and `LightingPass::drawUI` edits a member in place. Pass settings are therefore not data.
- **Inference:**
  - A new panel needs edits in `EditorLayer` (member and call), in `MenuBarPanel` (enum value and menu item), in the `panels.cppm` export list, and possibly in the `EditorLayer` constructor and `Engine`.
  - Pass settings can't be saved, set from the command line, or tested.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** editor panels, headless and editor-off runs, render settings.
- **Proposal:**
  - An `EditorContext` struct and a `Panel` interface (`name`, `draw`, open flag).
  - `EditorLayer` holds a vector of panels and builds the View menu from it.
  - Passes expose plain settings structs, which the renderer panel draws.
  - Size S.

### A-APP-12: The `aegis.engine` facade re-exports the whole engine
- **Where:** `engine.cppm`, 20 `export import` lines.
- **Observed:** examples use only `Engine`, `SceneDescription`, scene/registry/components, scripting, the asset components, `core::ASSETS_DIR` and `rhi::Format` (for `loadTexture`). `aegis-test` doesn't import `aegis.engine` at all.
- **Inference:**
  - Nothing separates the app API from internals such as `rhi::Device`, the passes and the editor.
  - Any interface change anywhere rebuilds every example.
  - Examples can bypass the services the proposal introduces.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** API boundary, example build times.
- **Proposal:** `aegis.app` re-exports only scene, components, asset handles, math and the app types. Tools import anything else explicitly. Size S.

### A-APP-13: Dead and misleading code in scripting and core
- **Where:**
  - `scripting/movement/dynamic_movement_controller.cppm` and `world_border.cppm` (neither exports its class)
  - `scripting/physics/motion_dynamics.cppm`
  - `core/profiler.cppm` `AGX_PROFILE_SCOPE`
  - `core/globals.cppm` `INVALID_HANDLE`, `SHADER_DIR`, `TARGET_FPS`
  - commented-out `Children` iterators and stale "Use Entity::setParent" comments in `components.cppm`
- **Observed:**
  - `DynamicMovementController::update` calls `get<physics::MotionDynamics>()`, but `MotionDynamics` is a script, not a component, so the call can only assert. These three modules were `ai/`'s movement layer and have no other user.
  - `AGX_PROFILE_SCOPE` pastes `profiler##__LINE__` directly, which produces the identifier `profiler__LINE__`; two uses in one scope would collide. The macro is unused.
- **Inference:** `scripting/` advertises three features that don't work.
- **Severity:** polish
- **Confidence:** high
- **Affects:** readability; less code to port in A-APP-08.
- **Proposal:** delete them together with `ai/`. Size S.

### A-APP-14: Two parallel extension hierarchies with mismatched lifecycles (Layer and System)
- **Where:** `core/layer.cppm`, `core/layer_stack.cppm`, `scene/system.cppm`, `ui/ui.cppm`.
- **Observed:**
  - Layers live as long as the engine; systems live as long as the scene, and `reset` doesn't remove them (EA-5).
  - `LayerStack` has exactly one user, `EditorLayer`. `UI` exists only to create the ImGui context and iterate the layers.
- **Inference:** the split between engine lifetime and scene lifetime is right. But engine-lifetime systems such as audio, a physics world or a hot-reload watcher fit neither mechanism: they are not UI, and they must not die with the scene.
- **Severity:** polish
- **Confidence:** medium (judgment)
- **Affects:** new engine-wide systems, the editor.
- **Proposal:**
  - One `System` type with a phase. Both the World (scene lifetime) and the App (engine lifetime) hold a list of systems.
  - The editor becomes one app-lifetime object with a `drawUi` hook.
  - `Layer`, `LayerStack` and `UI` fold into Presentation.
  - Delete the dead `onAttach`/`onDetach`.
  - Size S.

## 2. Extensibility traces

### A. A new scene component visible to scripting, the editor and the renderer (e.g. `SpotLight`)

**Edits today:**
- `scene/components.cppm`: add the struct, and a `ComponentTraits` specialization if it is required. This modifies an interface that about 19 files import, so it triggers a large rebuild.
- Scripting: nothing. `get<T>()` is a template, so this part is additive and free.
- Editor: two modifying edits in `ScenePanel` (an inspector lambda and an Add menu item), plus optionally `StatisticsPanel`.
- Renderer: `lighting_pass.cpp` (view and uniform packing), the mirrored shader struct, and the billboard in `post_effects.cpp`. These passes read the ECS directly (EA-6).

**Testable:** add/has/remove only. The light packing is inline in the pass and could be extracted into a pure function.

**Central files that force edits:** `components.cppm`, `scene_panel.cppm`, `lighting_pass.cpp`.

**Verdict:** costly on the editor side; acceptable on the renderer side, because a new light type legitimately touches lighting.

**Smallest change that makes it additive:** the component table (A-APP-09). Components can then live in the feature's own module plus one registration call.

### B. A new editor panel (e.g. Asset Browser)

**Edits today:**
- A new partition file (additive).
- One line each in the `panels.cppm` export list and `editor/CMakeLists.txt`.
- `MenuBarPanel`: an enum value and a menu item (modifying).
- `EditorLayer`: a member and a draw call (modifying).
- If the panel needs a service the five references don't cover (assets, window), the `EditorLayer` constructor and `Engine`'s `push` call change too.

**Testable:** nothing (ImGui).

**Central files that force edits:** `editor_layer.cppm`, `menu_bar_panel.cppm`, and possibly `engine.cppm`.

**Verdict:** acceptable but touches four or five files.

**Smallest change that makes it additive:** the `Panel` interface plus `EditorContext` (A-APP-11). A panel is then one file, one CMake line and one `addPanel` line.

### C. A new engine system in the frame loop (physics, audio, animation)

**Option 1:** a `scene::System` added through `scene.addSystem` inside every scene description.
- It runs after Camera and Transform, so it lags a frame.
- It isn't engine-wide.
- It survives `reset`.

**Option 2:** an `Engine` member plus a call in `run`.
- It modifies the inline `engine.cppm`, so every example rebuilds.
- It must be slotted into the member destruction order.
- It reaches devices through statics.

**Option 3:** a `Layer`. That is the wrong concept.

**Testable:** the system's `onUpdate(Registry&, float)` can be unit-tested with a bare `Registry`. That is a strength of the current design.

**Verdict:** costly for an engine-wide system; acceptable for a scene-local one.

**Smallest change that makes it additive:** a phase on `System` plus an app-lifetime system list run by the frame loop (A-APP-07 and A-APP-14).

## 3. Planned extensions

### Editor growth (panels, inspectors, serialization, reflection, undo)

**What exists:** hierarchy, entity list, scene settings, properties and Add Component panels, the gizmo, and the renderer, statistics and profiler panels. Docking is enabled, but `IniFilename = nullptr` means the layout is never saved. There is no serialization, no reflection, no undo, and no notion of a command. Edits are direct component writes from UI code in the middle of the render.

**Where the design resists:**
- edits don't show (01) and deletes are unsafe (04)
- the gizmo works in the wrong space (05) and input is not arbitrated (06)
- edits happen mid-render (07)
- there is no component table (09) and panels are coupled to render types (11)
- the scene slots are entity IDs outside the ECS
- scripts are not components (08)
- entity IDs are raw entt IDs, not stable across save/load
- asset identity is partial: `core::Asset::path` is set for textures, OBJ meshes and the IBL cube, but glTF meshes have none, and glTF materials store the material *name* in `path`

**Changes needed:**
- **NOW:**
  - Fix 02, 03 and 05 and add `tests/scene`. These are bugs that every editor feature sits on.
  - Turn the scene slots into components (04).
  - When `core::Asset` is deleted (EA-7), move its source identity (path plus sub-index) into `aegis.assets` rather than dropping it. Recovering it later means touching every loader again.
  - Give editor edits a safe point, phase 2 of A-APP-07.
- **WITH THE FEATURE:**
  - the component table (09) with save/load fields
  - a stable entity ID component
  - commands for undo
  - the `Panel` list (11)
  - ImGui ini persistence
  - pass settings as data
- **NOT NEEDED:** a general reflection system or entt::meta.

### Job-based frame work (app side)

**What exists:** none of it.
- Everything is single-threaded.
- The logger is `NO_THREAD`, and the `Profiler` map is unsynchronized.
- Systems get the whole `Registry&`.
- Passes read the live ECS while recording and also write to it (`camera.aspect`).
- The editor mutates the scene mid-render.
- Statics: `Engine::instance` and `Profiler::instance`.

**Where the design resists:** with no phase boundaries and no extract step, simulating frame N+1 can't overlap with rendering frame N.

**Changes needed:**
- **NOW:** the phase enum and the move of render-time ECS writes into post-update (07). This is the contract parallelism will rest on, and it is cheap while there are few systems.
- **WITH THE FEATURE:**
  - SceneView extract (EA step 5)
  - systems declaring the components they read and write
  - a job system
  - a thread-safe profiler and logger
- **NOT NEEDED:** an automatic dependency-inferring scheduler, or per-component locks.

### Shader and asset hot reload (app side)

**What exists:** on the app side, nothing. There is no watcher, assets load once, the static buffers are frozen (01), and `AssetManager` is dead code (EA-7).

**Who triggers a reload:** a watcher service polled in the input phase, or an editor menu action.

**Where it is applied:** at the phase 2 safe point, between frames. The GPU side relies on the deletion queue and `FrameGraph::compile`.

**Where the design resists:**
- In-place replacements don't propagate (01).
- No path → asset map exists, so a reload can't find the asset's users.
- The static asset helpers return plain values or entities.

**Changes needed:**
- **NOW:** nothing beyond the phases and keeping the asset source identity (both already listed above).
- **WITH THE FEATURE:**
  - the watcher
  - an `AssetLibrary` cache of path → `weak_ptr` (what `AssetManager` was meant to be)
  - in-place reload plus the static dirty counter
- **NOT NEEDED:** a GUID asset database.

## 4. docs/engine-architecture.md

### Problems section
Agree; everything is still true. Gaps:
- It misses 01–07 here: the static/dynamic rule, the hierarchy math and re-parenting bugs, the gizmo space, input capture, and frame order.
- EA-6 should add that passes also *write* the scene (`camera.aspect`).

### Services split
- **Platform, Gpu:** agree. Make them plain structs of owned members, each created by a function returning `expected`, with no interfaces.
- **AssetLibrary:** agree. It holds the defaults, the IBL and the loaders as members, and later the path cache.
- **World:** amend. It should be `scene::Scene` grown in `aegis.scene` (registry, systems, `reset`), not a new type in `aegis.app`.
  - If World lives in app, `aegis.editor` can't name it without an import cycle.
  - In `aegis.scene`, World is testable by linking `aegis-scene` alone.
- **Presentation:** amend. It owns the frame graph, the draw batches and the ImGui context and backends, all the ordering rules in one place. The editor is a separate, optional, app-owned object in `aegis.editor` that depends only on lower modules.

### AppDesc and command line
Agree. Additions:
- Move `aegis-test`'s `parseOptions` (`--headless`, `--frames`, `--screenshot`, `--gpu-driven`) into the app and reuse it.
- Add `--scene`, `--no-editor`, and a fixed time step when `--frames` is given.
- Drop `fpsLimit`; the busy-wait is not needed (vsync or present mode covers it).
- Add an asset root to replace `core::ASSETS_DIR` prefixes in scene code.

### `Engine::create` → `expected<unique_ptr>`
Agree. A heap-allocated engine also removes the this-capture hazard from `agent-notes.md`. Use one message-based `app::Error`; don't add a fourth error taxonomy (map M2).

Drop "several engines side by side" as a goal. The ImGui context and GLFW are process-global; the real gain is "constructible in a test".

### Scenes as named functions
Agree. Amendments:
- `SceneContext` holds only `World&` and `AssetLibrary&`.
- Scenes are registered as a `{name, function}` list, selected with `--scene`.
- Transform and Camera systems are engine systems that are always present (post-update). They must not live in the default-content helper, or a scene without the helper has no transforms.
- Split `createDefaultScene` into small opt-in helpers (`addDefaultCamera`, `addDefaultLights`) that return their entities.

### World owns scripts
Replace with scripts as a component (A-APP-08). Then `reset()` is `registry.clear()` plus clearing systems.

The doc also misses that a reset must re-arm the render side: reconnect the draw batches and call `sceneInitialized` again. This is the A-APP-01 counter.

### SceneView
Agree with the direction and with doing it last, justified by render tests without a scene and by job overlap. It should carry the static change counter.

Before it: remove render writes to the ECS, as part of the phases work.

### Commit order
Amend. Each step is one PR with several commits; per AGENTS.md, mechanical and logic changes go in separate commits.
- **0. (new)** Hierarchy and gizmo fixes plus `tests/scene` (02, 03, 05, link visibility). Small and independent, and creates the test target.
- **1.** AppDesc, command line and `expected`; examples become `device` CTests; `aegis-test` shares the command-line code.
- **2.** Remove the globals: `engine.cpp`, World in `aegis.scene`, AssetLibrary with source identity kept, a narrow re-export (12).
- **3.** World lifecycle: scripts as a component, a real `reset`, slots replaced by components, opt-in default helpers, named scenes.
- **4.** Frame phases, `Clock`, `InputState` with capture, and the editor safe point (06, 07).
- **5.** ImGui owned by Presentation; the editor is optional with a `Panel` list (11, 14).
- **6.** SceneView.

### Open decisions
1. **Name:** `aegis.app`. "Engine" already names the whole library (`aegis-engine`, `Aegis::Engine`).
2. **Code style:** convert `scene` and `scripting` where the rewrite touches them anyway. Use `expected` for fallible operations; keep asserts for programmer preconditions such as `get<T>`. `core` mostly disappears.
3. **Scene definitions:** plain functions; agree.

### Over-engineered or missing
- **Over-engineered:**
  - five service classes, where plain structs would do
  - multi-instance as a goal
  - `fpsLimit`
- **Missing:**
  - frame phases and the clock
  - input capture
  - the static/dynamic rule
  - how `aegis-test` reuses the app: keep its `verify*` device checks on the Gpu struct, and run its `--scene` path through `app::Engine::create` with a test scene function, so the device CTest covers the real app path
  - where the editor lives
  - what replaces the static asset helpers and `ASSETS_DIR` (AssetLibrary members, resolved against the asset root)

### Recommended outline
```
aegis.scene  World (= Scene++): registry, systems by phase, reset(), static change counter;
             components + ComponentInfo table; Scripts component + ScriptSystem
aegis.app    AppDesc (+ parseArgs shared with aegis-test)
             Engine::create(desc) -> expected<unique_ptr<Engine>, Error>
               Platform{window, input}?  Gpu{context, device, renderer}  AssetLibrary
               World  Presentation{frameGraph, drawBatches, imgui}  Editor? (aegis.editor, optional)
             frame(): input -> preUpdate(commands, reloads) -> update -> postUpdate -> extract
                      -> render -> present; Clock (wall, clamped | fixed)
             run(argc, argv, desc, scenes{name, fn(SceneContext&)->expected}) -> exit code
```

## 5. src/aegis/ai/: delete

**What it is:** 33 files, 1,861 lines, not built since the move to modules. It is gameplay AI: utility-AI options, considerations, a decision tree, steering behaviours (seek, flee, arrive, flocking, ...) and two example scenes.

**Why it can't be ported:**
- Every project include points to a header that no longer exists: `scene/scene.h`, `scene/entity.h`, `physics/motion_dynamics.h`, `utils/random.h`, `utils/math_utils.h`, `core/input.h`.
- It uses PascalCase `Aegis::AI` namespaces.
- It uses the removed callback input API (`Aegis::Input::instance().bind(...)`).
- It subclasses the old `Scene` with `createEntity`/`addComponent`.
- It treats scripts as components (`addComponent<TestAIComponent>`) and depends on `MotionDynamics` as a script.

A port would be a rewrite against a scripting API that A-APP-08 is about to change. It isn't engine foundation, and none of the stated goals needs it.

**Recommendation:**
- Delete it, together with `DynamicMovementController`, `WorldBorder` and `MotionDynamics`; git history keeps them.
- Remove the `ai/` exception from AGENTS.md.
- If AI is wanted later, rewrite the steering math as a pure, tested module on top of the new scripting API.

## 6. What works well and should stay
- **`Registry::create`** enforces the required-component set, and `remove` is compile-time restricted to optional components (`IsOptionalComponent`).
- **`System::onUpdate(Registry&, float)`** takes everything as parameters, with no globals, so systems are unit-testable as they are. New systems should keep this shape.
- **`Entity` is a plain ID value**, and scene code passes the `Registry` explicitly.
- **The asset helpers already delegate** to the new stack's `create → expected`. Only the error handling at the top is wrong.
- **The examples are short content code** at the right granularity. They port almost unchanged to scene functions.
- **`aegis-test` already models the target shape:** `Options`/`parseOptions`, `create() → expected`, an exit code, and a fixed time step. Reuse it rather than reinventing it.
- **`ScenePanel`'s `drawComponent`** with its required/optional overloads is a good seed for the component table.
- **The engine-lifetime versus scene-lifetime split** (layers versus systems) is the right idea; it only needs one mechanism.
