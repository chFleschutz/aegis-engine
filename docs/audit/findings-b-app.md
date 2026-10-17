# Findings, lens b (bugs): application layer

Scope: `src/aegis/engine.cppm`, `scene_defaults.cppm`, `scene_description.cppm`, `core/`, `scene/`, `scripting/`, `editor/`, `ui/`. Consumers checked: `examples/*/main.cpp`, `src/aegis/test/main.cpp`. This was a read-only review: nothing was built or run. Problems already described in `docs/engine-architecture.md` appear here only where a concrete failure scenario is added.

## Findings

### B-APP-01: Static entities never get a new GlobalTransform, so moves by scripts and the editor are invisible
- **Where:**
  - `scene/systems/transform_system.cppm` `TransformSystem::onUpdate`
  - `examples/simple-scene/main.cpp` `Rotator`, `SimpleScene::initialize`
  - `editor/editor_layer.cppm` `drawGizmo`, and the Transform inspector in `editor/panels/scene_panel.cppm`
  - `render_graph/passes/gpu_driven.cpp` `InstanceUpdatePass::sceneInitialized`
- **Observed:**
  - `onUpdate` only visits `view<Transform, GlobalTransform, Parent, DynamicTag>()`. `onBegin` computes every global transform once, and every renderer reads only `GlobalTransform`.
  - Simple-Scene calls `scripts.addScript<Rotator>(teapot)` on a teapot that has no `DynamicTag`.
  - The OBJ loader puts `Mesh` and `Material` on the returned root (`obj_loader.cpp` l.66-68).
  - The GPU-driven path also writes static instances only once per scene.
- **Inference:**
  1. In Simple-Scene, `Rotator` changes `Transform::rotation` every frame, but `GlobalTransform` keeps its `onBegin` value. The teapot does not rotate on screen, on either render path.
  2. In the editor, dragging the gizmo or the Location/Rotation/Scale fields of any entity without `DynamicTag` changes `Transform` but nothing on screen. This covers every loaded model, every point light and the directional light. The gizmo follows `Transform`, so it drifts away from the mesh. Rotating the directional light has no effect on lighting, because `lighting_pass.cpp` reads `GlobalTransform::forward()`.
  3. `DynamicTag` does not pass down the hierarchy. A glTF model's meshes sit on child entities (`gltf_loader.cpp` `buildScene`), so tagging and rotating the model root does not move the meshes.
  4. The rule "static means immovable" exists only as a comment on `DynamicTag`. Nothing enforces it or reports a violation.
- **Severity:** bug
- **Confidence:** high (from reading the code; confirm the Simple-Scene symptom by running it)
- **Affects:** every script that moves an entity, editor transform editing, any future animation or physics. `TransformSystem` can be unit-tested without a GPU.
- **Verify by:**
  - Run Simple-Scene and watch the teapot (expected: it does not rotate).
  - Select "Directional Light" and drag its Rotation (expected: the lighting does not change).
  - Unit test against `aegis.scene`: call `onBegin`, change `Transform`, call `onUpdate`, then `CHECK` that the global transform changed. It fails today.
- **Fix sketch:** Track changes with a dirty mark, set through a `Registry` transform setter or entt `patch`/`on_update`. Recompute changed subtrees and let the render side re-upload changed static instances. Alternatively, have the editor and scripts tag the subtree as dynamic, and assert in debug when a static `Transform` changes after `begin`.

### B-APP-02: Parent rotation and scale do not move children, because hierarchy composition is componentwise
- **Where:** `scene/systems/transform_system.cppm` `TransformSystem::onBegin` (l.39-41) and `onUpdate` (l.61-63)
- **Observed:** `globalTransform.location = globalTransform.location + localTransform.location;` with `scale = scale * local.scale` and `rotation = rotation * local.rotation`.
- **Inference:**
  - A child at local (1,0,0) under a parent rotated 90 degrees about Z ends up at world (1,0,0) instead of (0,1,0).
  - Under a parent scaled by 2, it ends up at (1,0,0) instead of (2,0,0).
  - Any glTF with translated child nodes under a rotated or scaled node renders with its parts in the wrong place. Scaling or rotating a loaded model's root in scene code does not scale or rotate the offsets of its sub-nodes.
  - The example assets avoid this by luck. The only parent with rotation or scale (`MetalRoughSpheres` node 0: matrix with scale 0.4 and -90 degrees about X) has children with zero translation. `Sponza` and `DamagedHelmet` carry their transform on the mesh node itself.
  - TRS cannot represent a non-uniform parent scale combined with a child rotation at all.
- **Severity:** bug
- **Confidence:** high
- **Affects:** loading arbitrary glTF files, any parented object (attachments, lights on objects), editor hierarchy edits.
- **Verify by:** Unit test against `aegis.scene`: parent rotated 90 degrees about Z, child at (1,0,0), call `onBegin`, then `CHECK(global.location == {0,1,0})`.
- **Fix sketch:** Compose matrices (`world = parentWorld * local.matrix()`) and store a world matrix in `GlobalTransform`. The minimum fix is `location = parent.location + parent.rotation * (parent.scale * local.location)`.

### B-APP-03: Registry hierarchy operations corrupt the linked lists (reparenting, removeChild, cycles)
- **Where:** `scene/registry.cppm` `Registry::addChild`, `setParent`, `removeChild`
- **Observed:**
  - `addChild` overwrites `Parent.entity` and appends the child to the new parent's list without unlinking it from the old parent.
  - It sets the child's `Siblings.prev` only when the new parent already has children, and never clears `Siblings.next`.
  - `removeChild` relinks the neighbours and resets `Parent`, but leaves the removed child's own `Siblings{next, prev}` intact. `removeChildren` does reset them.
  - Nothing checks that the new parent is not a descendant of the child.
- **Inference:**
  - (a) After `setParent(c, A); setParent(c, B)`, A's `Children` still counts and lists `c`, and walking `c.next` leads into B's list. Destroying A (`removeChildren`) then resets the `Parent` of `c` and of B's later children, silently detaching them from B.
  - (b) A has children `c1, c2`. After `removeParent(c1); setParent(c1, B)` with B empty, `B.first == B.last == c1` but `c1.next == c2`. Walking B's children (the editor Hierarchy panel, `removeChildren(B)`) visits `c2`, which belongs to A, and `removeChildren(B)` clears `c2`'s parent.
  - (c) `setParent(A, B)` while B is a descendant of A creates a cycle. The parent walk in `TransformSystem::onBegin` (`while (current)`) never ends, so `loadScene` hangs, and the Hierarchy panel's sibling walk loops forever.
  - Today only `Registry::destroy` and the glTF loader call these functions, so no example triggers this. Any reparenting feature (editor drag and drop, attaching objects at runtime) will.
- **Severity:** bug
- **Confidence:** high
- **Affects:** editor reparenting, scene composition, runtime attach and detach. Pure logic, directly unit-testable.
- **Verify by:** Unit tests against `aegis.scene.registry` for (a), (b) and (c), checking `Children::count`, the sibling walk and `Parent`.
- **Fix sketch:** In `addChild`, call `removeParent(child)` first, then append with `next = {}`. In `removeChild`, reset the removed child's `Siblings`. Reject a parent that is the entity itself or one of its descendants.

### B-APP-04: The editor gizmo treats the local Transform as a world matrix
- **Where:** `editor/editor_layer.cppm` `EditorLayer::drawGizmo`
- **Observed:** `glm::mat4 transformMatrix = transform.matrix();` takes the selected entity's local `Transform`. It is passed to `ImGuizmo::Manipulate` with the world-space view and projection, and the result is decomposed straight back into the local `Transform`.
- **Inference:**
  - Every glTF node and submesh is a child of the loader's root. For such an entity, the gizmo is drawn at the local offset instead of at the object. Example: in Helmets the DamagedHelmet root is at (-2,0,2), and selecting its mesh node puts the gizmo near the world origin.
  - Dragging writes world-space values into the local transform, so once global transforms are recomputed the child jumps by its parent's transform.
- **Severity:** bug
- **Confidence:** high
- **Affects:** editing the transform of loaded models in the editor, and any hierarchy editing.
- **Verify by:** Run Helmets, open the Scene panel, select a child of a model, press W, and compare the gizmo position with the mesh position.
- **Fix sketch:** Give ImGuizmo the world matrix (parent world times local). Convert the result back with the inverse of the parent's world matrix before writing `Transform`.

### B-APP-05: Destroying or editing entities in the editor leaves scripts, Scene slots and the gizmo dangling
- **Where:**
  - `editor/panels/scene_panel.cppm` `drawEntityActions` ("Destroy Selected Entity") and `drawComponent` (the close button removes optional components)
  - `scene/registry.cppm` `Registry::destroy` (`// TODO: Destroy all scripts attached to the entity`)
  - `scripting/script_manager.cppm` `ScriptManager` (no way to remove a script)
  - `scene/scene.cppm` slots `m_mainCamera`/`m_ambientLight`/`m_directionalLight`/`m_skybox`
  - `editor/editor_layer.cppm` `drawGizmo`: `registry.get<Camera>(scene.mainCamera())`
- **Observed:**
  - Any entity can be destroyed from the context menu, including the ones held in Scene slots.
  - The Camera component can be removed from Main Camera with its header's close button.
  - Scripts keep their `Entity` and call `m_registry->get<T>(m_entity)`, which is checked only by `AGX_ASSERT_X`.
  - Render passes guard the slots with `has<>` checks; the editor and scripts do not.
- **Inference:**
  - Scenario 1: in any example, select "Main Camera" under Scene Settings, then right-click the Hierarchy and choose "Destroy Selected Entity". On the next frame, `KinematicMovementController::applyRotation` calls `get<Transform>()` on the destroyed entity. Debug builds assert; release builds read entt storage with a stale id, which is undefined behaviour.
  - Scenario 2: destroying the Simple-Scene teapot (which has `Rotator`) or the Helmets plane (which has `ColorChanger`) gives the same result.
  - Scenario 3: with a gizmo mode active (W/E/R) and any entity selected, removing Camera from Main Camera makes `drawGizmo`'s `get<Camera>` fail the same way.
  - What this adds to `docs/engine-architecture.md` section 5 ("Leaked scripts"): the leak there is a memory problem, but the missing script/entity link is a crash path that one editor click reaches.
- **Severity:** bug
- **Confidence:** high
- **Affects:** the editor, every script, and future features that destroy entities at runtime (gameplay, scene switching). A unit test can cover `ScriptManager` plus `Registry::destroy` with a stub `Input`, or after extracting the script/entity bookkeeping.
- **Verify by:** Run Simple-Scene and destroy "Main Camera" or the teapot as described. In a debug build, expect an assert from `Registry::get`.
- **Fix sketch:** Store scripts per entity (a component holding `unique_ptr<ScriptBase>`, or a map cleaned up from an `onDestroy` hook). In the editor and `Scene`, either forbid destroying slot entities or clear the slot on destroy, and guard `drawGizmo` with `has<Camera>`.

### B-APP-06: Structural edits after loadScene break the GPU-driven static instance set
- **Where:**
  - `render_graph/passes/gpu_driven.cpp` `InstanceUpdatePass::sceneInitialized` (called only from `Engine::loadScene`)
  - `render_graph/draw_batch_registry.cpp` `onMaterialDestroy`/`updateOffsets`
  - Editor destroy and add/remove component, and static `Engine::loadModel` called after `loadScene`
  - `assets/static_mesh.cpp` `~StaticMesh` (calls `m_device.free(buffer)`)
- **Observed:**
  - Static instances, including each mesh's buffer device address (`mesh.meshDataAddress()`) and its material address, are written once per scene.
  - `DrawBatchRegistry` updates its counts and `firstInstance` offsets on every Material construct or destroy.
  - Freeing a buffer is deferred only until the next submit completes (`Device::retireValue`).
- **Inference:**
  - On the GPU-driven path (the default where mesh shaders exist), destroying a static mesh entity in the editor releases the last `shared_ptr<StaticMesh>`, for example the unique teapot or plane mesh in Simple-Scene. One frame later the buffer is freed, but the static instance buffer still holds its device address. Culling and the mesh shader then read freed memory, which can cause a GPU fault or garbage.
  - Separately, the batch counts and offsets move while the static buffer layout stays the same, so the static batch ranges stop matching the buffer.
  - A model loaded with `Engine::loadModel` after `loadScene` is drawn on the CPU path and missing on the GPU-driven path.
  - The app layer offers these operations at any time, while the render side assumes a frozen static set.
- **Severity:** bug
- **Confidence:** medium (the render internals were read only as far as needed; the render lens should confirm)
- **Affects:** the editor, runtime spawning and despawning, scene streaming.
- **Verify by:** In Simple-Scene on the GPU-driven path in a debug build, destroy the floor plane or the teapot and watch for validation or GPU-AV errors. Compare with `ENABLE_GPU_DRIVEN_RENDERING=false`.
- **Fix sketch:** Have the render side notice structural changes (`DrawBatchRegistry` already gets the signals) and rebuild the static set on the next frame. Until then, have the editor refuse to destroy static renderables.

### B-APP-07: Every editor tree node gets ImGui ID 1 (Entity converts to bool)
- **Where:** `editor/panels/scene_panel.cppm` `drawSingleEntity` (l.249) and `drawEntityTreeNode` (l.264): `ImGui::TreeNodeEx((void*)(uint64_t)(uint32_t)entity, ...)`
- **Observed:**
  - `scene::Entity` has a non-explicit `operator bool()` and no conversion to an integer or to `entt::entity` (`entity.cppm`).
  - So `(uint32_t)entity` goes through `operator bool` and yields 1 for every valid entity.
  - ImGui 1.92.4 is vendored, with `ConfigDebugHighlightIdConflicts = true` by default.
- **Inference:**
  - All sibling nodes in Hierarchy, All Entities and Scene Settings share one ID, and therefore one open/closed state. Opening one root model opens every root node.
  - Hovering a node raises ImGui's "conflicting ID" debug highlight.
  - Any per-entity ImGui state (rename-in-place, drag and drop) added later will collide.
- **Severity:** bug
- **Confidence:** high
- **Affects:** every editor feature keyed by entity.
- **Verify by:** Run Sponza or Helmets, open the Scene panel, hover a node in "All Entities" (expect the ID-conflict highlight), and expand one model in Hierarchy.
- **Fix sketch:** Use `static_cast<std::uint32_t>(entity.id())` (or `entt::to_integral`). Make `Entity::operator bool` `explicit` so this cannot compile again.

### B-APP-08: Game input ignores ImGui focus; editor and camera keys collide
- **Where:**
  - `platform/input.cpp` `Input::keyPressed`/`mouseButtonPressed` (poll GLFW directly)
  - `scripting/movement/kinematic_movement_controller.cppm` (`KeyMappings`: W/A/S/D/Q/E, right button for mouse-look)
  - `editor/editor_layer.cppm` `EditorLayer::onUpdate` (Q/W/E/R)
- **Observed:**
  - Nothing reads `ImGuiIO::WantCaptureKeyboard` or `WantCaptureMouse` (no match in `src/aegis` outside `ai/`).
  - `ScenePanel` opens its context menu with `BeginPopupContextWindow(0, ImGuiPopupFlags_MouseButtonRight)`.
- **Inference:**
  - Typing a name such as "wall" into the "Entity Name" field moves the camera forward and left, and switches the gizmo between translate (W) and hidden (Q is not even needed: E selects rotate).
  - Pressing W to pick the translate gizmo also moves the camera forward. E (rotate) also lifts the camera, and Q (hide gizmo) lowers it.
  - Right-clicking a panel to open "Create Entity" also triggers mouse-look: `toggleMouseRotate` sets `CursorMode::Disabled`, so the cursor is captured and the camera turns while the popup opens.
- **Severity:** bug
- **Confidence:** high
- **Affects:** every editor feature that takes text or mouse input, and every script that reads input.
- **Verify by:** Run Simple-Scene, select an entity, type in its name field, and right-click the Hierarchy panel.
- **Fix sketch:** Have `Engine`/`Input` gate game input on the `WantCapture*` flags (or let the editor layer consume input before scripts), and give gizmo hotkeys a modifier or require the viewport to be hovered.

### B-APP-09: Mouse look and pan are scaled by frame time, so sensitivity depends on the frame rate
- **Where:** `scripting/movement/kinematic_movement_controller.cppm` `applyRotation` (l.92-99) and `applyMovement` (mouse pan, l.156-162)
- **Observed:** The cursor delta for this frame is added to `rotate`, and then `rotate *= deltaSeconds;`. Pan does `transform.location += moveDir * deltaSeconds` with `moveDir` built from the pixel delta.
- **Inference:** A cursor delta is already a per-frame displacement, so multiplying it by dt makes the turn per pixel proportional to frame time. Moving the mouse 100 px turns the camera about 0.42 rad at 60 FPS and about 0.04 rad at 600 FPS. The free camera feels different on every machine and every scene, and becomes sluggish in light scenes.
- **Severity:** bug
- **Confidence:** high
- **Affects:** the default camera in every example; any future input-driven controller. Unit-testable once `Input` can be faked.
- **Verify by:** Code reading. Or compare the turn per mouse sweep in Template-Scene (high FPS) and Sponza (lower FPS).
- **Fix sketch:** Apply mouse deltas without dt (`rotate += delta * sensitivity`) and keep dt only for key-driven rotation and movement.

### B-APP-10: Frame order makes the camera view two frames old and out of step with the camera position and aspect
- **Where:**
  - `engine.cppm` `Engine::run` (scene update, then scripts, then render)
  - `scene_defaults.cppm` `createDefaultScene` (`CameraSystem` registered before `TransformSystem`)
  - `render_graph/passes/scene_update_pass.cpp` `SceneUpdatePass::execute`
  - `render_graph/passes/lighting_pass.cpp` l.99-100
- **Observed:**
  - In frame N, `CameraSystem` builds `viewMatrix` from a `GlobalTransform` that `TransformSystem` computed in frame N-1, from the `Transform` the script wrote in frame N-2.
  - `TransformSystem` then updates `GlobalTransform` from the script's frame N-1 write.
  - `SceneUpdatePass` uploads that older `viewMatrix` together with `.position = GlobalTransform.location`, which is one frame newer, and the lighting pass uses the newer position as `cameraPosition`.
  - `camera.aspect` is written in the render pass and used by `CameraSystem` in the next frame.
  - `aegis-test` registers Transform before Camera (`test/main.cpp` l.783-784), so its device tests do not exercise this order.
- **Inference:**
  - Camera input reaches the screen two frames late (one of them avoidable).
  - While the camera moves, the view and the shading position differ by one frame of motion, which skews specular and view-dependent terms.
  - The first frame and the first frame after every resize use the previous aspect (1.0 on frame 1), so they appear stretched.
- **Severity:** bug
- **Confidence:** medium (the order is certain; the visual impact is small)
- **Affects:** any system or script order a feature depends on, camera features, consistency between `aegis-test` and `Engine`. Unit test with `Scene`, both systems and a fake script.
- **Verify by:** Code reading, plus a unit test: set the camera `Transform`, run `scene.update` once, and `CHECK` the view matrix (it is stale with Camera registered before Transform).
- **Fix sketch:** Fix one frame order in a single place: scripts, then systems (transforms before cameras), then the editor, then render. Set the aspect from the extent before the scene update (for example on resize) instead of inside a render pass.

### B-APP-11: The first frame after a minimize gets the whole blocked time as delta, unclamped
- **Where:** `engine.cppm` `Engine::run` (l.151-165); `core/profiler.cppm` `ScopeProfiler`
- **Observed:** `lastFrameBegin = currentFrameBegin` runs before `isMinimized()`. `waitEvents()` blocks and then `continue`s. The next iteration's `frameTimeSec` therefore includes all the time spent minimized, and nothing clamps it. The "Frame Time" `ScopeProfiler` of the minimized iteration also records the blocked time.
- **Inference:** After being minimized for 10 s, scripts and systems get dt = 10. A camera key held at restore jumps the camera by 10 s of movement, `RotationSystem` and `Rotator` jump, and any future physics step explodes. The profiler's rolling "Frame Time" and FPS show a spike for 50 frames.
- **Severity:** polish (bug once physics or animation arrive)
- **Confidence:** high
- **Affects:** physics, animation, the profiler.
- **Verify by:** Code reading. Or minimize Eval-Scene `DynamicObjects` for a few seconds, restore it, and watch the cubes jump.
- **Fix sketch:** Reset `lastFrameBegin` after `waitEvents()` (or skip timing on minimized iterations) and clamp dt (for example to 0.1 s).

### B-APP-12: Setup failures in release builds are undefined behaviour, not "carry on"
- **Where:**
  - `engine.cppm` `Engine::createRenderer` (`AGX_ASSERT_X(context, ...); m_context = std::move(*context);`, likewise device and renderer)
  - `loadDefaultAssets` (`m_ibl.emplace(std::move(*ibl))`)
  - `check()` returning `T{}`
  - every example's `registry.get<Transform>(Engine::loadModel(...))`
- **Observed:**
  - In release, `AGX_ASSERT_X` compiles to nothing (`core/assert.h`).
  - `createRenderer` never logs `context.error().message` (nor the device or renderer error), unlike `loadDefaultAssets` and `createFrameGraph`.
  - The examples use the entity returned by `loadModel` immediately: Simple-Scene l.41/45, Helmets l.54-64, eval-scene l.280.
- **Inference:**
  - On a machine with no suitable Vulkan device or driver, a release build calls `operator*` on an `expected` that holds an error. That is undefined behaviour, typically a crash with no message. Debug builds stop at an assert that lacks the underlying reason.
  - When a model fails to load (for example, `aegis-assets` not cloned, or a path whose case is wrong on Linux), `check` returns `scene::Entity{}`. The next line, `registry.get<Transform>(Entity{})`, then indexes entt storage with the null id: undefined behaviour in release.
  - What this adds to `docs/engine-architecture.md` section 2: the doc says the engine "carries on", but every example dereferences the result right away, so in practice it crashes without a diagnosis.
- **Severity:** bug
- **Confidence:** high
- **Affects:** running examples as tests (exit codes), Linux bring-up, the proposed `Engine::create() -> expected`.
- **Verify by:** Code reading. Or run a release Simple-Scene with the teapot path misspelled.
- **Fix sketch:** Log the error in `createRenderer`. Short term, `std::abort()` after `ALOG::fatal` in release instead of using `T{}`. Long term, use `std::expected` all the way up, as the doc proposes.

### B-APP-13: AssetManager never clears its singleton, so a second Engine in one process fails
- **Where:** `core/asset_manager.cppm` `AssetManager::AssetManager` / `~AssetManager() = default`
- **Observed:** The constructor asserts `!s_instance` and sets it. The destructor does not reset it. By contrast, `Engine::~Engine` does reset `Engine::s_instance`.
- **Inference:** Constructing an `Engine`, destroying it, and constructing another in the same process (a test fixture, or switching scenes by rebuilding the engine) fails the assert "AssetManager instance already exists!" in debug. In release, `AssetManager::instance()` returns a dangling pointer after the first engine is gone. This blocks the "build a real engine in tests" goal of `docs/engine-architecture.md` even before the singleton is removed.
- **Severity:** bug
- **Confidence:** high
- **Affects:** tests that construct an Engine, and any engine re-creation.
- **Verify by:** Code reading. A test that constructs `Engine` twice in sequence on a GPU machine would hit the assert.
- **Fix sketch:** Reset `s_instance = nullptr` in the destructor. Better, delete `AssetManager`: its entries have no reader (map section 3).

### B-APP-14: Layers are detached after everything they reference is destroyed
- **Where:** `engine.cppm` member order (`m_layerStack` is the second member); `~Engine` body (`ImGui::DestroyContext()`); `core/layer_stack.cppm` `~LayerStack` (calls `onDetach`)
- **Observed:**
  - `m_layerStack` is destroyed after `m_frameGraph`, `m_drawBatches`, `m_scene`, `m_renderer`, `m_device`, `m_input` and `m_window`, and after the ImGui context was destroyed in the `~Engine` body.
  - `EditorLayer` holds references to all of these (`Input&`, `Renderer&`, `FrameGraph&`, `DrawBatchRegistry&`, `Scene&`).
  - `onDetach` is the documented lifecycle hook (`core/layer.cppm`).
- **Inference:** `EditorLayer` does not override `onDetach` today, so nothing crashes yet. The first layer that does what `onDetach` exists for (saving editor layout through ImGui, deselecting in the scene, releasing a GPU preview texture) uses destroyed objects at shutdown. Adding a layer is meant to be the obvious extension point, and this makes it unsafe.
- **Severity:** design problem
- **Confidence:** medium
- **Affects:** new layers and tools, editor persistence.
- **Verify by:** Code reading. Or add an `onDetach` that touches `m_scene` and run with a debug heap.
- **Fix sketch:** Detach and destroy layers explicitly in `~Engine` before the frame graph, scene and ImGui teardown, as part of the "Presentation" service in the doc.

### B-APP-15: The Camera inspector accepts degenerate projections; the aspect assert can never fire
- **Where:** `editor/panels/scene_panel.cppm` Camera `drawComponent` lambda; `scene/systems/camera_system.cppm` `calcPerspectiveProjection`
- **Observed:**
  - FOV is allowed from 0 to 180 degrees, Near from 0 to 100 and Far from 0 to 1000, with each limit independent of the others.
  - The assert is `std::abs(camera.aspect - std::numeric_limits<float>::epsilon()) > 0.0f`. It is true for any aspect other than epsilon, including 0.
- **Inference:**
  - FOV = 0 gives `tan(0) = 0`, so `1/(aspect*0)` is infinite and the projection, frustum and culling become NaN: a black or garbage frame.
  - Near = 0 gives a constant depth of 1, so the depth test fails and the geometry disappears.
  - Far < Near flips the depth.
  - It recovers when the value is dragged back, but it looks like a renderer bug.
- **Severity:** polish
- **Confidence:** high
- **Affects:** camera features, editor validation. `CameraSystem` is unit-testable.
- **Verify by:** Run any example, select Main Camera, and drag FOV to 0 or Near to 0.
- **Fix sketch:** Clamp in the inspector (FOV in [1, 179] degrees, near > 0, far > near) and turn the assert into a real `aspect > 0` precondition.

## Map candidates

1. TransformSystem ignores the parent's rotation and scale: **confirmed**, B-APP-02. The current example assets happen not to show it.
2. GlobalTransform is updated only for DynamicTag entities: **confirmed**, B-APP-01. The Simple-Scene teapot `Rotator` and editor edits on static entities are invisible.
3. `Registry::addChild` doesn't detach from the old parent: **confirmed and extended**, B-APP-03. `removeChild` also leaves stale siblings, and cycles are not rejected.
4. Scripts are never deleted, so destroyed entities' scripts keep running: **confirmed**, B-APP-05. One editor click leads to an assert or undefined behaviour. The leak itself is already known (doc section 5).
5. No ImGui `WantCaptureKeyboard/Mouse` check, and the gizmo keys collide with the camera keys: **confirmed**, B-APP-08. Right-click on a panel also triggers mouse-look.
6. The editor can destroy an entity held in a Scene slot: **confirmed**, merged into B-APP-05. Render passes are guarded with `has<>`; `drawGizmo` and scripts are not.
7. A huge delta time after minimize: **confirmed**, B-APP-11 (polish today).
8. Camera matrices two frames behind scripts: **confirmed**, B-APP-10. The camera position and aspect are also out of step with the view.
9. Editor `decomposeTRS` with euler angles making gizmo edits drift: **rejected**. `Transform::rotation` is a `glm::quat`, so overload resolution picks the quat `decomposeTRS` (`math.cppm` l.223) and no euler round trip happens. The real gizmo bug is local-versus-world space (B-APP-04). Only the inspector's euler fields round-trip (`scene_panel.cppm` l.180-182). `glm::eulerAngles` and `glm::quat(vec3)` are a consistent pair, so the effect is at most a cosmetic jump in the displayed values near ±90 degrees pitch (low).

Extra checks from the brief:
- **Engine destruction order versus GPU resources:** sound for current members. Draw batches disconnect before the scene; the scene, defaults and IBL release before the renderer and device; `Device::free` is deferred. The only problem is the layer stack (B-APP-14).
- **Engine init failure in release:** B-APP-12.
- **`loadScene` twice:** already known (doc section 5). The only extra detail is that the old camera's movement script keeps moving the now-unused first camera. No new defect.
- **entt signal handlers:** `DrawBatchRegistry` stays consistent whichever of `Material`/`DynamicTag` is destroyed first during `Registry::destroy`. Construct/destroy handlers balance the counts.
- **Component references across `emplace`:** eval-scene keeps `auto& cubeMesh = registry.get<Mesh>(cube).staticMesh` across 10,000 `add<Mesh>` calls. This is safe, because entt storage is paged and pointers stay stable on insertion. No reference is held across a `remove`/`destroy` of the same storage.
- **Entity validity:** there is no `valid()` check anywhere, but entt's `all_of` on a stale id returns false safely, and versioned ids are not reused. Only `get` on stale ids is dangerous (B-APP-05).
- **Profiler:** "Frame Time" includes the minimized wait (B-APP-11). The FPS shows infinity on the first frame (0 ms). `AGX_PROFILE_*` is unused and would not even work: `profiler##__LINE__` pastes to the literal token. Polish, not reported.
- **LayerStack:** B-APP-14. Pushing a layer while layers update would invalidate the iteration over `m_Layers`, but nothing does that today.
- **UI and ImGui context lifetime:** created in `UI::UI` and destroyed in `~Engine` before the members. Correct today; the ownership split is a design point for lens a.
- **Frame timing and FPS limit:** the limit is compiled off. The busy-wait measures from the frame start, which is correct.
- **Threading:** the app layer is single-threaded; no issue.

## Notes for lens a

- Hierarchy is an intrusive linked list stored in three required components. It is correct only through `Registry` methods, and its doc comments still point at the removed `Entity::setParent`/`addChild`.
- "Static versus dynamic" is a render-side optimisation that leaks into the ECS contract (`DynamicTag` "only during init") with no enforcement. It is the root of B-APP-01 and B-APP-06.
- The frame order is spread over `Engine::run`, the system registration order in `createDefaultScene`, and render passes that write scene state (`camera.aspect`). `aegis-test` uses another order.
- Editor UI mutates the scene during frame-graph execution (inside `UiPass`). This is safe for the CPU path today, but it ties editor actions to render timing.
- `Input` is a thin GLFW poller with no consume or capture layer, so the editor and scripts cannot arbitrate.
- `ScriptBase` gets its dependencies through `init()` after construction (two-phase init). Scripts cannot add components (`add` is commented out) and have no access to their `ScriptManager`.
- `Entity` has an implicit `operator bool`. This invites accidental integer conversions (B-APP-07); it should be `explicit`.
- `Registry`, `TransformSystem`, `CameraSystem`, `ScriptManager` (with a fake `Input`) and `LayerStack` are all GPU-free and have no tests. B-APP-01, -02, -03, -09, -10 and -15 would each be one doctest case.
- Examples assign `registry.get<Material>(e).instance = ...` directly, which bypasses the entt signals that `DrawBatchRegistry` relies on. The render map's stale-batch issue starts from this app-side habit.
- `check()`, `AGX_ASSERT` and `std::expected` coexist in `Engine`, and `createRenderer` drops the error messages entirely.
