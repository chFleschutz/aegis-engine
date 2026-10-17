# Findings, lens d: consistency
Format per finding: Where / Observed / Inference / Severity / Confidence / Affects / Proposal. The IDs below are not contiguous. Items already listed in map.md were re-verified.

## A. Duplicated code

### D-01: G-buffer declared and attached three times
- **Where:** render_graph/passes/geometry_pass.cpp (GeometryPass ctor, info, createResources, execute); passes/gpu_driven.cpp (GpuDrivenGeometryPass, same four); passes/lighting_pass.cpp (re-declares the five names as references).
- **Observed:** Both geometry ctors list the same addTexture("Position"/"Normal"/"Albedo"/"ARM"/"Emissive"), "Depth" and "CameraData". Both build the same writes list and the same ColorAttachment array from GeometryPass::gBufferFormats. Both execute bodies contain an identical beginRendering with depth clear 1.0, setViewport and setScissor.
- **Inference:** A new G-buffer target needs edits in three passes, and a miss only shows up at graph compile or in validation. A third geometry path would copy it again.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new G-buffer attachment or geometry path.
- **Proposal:** One `GBuffer` struct in render_graph (name and format table, handles, writes(), pipelineAttachments(), begin(cmd, pool, extent)) used by all three passes. Size M, logic change (own PR). The table is testable without a GPU.

### D-02: Image-view descriptor writing exists three times in Device; ImageView::create twice
- **Where:** rhi/device.cpp Device::createImageView(ImageHandle), createImageView(vk::Image), replace(ImageViewHandle, ...); rhi/image_view.cpp both ImageView::create overloads.
- **Observed:** The two createImageView bodies are identical except for the first call. Both contain "if Sampled write slot, if Storage write slot, else InitializationFailed". replace repeats the block with a reuse-slot argument. The two ImageView::create overloads differ only in the default view type source and nullopt vs the image handle.
- **Inference:** A new descriptor kind means three edits. In replace, if the old view had a slot and the new desc.usage drops that bit, the old slot is neither reused nor freed. The old view goes to the deletion queue, and only free(ImageViewHandle) returns heap slots. This is inferred from reading, not run.
- **Severity:** design problem (possible slot leak: bug, low confidence)
- **Confidence:** high for the duplication, low for the leak
- **Affects:** bindless changes, swapchain views, resize.
- **Proposal:** A private Device::writeDescriptors(ImageView&, desc, const ImageView* old) and one ImageView::create core. Size S, refactor (own PR), then fix the leak with a test of the helper.

### D-03: TextureDescriptor (image index + sampler index) redefined three times
- **Where:** render_graph/ibl.cpp:26; passes/lighting_pass.cpp:22; passes/post_effects.cpp:106.
- **Observed:** The ibl.cpp comment says "Mirrors bindless::TextureDescriptor". There are three C++ copies with two construction recipes.
- **Inference:** The CPU/GPU contract is restated three times and nothing checks it. The name also clashes with the AGENTS.md rule that a Descriptor is one slot index.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every pass or material passing a texture in push constants.
- **Proposal:** One exported `SampledTexture{image, sampler}` with assets::Texture::descriptor(), and delete the three copies. Size S, mechanical (own PR).

### D-04: Three binary/SPIR-V readers; the shared one is unused
- **Where:** render_graph/shader.cpp loadShader; test/main.cpp loadSPIRV (line 49); utils/file.cppm file::readBinary.
- **Observed:** loadShader and loadSPIRV are near-identical ifstream/ate/tellg/read code. loadShader also rejects size 0 and the test copy does not. readBinary has no callers.
- **Inference:** The test copy has already drifted from production.
- **Severity:** polish
- **Confidence:** high
- **Affects:** shader loading tests.
- **Proposal:** Keep loadShader and split a pure parseSpirv(bytes) for unit tests. Make aegis-test call loadShader and delete utils::file. Size S, logic.

### D-05: aegis-test builds its own engine; two classes named aegis::Engine
- **Where:** test/main.cpp class Engine (line 124, namespace aegis) and Engine::create; engine.cppm Engine::createRenderer, createFrameGraph, run.
- **Observed:** Both do Context::create, createDevice({}) and Renderer::create, hold unique_ptr members, then run flushUploads and renderFrame. main.cpp is 1490 lines. It mixes a SPIR-V loader, an option parser, device setup, 15+ verify* checks, scene creation, the frame loop and a second FrameTimer type.
- **Inference:** ctest never exercises the real engine startup. A change to creation order needs two edits. The two same-named classes hurt navigation. They are in different TUs, so there is no ODR problem today.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** a headless engine-startup test.
- **Proposal:** A shared `Gpu` bundle (context, device, renderer, one create(Desc)) used by both, or a headless Engine mode. Split main.cpp into checks/*.cpp plus a small main. Size L. The split is mechanical (own PR); the bundle is logic.

### D-06: Normal matrix computed by hand twice; math::normalMatrix is unused
- **Where:** passes/gpu_driven.cpp makeInstance (line 52); passes/static_mesh_render_system.cpp render (line 56); math/math.cppm normalMatrix (vec3 and quat overloads).
- **Observed:** Both passes use `glm::inverse(glm::mat3{ model })` and copy the columns into normalRow0/1/2 next to `glm::mat3x4{ glm::rowMajor4(model) }`. math::normalMatrix has no callers and returns R*S^-1, a different convention.
- **Inference:** There are two conventions for "normal matrix". The unused one would be transposed relative to the shader. The packing is copied, so a layout change needs both edits.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** instance layout changes and a CPU-only packing test.
- **Proposal:** `math::normalRows(mat4)` (or packTransform) used by both, delete math::normalMatrix, and test with a non-uniform scale. Size S, logic.

### D-07: Four Error types
- **Where:** rhi/error.cppm (code only); assets/error.cppm (code, message, optional rhi); render_graph/error.cppm (message, optional rhi, no code); the renderer enum.
- **Observed:** assets and render_graph each define makeError(const rhi::Error&, string) with the same wrapping. Engine::check<T,E> needs E::message, which rhi::Error lacks.
- **Inference:** Each module boundary converts errors by hand. render_graph errors cannot be switched on, so tests can only match strings.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** error handling in any new subsystem.
- **Proposal:** Keep rhi::Error. Give assets and render_graph the same shape (code, message, optional rhi) and add no global error module. Size M, logic.

### D-08: Resource-name string literals repeated across passes
- **Where:** render_graph/passes/*.cpp, 47 addTexture/addBuffer/addReference("literal") sites. "CameraData" appears in 4 files, "SceneColor" in 3, "Depth" in 3, "Final" in 2, the G-buffer names in 3, and the instance/visibility/command/count buffers in 2.
- **Observed:** A mistyped name compiles and only fails in resolveReferences (see M12).
- **Inference:** Typos become runtime errors, and a new pass must read other passes to learn the strings.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** adding passes and a default-graph compile test.
- **Proposal:** constexpr string_view constants in one partition (render_graph::resource). Size S, mechanical. Add no typed handles between passes yet.

### D-09: Barrier boilerplate in CommandBuffer
- **Where:** rhi/command_buffer.cpp: both transitionImageLayout overloads (~285-330), generateMipmaps, makeTransferWritesVisible, memoryBarrier (six pipelineBarrier2 calls).
- **Observed:** The two transition overloads differ only in how image and range are obtained, and generateMipmaps builds the same ImageMemoryBarrier2 a third time. makeTransferWritesVisible is memoryBarrier with fixed states.
- **Inference:** The most-edited sync code lives in three copies. Otherwise the similarity is acceptable.
- **Severity:** polish
- **Confidence:** high
- **Affects:** synchronisation changes.
- **Proposal:** A private imageBarrier(...) helper, and drop makeTransferWritesVisible if it has one caller. Size S, mechanical.

### D-10: Raw steady_clock beside utils::Timer; two FrameTimer types
- **Where:** engine.cppm run and applyFrameBrake; render_graph/frame_graph.cpp:145,158; utils/timer.cppm Timer (high_resolution_clock); test/main.cpp and frame_graph.cppm FrameTimer.
- **Observed:** Only core::Profiler uses utils::Timer.
- **Inference:** Two timing idioms, and high_resolution_clock is not guaranteed steady.
- **Severity:** polish
- **Confidence:** medium
- **Affects:** delta-time logic.
- **Proposal:** Move Timer to steady_clock, use it in Engine and FrameGraph, and rename one FrameTimer. Size S.

### D-11: Examples repeat scene boilerplate
- **Where:** examples/{sponza,helmets,simple-scene,eval-scene,template-scene}/main.cpp and CMakeLists.txt.
- **Observed:** The five CMakeLists differ only in the project name. Sponza's camera, skybox, model and light setup is repeated inside eval-scene (lines 22-28 and 52-55). The `Engine::loadEnvironment(core::ASSETS_DIR / "Environments/KloppenheimSky.hdr")` line recurs in 3 files.
- **Inference:** Mild. It matters only when the Engine API changes (all examples need edits).
- **Severity:** polish
- **Confidence:** high
- **Affects:** example maintenance.
- **Proposal:** An `aegis_add_example(name)` CMake function. Make eval-scene reuse or drop its Sponza scene. Size S, mechanical.

Acceptable similarity, not to fix: the per-pass local `struct PushConstants` (each mirrors a different Slang struct), and the per-pass Pipeline::*Desc setup.

## B. Dead code (each Grep-verified over src, tests, examples)

- **D-12: utils exports.** The only importer of aegis.utils.* is core/profiler (rolling_average, timer).
  - Unused: `utils::Color` (not exported, `Color::` has zero uses), `utils::hashCombine` (not exported, zero callers), `file::readBinary`, `ScopeTimer`, and the `aegis.utils` primary module itself.
  - Proposal: delete color, file, hashCombine and ScopeTimer; keep rolling_average and timer. Size S, mechanical. Severity polish, confidence high.
- **D-13: math.**
  - Dead: `math::normalMatrix` (both overloads, see D-06) and the `PerlinNoise1D` class (its namespace is not exported and nothing calls it; delete perlin_noise.cppm).
  - Also dead, but used only by perlin_noise: `math::percentage`, `tanh01`, `sigmoid01`, `fastSigmoid01` (no callers; fastSigmoid01 and sigmoid01 have none even there). `math::lerp` has no callers (perlin uses std::lerp).
  - Unused glm re-exports: make_mat4, angleAxis (used by examples, keep), row (used by frustum with plain glm, check the import).
  - Keep: Random (used by eval-scene), forward/right/up, tranformationMatrix, decomposeTRS and inFOV.
  - Delete interpolation.cppm and perlin_noise.cppm together, since the two depend on each other. Also fix the `tranformationMatrix` typo as a separate rename. Size S, mechanical. Severity polish, confidence high.
- **D-14: scripting.** `dynamic_movement_controller`, `world_border` and `physics/motion_dynamics` (module aegis.physics.motion_dynamics) are imported by no file except each other. dynamic_movement_controller is the only user of motion_dynamics. All three are only in scripting/CMakeLists.txt. Only KinematicMovementController is used (scene_defaults).
  - Delete the three, or keep one if a physics feature is planned. Size S, mechanical. Severity polish, confidence high.
- **D-15: modules/aegis-gltf.** Nothing links Aegis::GLTF or includes it (Grep). `modules/CMakeLists.txt` still calls add_subdirectory(aegis-gltf), so it is configured and built. It is superseded by fastgltf in assets.
  - Delete the directory and the add_subdirectory line. Only 1 file under it is tracked in git; the rest (including `out/build`) is untracked local output. Size S, mechanical. Severity polish, confidence high.
- **D-16: core globals.** `core::SHADER_DIR` has no users (shaders use AEGIS_SHADER_DIR). `core::INVALID_HANDLE` has no users. The only users of `ENGINE_DIR` are two eval-scene paths under "temp/". `PROJECT_DIR` and `BUILD_DIR` are compiled into aegis-core only for these constants.
  - Delete SHADER_DIR and INVALID_HANDLE. Keep ASSETS_DIR, which examples use. Size S, mechanical. Severity polish, confidence high.
- **D-17: AssetManager.** `Engine::assets()` has no callers. `m_assets.add` only stores the defaults, and `get()` is "not implemented yet" (asserts false). `garbageCollect` has no callers. `IsLoadable` has no users. It is a singleton asserting on a second instance, next to Engine's own singleton.
  - Delete AssetManager, `Engine::assets()`, `IsLoadable` and the "old names" block in `loadDefaultAssets`, unless an on-demand loader is planned. Keep `core::Asset` (base of Texture, Material and StaticMesh), or move it to assets and drop `aegis-core` from assets. Size S, logic-adjacent. Severity design problem, confidence high.
- **D-18: rhi.**
  - `Device::Desc` is an empty struct. Its only use is `createDevice(const Device::Desc&)` called with `{}` (engine, test).
  - `computeQueue()` and `transferQueue()` have no callers, yet the device creates, names and owns both queues.
  - Delete the Desc parameter and the two accessors, but keep the queues if async compute or transfer is planned. Size S. Severity polish, confidence high.
- **D-19: shaders.** None found dead. All 15 shaders are loaded by name from src. The module imports are used (`import modules.*`).
- **D-20: Unlinked shared state.** `aegis-utils` is a library for 2 classes, and `core/CMakeLists` links `aegis-utils` and `imguizmo` PUBLIC.
  - If D-12 is applied, fold timer and rolling_average into core and delete the utils target. Size S. Severity polish, confidence medium.
- **D-21: GPU-driven helper debris.** `rhi/utility.cppm` lines 59-61 contain a commented-out visitor example. `core/assert.h` has a commented-out ALOG block. Delete them. Severity polish, confidence high.

## C. Inconsistent patterns (2+ occurrences each way)

- **D-22: Error handling.** Four styles (new-style expected, AGX_ASSERT, println then return, std::ignore).
  - println-and-continue: renderer.cpp:191,223,240,349,359, rhi/context.cpp:55, gpu_driven.cpp:40, lighting_pass.cpp:90.
  - std::ignore on a fallible call: assets/upload.cppm:25, render_graph/immediate.cppm:19, engine.cppm:175 (flushUploads), gpu_driven.cpp:109,119, renderer.cpp:180, device.cpp:316.
  - Engine::check (log, assert, T{}). Window asserts. Engine::createRenderer asserts on a failed expected. AGX_ASSERT compiles to nothing in release.
  - Inference: A failure in swapchain recreation or submit is printed and the frame loop carries on, so nothing can observe it and tests cannot check it.
  - Proposal: a rule. New-style code returns expected up to Renderer/Engine, and only the Engine edge logs. `flushUploads` returns a result that is propagated. Size M, logic. Severity design problem, confidence high.
- **D-23: Logging.** `ALOG` (aegis-log) is used only in engine.cppm and core/logging. The new-style modules use `std::println` straight to stdout (8 sites in rhi, renderer, render_graph). assert.h uses std::cout. aegis-log has a Trace/Fatal severity scheme nothing below Engine can reach.
  - Proposal: pick one. Either rhi, renderer and render_graph return errors and never print (see D-22), or give them a small log hook. Size M. Severity design problem, confidence high.
- **D-24: Ownership.**
  - assets use `shared_ptr` created via `new` plus a private ctor (texture.cpp:94, static_mesh.cpp:105, material.cpp:113).
  - Components hold `shared_ptr<Mesh/Material>`, while rhi and render_graph resources are generational handles or values.
  - `ScriptManager` news scripts with no delete (script_manager.cppm:44, map-known). Scene systems use `unique_ptr`.
  - Inference: Consistent enough inside each layer, but "who frees GPU memory" differs per layer (the shared_ptr destructor calls Device::free).
  - Severity design problem, confidence medium.
- **D-25: Naming.**
  - Config structs: `Desc` (23 uses in rhi, assets, renderer, platform, ui_pass) vs `Settings` (post_effects.cppm:27, screen_passes.cppm:41) vs `Info` (render_pass.cppm:35, the pass declaration, not configuration) vs `Options` (test).
  - Creation verbs: `create` (static, expected) vs `load*` (assets) vs a public ctor (Window, Input, Engine) vs `make*` (Error, makeInstance) vs `add*` (pool, graph).
  - Getters: plain accessors in rhi and render_graph vs `get`/`set` prefixes (`getOrAdd`, `getSlot`) vs `instance()` statics in legacy code.
  - Members: `LayerStack::m_Layers` uses uppercase after the m_ prefix.
  - Proposal: rename Settings to Desc and m_Layers to m_layers (mechanical, own PR). The create/load split stays, and it is documented in AGENTS.md. Size S. Severity polish, confidence high.
- **D-26: Registration.** There are five ways to extend the engine, each different.
  - `FrameGraph::add<Pass>()` plus `pass.addRenderSystem<>()`.
  - `Scene::addSystem<T>()` (a vector of unique_ptr).
  - `ScriptManager::addScript<T>()` (new, no delete).
  - `LayerStack::push<T>()`.
  - `SceneDescription` subclasses via `Engine::loadScene<T>()`.
  - Inference: Each is a template with perfect-forwarded args, which is consistent. But the render-system and scripts lifetimes differ, and default systems are registered in scene_defaults.cppm while the default passes are in render_graph/default_graph.cpp.
  - Proposal: leave as is, and document where a new pass, system and script goes. Severity polish, confidence medium.

### D-27: Engine::createRenderer dereferences a failed expected in release builds (bug)
- **Where:** engine.cppm Engine::createRenderer (and loadDefaultAssets, createFrameGraph).
- **Observed:** `AGX_ASSERT_X(context, ...); m_context = std::move(*context);`, repeated for device and renderer. With NDEBUG, AGX_ASSERT_X expands to `static_cast<void>(0)` (core/assert.h). In loadDefaultAssets, the failure path of the IBL create is logged and then `std::move(*ibl)` runs anyway. `check()` returns `T{}` (a null shared_ptr) after the disabled assert.
- **Inference:** Failure scenario: a release build on a machine without a Vulkan 1.3 device. Context::create returns the unexpected value and `*context` is undefined behaviour, so you get a crash or garbage instead of a message. In debug the assert reports it.
- **Severity:** bug
- **Confidence:** high
- **Affects:** release builds, startup failures, any headless test of Engine.
- **Proposal:** `Engine::create() -> std::expected<Engine, Error>` or an immediate log plus std::exit(1) at each of these sites. Size S, logic.

## D. Structure

- **D-28: Root files and module names.**
  - `engine.cppm` (aegis.engine), `scene_defaults.cppm` (module aegis.defaults, file name differs) and `scene_description.cppm` (aegis.scene_description) sit at src/aegis root, and are compiled into the aegis-engine target.
  - Module name vs path mismatches: `aegis.defaults` vs scene_defaults.cppm, and `aegis.physics.motion_dynamics` under scripting/ (dead, see D-14).
  - Proposal: move into `src/aegis/engine/` (engine.cppm, scene_defaults as `aegis.scene_defaults` and scene_description) so the root holds only subsystem directories. The rename is mechanical (own PR). Size S.
- **D-29: render_graph is a framework plus all passes plus IBL plus UI backend in one module.** Passes include `ui_pass` (ImGui Vulkan backend), `ibl.cpp`, `shader.cpp` and ~13 concrete passes.
  - Observed: tests/render_graph links the whole scene stack because of the scene dependency (U2, map).
  - Proposal: split into `render_graph` (frame_graph, schedule, resource_pool, frustum, draw_batch_registry, render_pass), `render_passes` (everything under passes/, ibl, shader) and UI backend (with ui_pass). This gives the pure schedule/pool tests a smaller link set and lets a pass-only change skip rebuilding the framework (~180 steps, agent-notes). Size L, build restructuring (own PR). Severity design problem, confidence medium.
- **D-30: test/ under src/ vs tests/ at root.** src/aegis/test (aegis-test, a device-check program) sits beside tests/ (doctest). The names are easy to confuse.
  - Proposal: rename to `src/aegis/device_check` (or move to tools/) and split main.cpp (D-05). Size S rename (mechanical), L for the split.
- **D-31: Module style split.** 83 module files have 3 name parts, 13 have 2 and 11 have 4. New-style code uses partitions, legacy uses dotted names (math is the exception, M4). Mixed `.cppm`-only headers in `core`, `scene`, `scripting` and `utils` compile every importer on any edit.
  - Proposal: when each legacy module is touched, convert it to partition plus .cpp, as the AGENTS.md style table already says. Do not do it as a sweep. Severity polish, confidence high.
- **D-32: File size outliers.** test/main.cpp 1490 lines (D-05), rhi/device.cpp 725 (20+ create/free overloads, D-02), rhi/command_buffer.cpp 602, rhi/vulkan_conversions.cppm 598 (enum tables, acceptable), editor/panels/scene_panel.cppm 382 (header-only legacy).
  - Proposal: only main.cpp is worth splitting now. Device is hub-shaped by design; split it only if D-02 does not shrink it. Severity polish, confidence medium.

## E. Cleanup batch (safe, mechanical, independent of any redesign; suggested first PRs)
1. Delete modules/aegis-gltf and its add_subdirectory line (D-15).
2. Delete utils Color, hashCombine, file and ScopeTimer (D-12), then optionally fold utils into core (D-20).
3. Delete math perlin_noise, interpolation and normalMatrix (D-13, D-06).
4. Delete dynamic_movement_controller, world_border and motion_dynamics, and the three CMake lines (D-14).
5. Delete core::SHADER_DIR, core::INVALID_HANDLE, AssetManager, Engine::assets() and IsLoadable (D-16, D-17).
6. Delete the empty Device::Desc and the two unused queue accessors (D-18), and the commented-out code (D-21).
7. Rename Settings to Desc and m_Layers to m_layers, fix the tranformationMatrix typo (D-25).
8. Move the three root files into src/aegis/engine/ and rename aegis-test to device_check (D-28, D-30).
9. Add the resource-name constants (D-08), then the shared SampledTexture type (D-03). Both are small and mechanical.
Not in the batch (logic, own PRs): D-27 (fix first, it is a bug), D-01, D-02, D-05, D-07, D-22, D-23, D-29.

Items I did not verify in depth: the possible descriptor-slot leak in Device::replace (D-02) and the `row`/`make_mat4` re-export usage (D-13). Both need a Read of their callers before anyone acts.
