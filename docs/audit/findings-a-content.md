# Findings, lens a (architecture): content and platform

Scope: `src/aegis/assets`, `src/aegis/platform`. Consumers read as evidence: `src/aegis/engine.cppm`, `src/aegis/test/main.cpp`, `src/aegis/render_graph/{ibl.cpp,draw_batch_registry.cpp,passes/*}`, `src/aegis/editor/panels/scene_panel.cppm`, `tests/assets`, `examples/`. Paths are relative to `src/aegis/` unless they start with `examples/`, `tests/` or `docs/`. Every fact was re-checked against the code; the map was used only as an index. Known gotchas from `docs/agent-notes.md` are cited as evidence, not reported as new findings.

Summary: 13 findings: 3 bugs, 8 design problems, 2 polish.

| ID | Title | Severity | Conf. |
|---|---|---|---|
| A-CONTENT-01 | `aegis.assets` builds ECS entities: upward edge into `aegis.scene.registry` | design problem | high |
| A-CONTENT-02 | `GltfImport` is one monolith with no CPU intermediate | design problem | high |
| A-CONTENT-03 | Loaders hand out the engine-wide default material as if it were the entity's own | bug | high |
| A-CONTENT-04 | Ownership is `shared_ptr` + raw `Device&`, with no asset identity, cache or dedupe | design problem | high |
| A-CONTENT-05 | `core::Asset` base: a legacy dependency that provides one unread field | polish | high |
| A-CONTENT-06 | Every `Texture` owns a private sampler; glTF samplers are ignored | design problem | high |
| A-CONTENT-07 | glTF import silently drops or misreads data it does not support | bug | medium |
| A-CONTENT-08 | `Texture` is a GPU type with decoders built in: each new format needs a new factory | design problem | high |
| A-CONTENT-09 | Material GPU sync and validation are pushed onto consumers | design problem | medium |
| A-CONTENT-10 | Loading is synchronous, blocking and bound to the graphics queue | design problem | high |
| A-CONTENT-11 | `platform::Window` has no error channel, and GLFW is terminated per window | bug | high |
| A-CONTENT-12 | GLFW leaks through `platform`'s public API into `rhi` and the engine | polish | medium |
| A-CONTENT-13 | `Input` is welded to `Window` and GLFW, so its logic cannot be unit-tested | design problem | high |

---

## 1. Findings

### A-CONTENT-01: `aegis.assets` builds ECS entities: upward edge into `aegis.scene.registry`
- **Where:**
  - `assets/loader.cppm`: `Loader::Desc::scene`; `Loader::load` returns `aegis::scene::Entity`.
  - `assets/gltf_loader.cpp`: `GltfImport::run`, `GltfImport::buildScene`.
  - `assets/obj_loader.cpp`: `Loader::loadObj`.
  - `assets/components.cppm`: `Mesh`, `Material`, `Environment`.
  - `assets/CMakeLists.txt`: PUBLIC `aegis-scene`, PRIVATE `EnTT::EnTT`.
- **Observed:**
  - `Loader::Desc` holds `aegis::scene::Registry& scene`. `buildScene` calls `scene.create(name, t, r, s)`, `scene.add<Mesh>`, `scene.add<Material>`, `scene.addChild` and `scene.setParent`, and reads `scene.get<aegis::Parent>(entity).entity`. `run` writes `scene.get<aegis::Transform>(m_root).rotation` (the Y-up to Z-up fix). `loadObj` calls `desc.scene.create(name)` and then `add` twice.
  - `CMakeLists.txt` links `aegis-scene` PUBLIC, with the comment "The loaders populate a Scene::Registry". EnTT is linked PRIVATE even though no file in `assets/` names entt.
  - `components.cppm` defines three ECS components in the asset module: `Mesh{shared_ptr<StaticMesh>}`, `Material{shared_ptr<MaterialInstance>}` and `Environment{4 x shared_ptr<Texture>}`. `Environment` is *produced* by `render_graph::Ibl::loadEnvironment` (`render_graph/ibl.cpp`), not by assets.
  - `tests/assets/CMakeLists.txt` links only `aegis-assets`. That still builds the legacy scene stack (`aegis-scene`, `aegis-core`, `aegis-utils`) into a test of `MeshPreprocessor`.
  - The only test of the loaders is `aegis-test`'s `verifyAssets` (`test/main.cpp` ~620). It needs a live `rhi::Device` and a legacy `aegis::scene::Registry`.
- **Inference:** The new-style content layer (L4) depends on the legacy scene (L6). Three consequences:
  - The loader can't be used or tested without a registry.
  - The glTF node hierarchy exists only as entities, so spawning a model again means reloading the file.
  - Once the planned `SceneView` decouples render_graph (engine-architecture.md §6), assets will be the last new-style module that imports `aegis.scene`.

  The components are scene-to-asset bindings, so they belong with the scene glue (the engine today, the World later), not in assets.
- **Severity:** design problem
- **Confidence:** high
- **Affects:**
  - Extensions: every new asset type that becomes an entity (skinned mesh, terrain, vegetation), prefabs and instancing, serialization, the `SceneView` plan.
  - Tests: loader unit tests; the build weight of `aegis-tests-assets`.
- **Proposal:**
  - Replace `Loader::load(Desc, path) -> Entity` with `loadModel(Device&, ModelDefaults, path) -> expected<Model>`. `Model` is plain data: per-primitive `shared_ptr<StaticMesh>`s, the materials, and `nodes{name, TRS, parent index, optional mesh index}`, plus the axis-fix root transform.
  - Move `buildScene`'s entity creation (~50 lines) into `instantiate(Registry&, const Model&) -> Entity`, in the scene glue (`engine.cppm` today, the World later). Move `Mesh` and `Material` with it. `Environment` is better owned by render_graph's IBL.
  - Drop `aegis-scene` and `EnTT` from `assets/CMakeLists.txt`.
  - What it unlocks: instancing a model N times from one load, a testable node mapping, and an assets module free of legacy code.
  - Callers to update: `Engine::loadModel` and `test/main.cpp` (2 places). Size S-M.

### A-CONTENT-02: `GltfImport` is one monolith with no CPU intermediate
- **Where:** `assets/gltf_loader.cpp`: the anonymous-namespace class `GltfImport` (`run`, `loadMeshes`, `loadTextures`, `loadMaterials`, `buildScene`) and `Loader::loadGltf`.
- **Observed:** One 287-line file does all of the following in one pass on the caller thread, and every stage uses `m_desc.device`:
  - fastgltf parsing (`Parser::loadGltf`);
  - accessor reads into `MeshPreprocessor::Input`;
  - meshoptimizer processing;
  - `StaticMesh::create`, which creates GPU buffers and queues their uploads;
  - choosing sRGB or UNORM per texture;
  - stb decode plus upload, through `Texture::loadFromFile` and `loadFromMemory`;
  - mapping glTF PBR to template parameters by string (`"albedo"`, `"metalRoughnessMap"`, ...);
  - building the entities.

  Only `MeshPreprocessor` and the material layout helpers have unit tests (10 cases in `tests/assets`).
- **Inference:**
  - Content features will land in the pure parts: accessor glue, format selection, material mapping and the node hierarchy. That covers skins, KTX2, extensions such as `KHR_texture_transform`, and alpha modes. None of those parts can be tested without a GPU.
  - There is no point where a worker thread could stop and hand plain data to the main thread, so streaming has nothing to build on.
  - This is the "extract, don't mock" case from `tests/README.md`, at module scale.
- **Severity:** design problem
- **Confidence:** high
- **Affects:**
  - Extensions: every new glTF feature or model format, async loading, caching, hot reload.
  - Tests: glTF mapping, format selection and node mapping, all untested today.
- **Proposal:** Split the work into a CPU import step and a GPU upload step.
  - **Import:** `importGltf(path) -> expected<ModelData>`. `ModelData` holds `vector<MeshData>` per primitive (`MeshData` already exists), `vector<ImageData>` (decoded pixels, format, extent and mip count; see A-CONTENT-08), `vector<MaterialData>` (plain factors, texture indices, alpha mode, double-sided) and `vector<NodeData>`.
  - **Upload:** `upload(Device&, ModelData&&, defaults) -> expected<Model>`, producing the `Model` from A-CONTENT-01.
  - **Tests:** the glTF → `MaterialData` and node mappings become doctest cases over a small glTF held in memory (fastgltf can parse from a buffer).
  - Size M. This change is NOW; see §4.

### A-CONTENT-03: Loaders hand out the engine-wide default material as if it were the entity's own
- **Where:**
  - `assets/obj_loader.cpp`: `Loader::loadObj` calls `desc.scene.add<Material>(root, desc.defaultMaterial)`.
  - `assets/gltf_loader.cpp`: `GltfImport::materialFor`.
  - `engine.cppm`: `Engine::loadModel` passes `.defaultMaterial = engine.m_defaults->pbrMaterial`.
  - Consumers: `examples/eval-scene/main.cpp` ~279-284 and `examples/helmets/main.cpp` `ColorChanger::update`.
- **Observed:**
  - Every OBJ entity, and every glTF primitive without a material, gets the same `shared_ptr<MaterialInstance>`: `DefaultAssets::pbrMaterial`. `setParameter` is a non-const mutator on that shared object.
  - Eval-Scene takes it as the cube's own material:
    `auto& cubeMat = registry.get<aegis::assets::Material>(cube).instance; cubeMat->setParameter("albedo", glm::vec3{ 0.8f, 0.1f, 0.1f });`
    and then also sets metallic and roughness.
  - Helmets attaches a `ColorChanger` script to the OBJ plane. It calls `get<aegis::assets::Material>().instance->setParameter("albedo", color)` every frame.
  - Simple-Scene avoids the problem only because it replaces `.instance` with its own material.
- **Inference / failure scenario:**
  - In Eval-Scene's cube scene, every entity on the default material turns red and metallic: any other OBJ, any material-less glTF primitive, and every later `loadModel` in the process, because `DefaultAssets` lives as long as the engine.
  - In Helmets, all such entities would cycle colour with the plane.
  - The API invites the mistake: the component is named `Material`, and nothing signals that the instance is shared.
- **Severity:** bug. It is latent in today's scenes and certain for the first scene that combines these pieces.
- **Confidence:** high for the mechanism; the visible effect depends on scene contents.
- **Affects:**
  - Extensions: editor material editing, scripting, per-entity tints.
  - Tests: an import or instantiate unit test can pin "the default material is not shared mutable state".
- **Proposal:**
  - Make shared instances const: `DefaultAssets::pbrMaterial` becomes `shared_ptr<const MaterialInstance>`, and so does what `assets::Material` holds. Mutating then needs an explicit `clone()` or `createMaterial`.
  - Minimal alternative: the loader creates one fresh default instance per load.
  - Size S.

### A-CONTENT-04: Ownership is `shared_ptr` + raw `Device&`, with no asset identity, cache or dedupe
- **Where:**
  - `StaticMesh`, `Texture`, `MaterialInstance`: each has `m_device` and a destructor that calls `m_device.free(...)`.
  - `assets/components.cppm`.
  - `engine.cppm`: `loadModel`, `loadTexture`, `loadEnvironment`.
  - `GltfImport::loadTextures`.
- **Observed:**
  - Each asset is created through a private constructor and `new` into a `shared_ptr`, holds `rhi::Device&`, and frees its handles in its destructor. Nothing ties that lifetime to the Device; only `Engine` member order does (agent-notes).
  - There is no cache:
    - `Loader::load` re-reads, re-decodes and re-uploads on every call.
    - `Engine::loadEnvironment` reruns the IBL compute chain on every call. Eval-Scene loads `KloppenheimSky.hdr` six times (lines 23, 53, 89, 167, 268, 328).
    - Within one glTF, `loadTextures` decodes `images[*texture.imageIndex]` once per texture. Two glTF textures that share an image are decoded and uploaded twice.
  - The only identity is `core::Asset::path()`, and nothing reads it (A-CONTENT-05).
- **Inference:**
  - `shared_ptr` is fine for *lifetime*: the RHI deletion queue makes refcounted release safe from anywhere in the frame.
  - What is missing is *identity*, a key under which an asset can be found again. Dedupe, hot-reload swaps, streaming residency and serialization all need one.
  - The raw `Device&` is the same pattern the RHI uses internally. An `AssetLibrary` owning the assets would make the lifetime rule structural.
- **Severity:** design problem
- **Confidence:** high
- **Affects:**
  - Extensions: streaming, hot reload, serialization, an editor asset browser, memory use in large scenes.
  - Tests: an identity/cache layer keyed on plain values is unit-testable.
- **Proposal:** See §4. Keep `shared_ptr` for lifetime. Add an `AssetLibrary` that maps a stable key (normalized relative path plus a sub-asset index) to the loaded asset and dedupes loads. No generational asset handles yet. Size M.

### A-CONTENT-05: `core::Asset` base: a legacy dependency that provides one unread field
- **Where:**
  - `core/asset.cppm`: `aegis::core::Asset`.
  - Imported by `assets/{static_mesh,texture,material}.cppm`.
  - `assets/CMakeLists.txt` links `aegis-core` PUBLIC, with the comment "Aegis::Core::Asset, the base AssetManager stores".
- **Observed:**
  - `Asset` has only a virtual destructor, `path()` and `setPath()`.
  - `setPath` is called from `texture.cpp`, `obj_loader.cpp` and `ibl.cpp` with real paths. `gltf_loader.cpp:179` calls `instance.setPath(std::string{ gltfMaterial.name })`, storing a material *name* as a path.
  - Grep finds no reader of `path()` outside `core/asset_manager.cppm`.
  - glTF meshes and embedded textures get no path at all.
  - `IsAsset` and `IsLoadable` are used only by `AssetManager`, which engine-architecture.md §7 already marks for deletion.
- **Inference:**
  - The base costs every asset a vtable and links `aegis-core` (and through it imguizmo, map C7) into assets and its tests, for a field that is set inconsistently and never read.
  - It stands in for the identity that A-CONTENT-04 says is missing, but identity belongs in the library that maps keys to assets, not in the asset object.
- **Severity:** polish. It is already listed in engine-architecture.md §7; it's reported here because it is one of the two legacy edges.
- **Confidence:** high
- **Affects:** test link weight; serialization (needs a real key).
- **Proposal:** Delete the base and the three imports together with `AssetManager`. Keep a `std::string name` where debugging needs one. Size S.

### A-CONTENT-06: Every `Texture` owns a private sampler; glTF samplers are ignored
- **Where:**
  - `assets/texture.cpp`: `Texture::create(Device&, const Desc&)` calls `device.createSampler` once per texture.
  - `rhi/device.cpp`: `Device::createSampler`, which has no dedupe.
  - `rhi/bindless_heap.cppm`: `maxSamplers{ 2048 }`.
  - `gltf_loader.cpp`: `loadTextures`.
- **Observed:**
  - Each `Texture` creates its own `VkSampler` and bindless sampler slot, although almost all of them use the default `Sampler::Desc`.
  - `createSampler` returns `InitializationFailed` when the heap has no free slot.
  - `GltfImport` never reads `m_gltf.samplers` or `texture.samplerIndex`. Every glTF texture therefore uses Repeat, linear filtering and 16x anisotropy, whatever the file asks for.
- **Inference / failure scenario:**
  - Once a scene has more than ~2,040 textures (2048 slots minus the render graph's own samplers), loading the next texture fails with a GPU error. Vulkan's `maxSamplerAllocationCount`, often 4,000 on desktop drivers, is the next limit. Streamed worlds and terrain or vegetation texture sets hit this first.
  - A glTF that asks for ClampToEdge (decals, atlases) samples with Repeat instead and shows edge bleeding.
  - The material block already stores a sampler index next to each image index ("A texture takes 8 bytes"), so sharing samplers needs no shader change.
- **Severity:** design problem
- **Confidence:** high for the mechanism; medium for the exact limits, which vary by driver.
- **Affects:**
  - Extensions: streaming, terrain, vegetation, KTX2 (which carries no sampler).
  - Tests: a sampler cache keyed on `Sampler::Desc` is pure and testable.
- **Proposal:** `Texture` keeps its image and view but references a shared sampler from a `SamplerCache` keyed by `Sampler::Desc` (in rhi or assets). `GltfImport` maps glTF samplers to descs. Size S-M.

### A-CONTENT-07: glTF import silently drops or misreads data it does not support
- **Where:** `assets/gltf_loader.cpp`: `loadMeshes`, `loadMaterials`, `buildScene`, `run`.
- **Observed:**
  1. `loadMeshes` never reads `primitive.type`. Points, lines, strips and fans all go into `MeshPreprocessor` as a triangle list.
  2. `run` uses `defaultScene.value_or(0)` only for the root's name. `buildScene` creates an entity for every node in `m_gltf.nodes` and parents every parentless one to the root, ignoring `scenes[i].nodeIndices`.
  3. `setTexture` ignores `texCoordIndex`; only `TEXCOORD_0` is read.
  4. `loadMaterials` ignores `alphaMode`, `alphaCutoff` and `doubleSided`. Every instance uses the one Opaque `pbrTemplate`, so `MaterialType::Transparent` can't come from a file. No shader does an alpha test: grep for `discard` finds only `point_light.slang`.
- **Inference / failure scenarios:**
  - (1) A `TRIANGLE_STRIP` primitive whose index count is a multiple of 3 renders as scrambled triangles with no error. Any other strip is rejected with the misleading message "not a multiple of 3".
  - (2) A file with two scenes renders both, on top of each other.
  - (4) Alpha-masked foliage renders as opaque quads. Khronos' Sponza uses MASK for its plants (an inference from the sample asset, not checked in the submodule). This is the asset side of vegetation.
- **Severity:** bug for items 1 and 2; design gap for items 3 and 4.
- **Confidence:** medium. The code paths are certain; the impact depends on content.
- **Affects:**
  - Extensions: vegetation, transparency, any glTF beyond the sample set.
  - Tests: all four become unit tests once import is CPU-only (A-CONTENT-02).
- **Proposal:**
  - Reject non-triangle topology with `UnsupportedFormat`, or convert strips and fans.
  - Instantiate only the subtrees of the default scene's `nodeIndices`.
  - Carry alpha mode, cutoff, double-sided and texCoord in `MaterialData`.
  - Size S.

### A-CONTENT-08: `Texture` is a GPU type with decoders built in: each new format needs a new factory
- **Where:**
  - `assets/texture.cppm` / `texture.cpp`: seven factories, `create(Device&, Desc, mip0)`, `create(Device&, Desc)`, `loadFromFile`, `loadEquirectangular`, `loadFromMemory`, `solidColor` and `solidColorCube`, plus the helper `createFromRgba8`.
  - `gltf_loader.cpp` `loadTextures`, which chooses the format.
- **Observed:**
  - Three of the factories decode with stb inside the factory, so decoding and uploading are one call.
  - LDR input is always decoded to RGBA8 (`isRgba8` rejects anything else). HDR is always RGBA32F with one mip.
  - `create(desc, mip0)` accepts only mip 0; the remaining mips are generated on the GPU.
  - The caller chooses sRGB vs UNORM, and `GltfImport` re-derives that from how materials use each texture.
  - `create(desc)` without data leaves the image in `ResourceState::Unknown` for the caller (`Ibl`) to transition.
  - Nothing in assets checks that `mip0`'s size matches `desc`. Only an `assert` in `upload_manager.cpp` ("Upload data does not match image layout") does, and it compiles out in release.
- **Inference:**
  - KTX2/BCn needs "upload all mips as given", which no factory can express. The RHI already supports it: `Device::upload(image, data, generateMips=false)` with the `subresourceFootprints` layout.
  - Each new format adds another factory that decodes and uploads in one step, so decoding stays off the testable path.
- **Severity:** design problem
- **Confidence:** high
- **Affects:**
  - Extensions: KTX2 and compressed textures, terrain heightmaps (R16/R32F, never sRGB), mip-level streaming.
  - Tests: decoders that work on bytes become doctest cases.
- **Proposal:**
  - Add a plain `ImageData {format, extent, mipLevels, arrayLayers, type, vector<byte> bytes, generateMips}`.
  - Add free decoders: `decodeImage(span<const byte>, ColorSpace) -> expected<ImageData>`, `decodeHdr`, and later `decodeKtx2`.
  - Replace the four loading factories with one `Texture::create(Device&, const ImageData&, sampler)`. It validates the size before uploading and returns `InvalidData` on a mismatch. Keep `create(desc)` for GPU-filled textures.
  - A new format is then one decoder function plus tests, with no change to `Texture`. Size S-M.

### A-CONTENT-09: Material GPU sync and validation are pushed onto consumers
- **Where:**
  - `assets/material.*`: `MaterialInstance::setParameter`, `update(frameIndex)`, `parameterAddress`; `MaterialTemplate::addParameter`.
  - `render_graph/passes/static_mesh_render_system.cpp`: `render` calls `instance->update(frameIndex)`.
  - `render_graph/passes/gpu_driven.cpp`: `collectInstances` and `InstanceUpdatePass::sceneInitialized`.
- **Observed:**
  - `setParameter` and `addParameter` validate only with `assert`. In release builds:
    - an unknown name is stored as an override that `writeParameters` never reads;
    - a value of the wrong type is written with its own size at the template's offset;
    - `parameter<T>` then throws `bad_variant_access`.
  - Parameter names are strings, repeated across `defaults.cpp`, `gltf_loader.cpp` and the examples.
  - Every pass that reads materials must call `update(frameIndex)` itself.
  - The GPU-driven static path calls `update(0)` once, in `sceneInitialized`, and reads the frame-0 block from then on. Its comment says "Static instances never change, so their materials' frame 0 block serves every frame".
  - `update` allocates a new byte vector for every dirty slot.
- **Inference / failure scenario:**
  - With GPU-driven rendering, a script or a future editor panel that changes the material of a *static* entity has no visible effect, while the CPU path shows the change. Helmets works around this by tagging its plane `DynamicTag`.
  - The rule for who uploads material blocks lives in two passes with two different conventions, and every new pass that reads materials has to learn it.
- **Severity:** design problem
- **Confidence:** medium
- **Affects:**
  - Extensions: editor material editing, new material templates (terrain, foliage), new passes.
  - Tests: dirty tracking and validation are untested.
- **Proposal:**
  - `setParameter` returns `std::expected<void, Error>`, with `InvalidData` for an unknown name or wrong type.
  - One owner syncs every dirty instance once per frame (the frame loop or `DrawBatchRegistry`), so passes only read `parameterAddress`.
  - Size S.

### A-CONTENT-10: Loading is synchronous, blocking and bound to the graphics queue
- **Where:**
  - `assets/upload.cppm`: `detail::uploadBlocking`.
  - `StaticMesh::create`, `Texture::create`.
  - `rhi/upload_manager.cpp`: `flushPending`, `reclaimFront`.
  - `rhi/device.cpp`: `retireValue`, `flushUploads`.
  - `engine.cppm` `run`: `std::ignore = m_device->flushUploads()`.
- **Observed:**
  - `uploadBlocking` retries an upload up to 16 times and flushes between attempts, ignoring the flush result.
  - `flushPending` submits on the graphics queue. When the command-buffer ring is full, `reclaimFront` waits on the GPU (`std::ignore = queue.wait(...)`).
  - Nothing on the device side is synchronized: the pools, the bindless heap, the deletion queue and `UploadManager` assume one thread. `transferQueue()` has no callers.
  - `Device::free` retires a resource at `graphicsQueue.lastSubmitted() + 1`. A mesh freed while its upload is still in the open batch is safe only because the next graphics submission is that upload batch. This case is real: `GltfImport` fails in `loadTextures` after `loadMeshes` has already queued buffers. `Engine::run` and `submitImmediate` both flush uploads before submitting anything else, which is what keeps it safe.
- **Inference:**
  - This is fine for loading at startup, and it is the main obstacle to streaming. CPU work can't move to a worker thread while it is interleaved with Device calls (A-CONTENT-02).
  - The free-while-pending rule depends on submission order on a single queue. It breaks once uploads move to a transfer queue, which also needs queue-family ownership transfer.
  - None of this blocks anything until streaming starts, provided import is split from upload first.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** Extensions: streaming, background loading, loading screens.
- **Proposal:**
  - Now: nothing beyond A-CONTENT-02.
  - With streaming: workers produce `ModelData` and `ImageData`; the main thread uploads within a per-frame byte budget; uploads on a transfer queue get release/acquire barriers and their own retire value.
  - Size L, with the feature.

### A-CONTENT-11: `platform::Window` has no error channel, and GLFW is terminated per window
- **Where:** `platform/window.cpp`: `Window::Window`, `~Window`, `operator=(Window&&)`.
- **Observed:**
  - The constructor runs `glfwInit();` without checking its result, then `assert(m_window && "Platform Error: Failed to create window");`. In release the assert is gone, and `glfwSetWindowUserPointer` runs on a null window.
  - The destructor calls `glfwDestroyWindow(m_window); glfwTerminate();`.
  - Move assignment swaps the two windows, so `other`'s destructor destroys the old window and terminates GLFW.
- **Failure scenario:**
  - `Window a{...}; a = Window{...};`: after the swap the temporary holds the old window. Its destructor calls `glfwTerminate()`, which destroys *all* windows, including the one now in `a`. `a.update()` then polls a terminated library through a dangling handle.
  - The same happens whenever two `Window`s coexist (an editor tool window, a test) and one is destroyed.
  - On a machine with no display, `glfwInit` fails. The engine asserts, or crashes in release, when it could report an error or fall back to headless.
- **Severity:** bug. It is latent today: nothing move-assigns a `Window`, and `Engine` holds one by value.
- **Confidence:** high
- **Affects:**
  - Extensions: a multi-window editor, the headless fallback in the `AppDesc` proposal, a platform test target.
  - Tests: if creation returns `expected`, the no-display path can be tested in CI.
- **Proposal:**
  - Replace the constructor with `Window::create(Desc) -> std::expected<Window, Error>`, checking both `glfwInit` and `glfwCreateWindow`.
  - Initialize and terminate GLFW once, through a refcount or a small shared `Glfw` owner.
  - Delete move assignment; keep the move constructor.
  - Size S.

### A-CONTENT-12: GLFW leaks through `platform`'s public API into `rhi` and the engine
- **Where:**
  - `platform/window.cppm`: `glfwWindow()`, and the GLFW include in the GMF.
  - `platform/input.cppm`: the enum values are GLFW macros.
  - `platform/CMakeLists.txt`: GLFW linked PUBLIC.
  - `rhi/context.cpp`: `createSurface` (`glfwCreateWindowSurface(..., desc.window->glfwWindow(), ...)`) and `findExtensions` (`glfwGetRequiredInstanceExtensions`).
  - `rhi/swapchain.cpp` includes GLFW.
  - `engine.cppm` `createFrameGraph`: `ImGui_ImplGlfw_InitForVulkan(m_window.glfwWindow(), true)`.
- **Observed:** `rhi` includes GLFW and calls it for surface creation and instance extensions. `platform` exposes the raw handle, and every importer of `aegis.platform` gets GLFW's macros.
- **Inference:**
  - GLFW is the only backend, so a backend abstraction is not worth building.
  - Two leaks are worth closing:
    - `rhi` needs only "required instance extensions" and "create a surface", not GLFW itself.
    - The ImGui GLFW install order (agent-notes: construct `Input` before `ImGui_ImplGlfw_InitForVulkan`) is a platform concern that currently lives in the engine.
- **Severity:** polish
- **Confidence:** medium
- **Affects:** Extensions: moving ImGui ownership into `Presentation` (engine-architecture.md step 4).
- **Proposal:**
  - Add `Window::requiredInstanceExtensions()` and `Window::createSurface(opaque instance handle)`. platform must not import `vulkan_hpp`, so these use opaque handles.
  - `rhi::Context` calls them, and GLFW is dropped from `rhi/CMakeLists.txt`.
  - `glfwWindow()` stays only for the ImGui backend.
  - Size S. Not NOW.

### A-CONTENT-13: `Input` is welded to `Window` and GLFW, so its logic cannot be unit-tested
- **Where:**
  - `platform/window.cppm`: `friend class Input`, the single `std::function<void(int,int,int)> m_keyHandler`, the static `onKey`.
  - `platform/input.*`: `Input(Window&)`, `onKey`, `keyPressed`, `mouseButtonPressed`, `cursorPosition`, `bind`.
- **Observed:**
  - `Input` installs itself into the window's single handler slot through `friend` access.
  - State queries go straight to GLFW (`glfwGetKey`, `glfwGetMouseButton`, `glfwGetCursorPos`).
  - Only key events are routed. There is no scroll, character (text), cursor-delta or gamepad path. `kinematic_movement_controller.cppm` derives mouse deltas itself from `m_previousCursorPos`.
  - The binding match in `onKey` (exact modifiers, matching action) is pure logic, but it can only be reached through a live GLFW window. The only check is the Win32-only `verifyInput` in aegis-test.
  - `Input`'s doc says "must not move", yet `Window` is movable, and its move constructor carries along a handler that captures the old `Input*`.
- **Inference:**
  - Each new input kind needs a new callback and handler slot in `Window`, a new `friend` hook, and new `Input` API.
  - A second listener has no slot at all.
- **Severity:** design problem
- **Confidence:** high
- **Affects:**
  - Extensions: scroll, text input, gamepad, remapping, replaying recorded input in tests.
  - Tests: platform has zero unit tests, although tests/README.md claims coverage (map M9).
- **Proposal:**
  - `Window::update()` appends GLFW callback data to a per-frame `vector<platform::Event>`, where `Event` is a `variant<KeyEvent, MouseButtonEvent, CursorEvent, ScrollEvent, CharEvent>`. Gamepad state is polled in `update` and emitted as events.
  - `Input` becomes a GLFW-free state machine, `Input::apply(span<const Event>)`. It tracks pressed keys, the cursor delta and scroll, and dispatches bindings.
  - This removes the `friend`, the single handler slot and the "must not move" rule.
  - Input becomes doctest-able with synthetic events.
  - Size S-M.

---

## 2. Extensibility traces (A)

### A1. A new asset type through import, load, upload, scene entity and render pass (traced: a glTF skinned mesh)

| Step | File / module | Edit kind | Notes |
|---|---|---|---|
| Parse skin, joints, weights, inverse bind matrices | `gltf_loader.cpp` `loadMeshes` + new `loadSkins` | modifying | Anonymous-namespace class; every stage holds `Device`. `MeshPreprocessor::Input` has no slot for JOINTS_0/WEIGHTS_0 |
| Vertex layout | `mesh_data.cppm` `Vertex`, `mesh_preprocessor.cpp` `interleave` | modifying | One fixed `Vertex` for every mesh, matched by the shaders (scalar layout). Adding joints resizes every static mesh, or needs a second stream |
| GPU representation | new `SkinnedMesh` next to `static_mesh.cppm` | additive, but copies the ~100-line buffer/upload loop of `StaticMesh::create` | No shared "GPU buffers with addresses" helper |
| Component | `components.cppm` | additive | |
| Entity creation | `gltf_loader.cpp` `buildScene` | modifying | Joints are nodes; the node → entity map exists only inside `buildScene` |
| Engine entry | `engine.cppm` `Engine::loadModel` | modifying if options are needed | Static singleton helper |
| Animation | a new system in `scene/` | additive | |
| Draw | `static_mesh_render_system.cpp`, `gpu_driven.cpp`, `draw_batch_registry.cpp` | modifying | Views are hard-coded to `assets::Mesh` + `assets::Material` |

- **Central files that force edits:** `gltf_loader.cpp` (every glTF feature), `Vertex` in `mesh_data.cppm` (every vertex attribute), `texture.cppm` (every texture format), and the two mesh-drawing passes.
- **KTX2:** a new `Texture` factory, a new branch in `loadTextures` and `KHR_texture_basisu` handling. All are modifying edits, with no testable seam.
- **Terrain heightmap:** `loadFromFile` refuses anything but RGBA8, so it needs a new factory for R16/R32F. A patch generator could be a pure function returning `MeshData`. The terrain reaches the scene only through hand-written entity code.
- **Testable today:** only `MeshPreprocessor`.
- **Verdict: costly.**
- **Smallest change that makes it additive:** the A-CONTENT-02 split (`ModelData` → upload → instantiate). A skinned mesh then *adds* `SkinData` (optional) to `ModelData`, an importer test, a `SkinnedMesh::create` and an instantiate branch. `StaticMesh` and `Vertex` stay untouched if the skin data lives in a second stream.

### A2. A new input kind (scroll, gamepad, text)
1. `window.cppm`/`.cpp`: add a static callback and a `std::function` member, and register the callback (modifying).
2. `input.*`: new API, plus installing the handler through `friend` (modifying).
3. Engine: ImGui's GLFW backend chains to the previously installed callback, so construction order matters again (agent-notes).

- **Verdict:** acceptable to add (about 4 small edits), costly to test (nothing is reachable without GLFW).
- **To make it additive:** the event vector from A-CONTENT-13. A new kind is then one event struct, a 3-line callback and one `apply` case, all unit-testable except the callback.

---

## 3. Planned extensions (B)

### B1. Asset streaming (async loading, residency, memory budgets, queue ownership transfer)
- **Today:** none.
  - Loads are synchronous; uploads go through the graphics-queue `UploadManager`, which blocks when its ring is full (A-CONTENT-10).
  - Assets have no identity (A-CONTENT-04), so there is nothing to make resident or evict.
  - There is no GPU memory budget, and every texture holds a sampler slot (A-CONTENT-06).
- **Where it resists:**
  1. CPU work (fastgltf, stb, meshopt) is interleaved with Device calls. A worker calling `StaticMesh::create` would race the unsynchronized `ResourcePool`, `BindlessHeap`, `DeletionQueue` and `UploadManager`.
  2. `assets::Mesh` holds a `shared_ptr<StaticMesh>` that is always non-null and resident. There is no "not loaded" state and no lower-LOD state for a pass to handle.
  3. Freeing while an upload is pending is only safe because of single-queue submission order (A-CONTENT-10).
- **Changes:**

  | Change | When | Why |
  |---|---|---|
  | Split CPU import from GPU upload (A-CONTENT-02, -08) | NOW | Every content feature added before the split lands in the monolith and has to be moved later. The split is also the testability fix |
  | `AssetLibrary` identity and dedupe | WITH THE FEATURE | The key format itself is NOW (see B4) |
  | Worker pool, per-frame upload budget, residency state in components, transfer queue with ownership transfer, per-queue retire values | WITH THE FEATURE | |
  | Thread-safe `Device` | NOT NEEDED | Not needed as long as workers produce only plain data and uploads stay on the main thread. That is the reason to do the split |

### B2. Asset hot reloading
- **Today:** none. The only identity is the unread `Asset::path()`. `shared_ptr` references are copied into many places:
  - components (`Mesh`, `Material`);
  - material overrides (`MaterialValue` can hold `shared_ptr<Texture>`);
  - `Environment`;
  - user code (the examples keep `paintingTexture` and `cubeMats`).
- **What a reload must swap:**
  - Material GPU blocks reference textures by bindless *index* (`writeParameters` stores `sampledDescriptor().index`). Reloading a texture therefore means one of two things:
    - rewriting the descriptor in place with `Device::replace(ImageViewHandle, ...)`, whose `@warning` requires an idle GPU;
    - re-dirtying every material that uses the texture, which nothing can find.
  - Instance buffers reference meshes by device address (`gpu_driven.cpp` `makeInstance`). Static instances are written once, so a swapped mesh needs `sceneInitialized` to run again.
- **Changes:**

  | Change | When | Why |
  |---|---|---|
  | Key → asset identity in `AssetLibrary` | WITH THE FEATURE | |
  | In-place image swap behind the existing bindless slot, after a wait-idle | WITH THE FEATURE | Keeps every material block valid |
  | Per-frame handle indirection to avoid the wait-idle | NOT NEEDED | Reload is an editor action, so an idle wait is fine |

### B3. The asset side of terrain with runtime deformation, and of dense instanced vegetation
- **Terrain today:**
  - There is no heightmap path: decoders produce only RGBA8 or RGBA32F (A-CONTENT-08).
  - GPU-writable images do work: `Texture::create(desc)` with `usage = Storage` is what IBL uses, though the state is left to the caller.
  - Deformation itself is a GPU write and belongs to render_graph. The asset side needs an R16/R32F storage image loaded from a file, plus a CPU copy of the heightfield for physics and picking. That copy is dropped today (no CPU copy is retained).
- **Vegetation today:**
  - Mesh-level instancing works: one `StaticMesh` is shared across nodes, and Eval-Scene draws 1M cubes from one mesh.
  - Each instance is still an entity with a transform and two `shared_ptr` components.
  - Alpha masks are dropped (A-CONTENT-07). There are no mesh LODs.
- **Changes:**

  | Change | When | Why |
  |---|---|---|
  | `ImageData` with any format and an optional retained CPU copy | NOW, as part of the split | Cheap now; later it means rewriting the factories |
  | Alpha mode and cutoff in `MaterialData`, plus a masked template | WITH THE FEATURE | |
  | An entity-free `InstanceSet` (mesh + material + transforms) | WITH THE FEATURE | Render-side design |
  | Mesh LODs (`meshopt_simplify`, `MeshData::lods`) | WITH THE FEATURE | Additive once `MeshData` is the intermediate |

### B4. The asset side of editor growth: serialization
- **Today:** a scene file has nothing to reference.
  - `Loader::load` returns an `Entity` that does not record its source; the model path is lost.
  - glTF sub-asset names (`"{mesh.name}_{i}"`) are not unique.
  - Example paths are absolute (`core::ASSETS_DIR / ...`, baked at configure time).
  - Generated assets (IBL `Environment`, `DefaultAssets`, `createMaterial` results) have no source at all.
- **Changes:**

  | Change | When | Why |
  |---|---|---|
  | Define the asset key: `AssetKey{ path relative to the asset root, '/' separators, case as on disk; subIndex }`. Store it on instantiated roots (`ModelSource` component); `loadModel` takes a relative path | NOW | The key format is what scene files persist. Changing it after the first scenes are saved means a migration, and case-sensitive Linux paths make it worth settling once |
  | Material as data (template name + overrides) | WITH THE FEATURE | Falls out of A-CONTENT-02 |
  | UUID asset database or on-disk import cache | NOT NEEDED | Paths suffice until the editor can rename assets |

---

## 4. Target shape for `aegis.assets`

```
aegis.assets (no aegis.scene, no aegis.core)
├─ import (CPU only, no Device, unit-tested)
│   MeshData + MeshPreprocessor (exist) · ImageData + decodeImage/decodeHdr(/decodeKtx2)
│   MaterialData (factors, texture indices, alpha) · ModelData{meshes, images, materials, nodes}
│   importGltf(path|bytes), importObj(path)
├─ gpu (thin, Device-owning)
│   StaticMesh::create(Device&, const MeshData&) (exists) · Texture::create(Device&, const ImageData&, sampler)
│   MaterialTemplate/MaterialInstance (setParameter -> expected) · upload(Device&, ModelData&&, defaults) -> Model
└─ AssetLibrary (later): AssetKey -> shared_ptr cache, dedupe, reload hook
instantiate(Registry&, const Model&) -> Entity lives in the scene glue (engine / World), not here
```

- **Split CPU import from GPU upload: yes, NOW.**
  - It is not over-engineering: it replaces code (the anonymous `GltfImport`, the stb-in-factory paths) instead of adding a layer, and the intermediates already half-exist (`MeshData`, `MeshPreprocessor::Input`).
  - What it enables:
    1. Doctest cases for glTF mapping, topology rejection, format choice, the node hierarchy and OBJ parsing, all without a GPU.
    2. A worker thread later, without touching `Device`.
    3. Additive content features (trace A1).
  - Size M, mostly `gltf_loader.cpp` and `texture.cpp`.
- **Assets referenced through an `AssetLibrary`: yes, as a cache over `shared_ptr`, WITH THE FEATURE; the key format is NOW (B4).**
  - engine-architecture.md's `AssetLibrary` (defaults, IBL, loaders) is already justified by removing the `Engine` statics.
  - Adding a key → asset map to it buys dedupe: the HDR loaded six times in Eval-Scene, and images shared between glTF textures.
  - It is the prerequisite for reload and serialization: ~50 lines, testable with a fake loader.
- **Generational asset handles (`AssetHandle<T>`): NOT NEEDED now.**
  - RHI handles exist for pooled, deferred Vulkan destruction, and the deletion queue already gives `shared_ptr` assets that property.
  - Handles would pay off only for streaming residency, or for reload without an idle wait. Both can be retrofitted behind `AssetLibrary` without disturbing the import/upload split.
- **Not proposed:**
  - a polymorphic `Asset` base or type registry;
  - an abstract loader plugin interface (two formats and an extension switch are clearer);
  - a thread-safe `Device`.

---

## 5. What works well and should not change
- **`MeshPreprocessor` + `MeshData`:** a pure CPU stage with plain-value input and output and `expected` validation, covered by 6 tests. It is the model for the rest of import.
- **`assets::Error`:** one code enum, a message and an optional `rhi::Error`, with two `makeError` overloads. Lower-layer errors keep their detail.
- **Failure cleanup in `StaticMesh::create` / `Texture::create`:** partial resources are freed on every error path, and the deletion queue makes that safe from anywhere.
- **Materials as bindless + device-address data** (`MaterialTemplate` scalar layout, tested `writeParameters`): a new template is data, not a new pipeline layout.
- **GPU meshes shared across glTF nodes:** instancing works without extra machinery.
- **`platform` as a leaf module with no aegis imports,** and `Window` tracking the framebuffer size for the swapchain. Keep GLFW as the single backend; don't abstract it.
- **Headless never touching GLFW** (`rhi/context.cpp` `findExtensions`): this is what lets `aegis-test --headless` and CI run without a display. Preserve it through A-CONTENT-11 and -12.
