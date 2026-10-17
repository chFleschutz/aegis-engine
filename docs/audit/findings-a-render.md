# Findings A: render stack, architecture lens

Phase 2 (analysis, read-only). Scope: `src/aegis/rhi`, `src/aegis/renderer`, `src/aegis/render_graph` (incl. `passes/`). Consumers used as evidence: `src/aegis/engine.cppm`, `src/aegis/test/main.cpp`, `tests/rhi`, `tests/render_graph`, `src/aegis/editor/panels/scene_panel.cppm`. Paths are under `src/aegis/` unless stated otherwise. Known items from `docs/agent-notes.md` and the map's disagreements (M1-M16, D1-D12) are used as evidence and not reported again. Every fact below was re-read in code; the map was not relied on unchecked.

## 1. Findings

### A-RENDER-01: GPU-driven static instances are a snapshot, while batch counts follow the live scene
- **Where:** `render_graph/passes/gpu_driven.cpp` `InstanceUpdatePass::sceneInitialized`, `CullingPass::execute`; `render_graph/draw_batch_registry.cpp` `onMaterialConstruct/onMaterialDestroy/updateOffsets`; `shaders/gpu-driven/culling.slang` `main`; `editor/panels/scene_panel.cppm` `drawEntityActions`; `engine.cppm` `Engine::loadModel`.
- **Observed:** `StaticInstances` and `m_staticCount` are written only in `sceneInitialized`. `DrawBatchRegistry` reacts to `on_construct`/`on_destroy<assets::Material>` for the scene's whole lifetime and rewrites every batch's `firstInstance`; `DrawBatches` is re-uploaded every frame from those counts. The culling shader appends without a bound: `InterlockedAdd(pc.counts[instance.drawBatch], 1, drawID); pc.visibility[batch.offset + drawID] = instanceID;` (no `drawID < batch.count` check). `VisibleInstances` and `IndirectDrawCommands` are sized to `drawBatches.instanceCount()`. The editor calls `registry.destroy(m_selectedEntity)` at runtime, and `Engine::loadModel` can add entities after `loadScene` has called `sceneInitialized`.
- **Inference:** two failures in GPU-driven mode (the default where mesh shaders exist):
  1. *Destroy a visible static mesh entity in the editor.* The registry decrements the batch, but the snapshot still holds the instance and culling still dispatches over the old `m_staticCount`. The batch then produces `count + 1` draws: the extra one overwrites the first slot of the next batch, or, for the last batch, writes one element past the end of both buffers (a GPU out-of-bounds write). The stale instance also keeps its mesh's device address. If the entity held the last `shared_ptr<StaticMesh>`, that buffer is freed once its frame retires, and later frames read freed memory.
  2. *Add a static mesh after scene init.* The registry counts it and shifts offsets, but the snapshot never contains it. It is drawn on the CPU path and silently missing on the GPU path.
  A second `loadScene` also rewrites the non-per-frame `StaticInstances` buffer in place while an earlier frame may still read it. `DynamicTag`'s comment ("can ONLY be added/removed during scene initialization") documents a restriction that nothing enforces, and the same restriction silently applies to every static entity.
- **Severity:** bug
- **Confidence:** high for the mechanism; medium for a device fault as the outcome (it depends on mesh ownership at that moment).
- **Affects:** runtime editing, streaming, terrain chunks and vegetation (all of them add and remove instances after init). The GPU-driven path has no correctness test.
- **Proposal:** (a) now, size S: clamp in the shader (`if (drawID >= batch.count) return;`). (b) Size M: replace the snapshot with one incremental CPU-side instance store that both counts and stores instances: slot-allocated, with a dirty range that `InstanceUpdatePass` uploads. Today the counting lives in `DrawBatchRegistry` and the storing in `InstanceUpdatePass`; this merges them. The store is pure data, so it is unit-testable (slot reuse, batch offsets, dirty ranges) next to `draw_batch_registry.test.cpp`. It is also the core of the smallest SceneView (section 4).

### A-RENDER-02: References returned by `Device::get` dangle when a pool grows, and `UploadManager` hits this
- **Where:** `rhi/resource_pool.cppm` `ResourcePool<T>::get/push/fetchNextSlot` (`std::vector<Slot> m_slots`, reserve 256); `rhi/device.cpp` `Device::upload(BufferHandle…)`; `rhi/upload_manager.cpp` `UploadManager::upload(const Buffer& dst…)`, `stage`.
- **Observed:** `Device::upload` passes `get(dst)` (a `const Buffer&` into `m_slots`) to `UploadManager::upload`. That function calls `stage(size)`, and for `size > overflowThreshold` (default 32 MB) `stage` calls `m_device->createBuffer(...)`, which runs `m_slots.emplace_back()` when no slot is free. Only after that does it run `copyBuffer(staging->buffer, dst, …)`.
- **Inference:** a buffer upload larger than 32 MB, made while every slot is in use and the slot vector is at capacity (256, 512, …), reallocates `m_slots`. The `dst` reference then points into freed memory, so the recorded copy targets an undefined `VkBuffer` or crashes. Large scenes reach hundreds of buffers (each mesh owns several), and terrain or vegetation data can exceed 32 MB, so this is reachable. More generally, any `const Buffer&`, `Image&` or `ImageView&` held across any `create*` call is unsafe. Today nothing beyond this case relies on it: `CullingPass` resizes before it fetches. But the rule is undocumented, and it is also what makes `Device` unsafe for multithreaded recording: `CommandBuffer` calls `m_device.get` while another thread could be creating resources.
- **Severity:** bug
- **Confidence:** high (read in code; not reproduced)
- **Affects:** asset streaming, large uploads, the multithreaded recording extension; `ResourcePool` unit tests (these can pin reference stability without a GPU).
- **Proposal:** give `rhi::ResourcePool<T>` stable slot storage: fixed-size chunks (e.g. `std::vector<std::unique_ptr<std::array<Slot, 256>>>`) or a `std::deque<Slot>`. This replaces the `std::vector<Slot>` and needs no API change. Add a unit test showing that a reference survives 1000 pushes. Size S.

### A-RENDER-03: A pass declares its resources twice, and nothing checks that the two agree
- **Where:** `render_graph/render_pass.cppm` `RenderPass::info`; `render_graph/resource_pool.cpp` `addTexture/addBuffer/addReference`, `createResources`; `render_graph/frame_graph.cpp` `compile`, `execute`, `readTimings`.
- **Observed:** every pass constructor declares its accesses with a `Usage` (`pool.addReference("Depth", Usage::DepthRead)`). `info()` then lists the same handles again, split by hand into `reads` and `writes`. `compile()` takes ordering edges and barriers only from `info()` (`passDecls`, `accesses`), but usage flags from every declaration (`createResources` loops over `m_entries`). `detail::isWrite(Usage)` already classifies each usage. `execute` calls `pass.info()` per pass per frame just for its name (building a `std::string` and two vectors). `ResourcePool`'s constructor needs `rhi::Device&` and `renderer::Renderer&`.
- **Inference:** a pass that declares an entry but forgets it in `info()`, or lists a write under `reads`, compiles cleanly and gets the right usage flags, but gets no ordering edge or barrier. That hazard surfaces only if sync validation happens to catch it on a device run, and CI never runs one. Every new pass repeats the list. The scheduler's inputs (`PassDecl`, `EntryDecl`, `Access`) are pure, yet assembling them requires a live `ResourcePool`. So `resolveReferences`, usage merging, the error paths of `compile()`, and the end-to-end sort-plus-plan have no unit test. `tests/render_graph/schedule.test.cpp` tests only hand-built inputs.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new pass; unit tests of graph compilation.
- **Proposal:** have `FrameGraph::add<T>` set the current pass index before it constructs `T`, so that each `add*` call records the access for that pass, and derive reads and writes from `isWrite(usage)`. `RenderPass::info()` becomes `name()` (returning `std::string_view`). Split `ResourcePool` into a device-free declaration table (entries, names, usages, per-pass accesses, reference resolution, usage merging, extent/format queries of desc) and the realized resources. `compile()`'s logic then runs in a test with no Device. Size M, mostly mechanical across about 12 passes.

### A-RENDER-04: Passes are wired by unchecked name strings and duplicate each other's formats
- **Where:** `render_graph/resource_pool.cpp` `resolveReferences`; `passes/geometry_pass.cpp` and `passes/gpu_driven.cpp` constructors; `passes/screen_passes.cpp` `SkyBoxPass::createResources`; `passes/post_effects.cpp` `TransparentPass::createResources`; `passes/ui_pass.cpp` `UiPass::createResources`.
- **Observed:** references resolve by string, and `resolveReferences` takes the first entry with a matching name (`if (... m_entries[i].name == entry.name) { reference->target = i; break; }`). Duplicate creators are not rejected (map D7). `GeometryPass` and `GpuDrivenGeometryPass` each declare `Position/Normal/Albedo/ARM/Emissive/Depth`, and the second reaches into `GeometryPass::gBufferFormats`. Passes that reference a texture hard-code its format: SkyBox and Transparent use `RGBA16_SFLOAT` and `D32_SFLOAT`, and UI uses `vk::Format::eR16G16B16A16Sfloat` with the comment `// "Final", see PostProcessingPass`. After `ResourcePool::createResources`, the real format could be read through the pool, but there is no `format(handle)` accessor. The magic strings appear in 7 pass files.
- **Inference:** adding both geometry passes, or a pass that re-creates `SceneColor`, silently wires readers to whichever entry was declared first. Changing `SceneColor` to another format (e.g. R11G11B10) needs edits in four files, and a missed one is a pipeline/attachment format mismatch caught only by validation on a device run. A typo in a reference name is caught at `compile()` (good), but a duplicate is not.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new pass that consumes existing targets (terrain and vegetation G-buffer writers, TAA, SSAO); format changes; a test can cover duplicate detection once A-RENDER-03's declaration table exists.
- **Proposal:** (1) make `resolveReferences` return an error for two creators of one name (S, testable). (2) Add `ResourcePool::format(handle)` and use it in `createResources` instead of literals (S). (3) Put the shared names in one `render_graph::names` constant block (or small typed constants such as `inline constexpr std::string_view sceneColor{"SceneColor"}`) next to `addDefaultPasses` (S). No new abstraction is needed.

### A-RENDER-05: The graph cannot express persistent images or read-before-overwrite
- **Where:** `render_graph/schedule.cpp` `sortPasses`, `planTransitions`; `render_graph/schedule.cppm` doc of `Transition`.
- **Observed:** `planTransitions`: "An image's first access each frame transitions from undefined" (`transitions[p].push_back({ resource, std::nullopt, usage })`). `sortPasses` adds edges only from readers and writers to the *last* producer. The doc says "readers depend on the last producer", and there is no write-after-read edge. Buffers, by contrast, keep their contents: their first access syncs from last frame's final access. There is no way to declare an imported or externally owned resource. The frame target reaches passes through `FrameInfo::target`, outside the graph, and `PresentPass` transitions it by hand.
- **Inference:** (a) Any image whose content must survive a frame is discarded: a TAA or history buffer, a terrain heightmap deformed by compute, a vegetation density map, an accumulated shadow cache. (b) A pass that must read a resource *before* another pass overwrites it in the same frame (ping-pong, history copy) is placed after the writer, because readers always follow the last producer. (c) A resource owned outside the graph (e.g. terrain's heightmap, an asset texture written by compute) can only be used with hand-written barriers and with no ordering. These are the first things the planned terrain and TAA-style features need. The current behaviour is correct for the shipped passes.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** terrain deformation, vegetation, TAA and any temporal effect, persistent GPU-written data; schedule tests (all pure: easy to extend).
- **Proposal:** add `TextureDesc::persistent` (first access transitions from the previous frame's last usage, as buffers already do), add a read-then-write ordering rule (a reader declared before a writer in declaration order runs before it), and add `ResourcePool::import(name, Texture|BufferHandle, initialUsage)` for external resources. All three are local to `schedule.cpp`/`resource_pool.cpp` and unit-testable in `schedule.test.cpp`. Size S-M, WITH THE FEATURE (terrain or TAA), not before.

### A-RENDER-06: Graph textures are owned by the Renderer, so the graph cannot be rebuilt and resize is not testable
- **Where:** `renderer/renderer.cpp` `createResolutionDependentTexture`, `resize`; `renderer/renderer.cppm` `ResolutionDependentResource`; `render_graph/resource_pool.cpp` `~ResourcePool`, `createResources`; `render_graph/frame_graph.cpp` `compile`.
- **Observed:** `ResourcePool::createResources` sends every `{0,0}`-extent texture to `m_renderer.createResolutionDependentTexture`, which appends it to `m_resDependent`. `~ResourcePool` frees only `ownedByPool` textures, and `Renderer` has no API to drop an entry, so these textures are never freed before Device teardown. `scaleFactor` is always the literal `1.0`. `Renderer::resize` recreates them with `Device::replace` and reports failure only with `std::println(... ); continue;` (it returns `void`). `compile()` is "call once": a second call appends more `m_timers`.
- **Inference:** destroying and rebuilding a `FrameGraph` while the Device lives leaks every resolution-dependent image and view, and leaves the Renderer resizing orphans forever. That rebuild is the natural way to switch GPU-driven on or off at runtime, change the pass list, or reload passes. The rule "a graph texture of extent X at scale s" is graph knowledge that sits in the Renderer, mixed with swapchain pacing. It also forces `ResourcePool` to depend on `Renderer`, which is one reason the pool is not testable (A-RENDER-03).
- **Severity:** design problem
- **Confidence:** high (the leak is read in code; the rebuild scenario is not exercised today)
- **Affects:** runtime pipeline switching, shader/pass hot reload, transient aliasing (which needs the graph to own allocations), resize logic tests.
- **Proposal:** `render_graph::ResourcePool` owns all graph textures, frees them in its destructor, and recreates resolution-dependent ones in `FrameGraph::resized(extent)`. The extent math (`extent * scale`, clamping to 1) becomes a pure `detail` function with a test. `Renderer` keeps only its targets (swapchain or offscreen), and `createResolutionDependentTexture` goes away (`aegis-test` `verifyRendererTextures` moves to the graph). `ResourcePool` then needs `Device&` and an extent, not `Renderer&`. `resize` returns `expected`. Size M.

### A-RENDER-07: Passes read and write the ECS directly, and reach the draw-batch registry by two routes
- **Where:** `render_graph/render_pass.cppm` `FrameInfo{… aegis::scene::Scene& scene; DrawBatchRegistry& drawBatches; …}`, `RenderPass::sceneInitialized(…, Scene&)`; `passes/render_system.cppm` `RenderContext`; `passes/scene_update_pass.cpp` `execute`; `passes/lighting_pass.cpp`, `screen_passes.cpp` `SkyBoxPass::execute`, `static_mesh_render_system.cpp`, `post_effects.cpp` `PointLightRenderSystem::render`, `gpu_driven.cpp` `InstanceUpdatePass`, `CullingPass::execute`.
- **Observed:** seven pass or system bodies run `registry.view<…>()` / `registry.get<…>()`. `SceneUpdatePass::execute` *writes* to the scene: `camera.aspect = …;  // The camera system computes the projection on the next scene update`. Material CPU state is mutated during recording (`material.instance->update(frameIndex)` in `collectInstances` and `StaticMeshRenderSystem::render`). `InstanceUpdatePass` holds `const DrawBatchRegistry&` from its constructor, while `CullingPass` and `GpuDrivenGeometryPass` read `frameInfo.drawBatches`: the same object by two routes. To render anything through the graph, `aegis-test` (`createScene`) has to build an ECS scene with camera, lights and environment entities. (The dependency direction itself is known from engine-architecture §6; the write-back, the mutation during recording and the double route are not listed there.)
- **Inference:** every new pass written against `Scene&` adds to the cost of the SceneView retrofit. The ECS packing logic inside passes (light uniforms, instance data, camera data) can't be unit-tested without an entt registry and a GPU. The write-back means the camera's projection lags one frame behind a resize, and it is a data race once passes record in parallel or the scene updates on another thread.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new pass (terrain, vegetation, shadows); parallel recording; render tests without a scene.
- **Proposal:** see section 4: a minimal per-frame `FrameScene` value (camera, lights, environment descriptors, instance store from A-RENDER-01) filled by the app before `execute`. Passes take it from `FrameInfo` instead of `Scene&`. The aspect ratio is set by the app from `renderer.extent()` before the scene update. Size M.

### A-RENDER-08: Pipelines are not pooled and nothing can be retired except pooled resources
- **Where:** `rhi/pipeline.cppm` `Pipeline` (by-value `vk::raii::Pipeline`); `rhi/device.cppm` `createPipeline` (returns `Pipeline`), private `retireValue`, private `m_deletionQueue`; all passes (`std::optional<rhi::Pipeline> m_pipeline` set in `createResources`); `render_graph/shader.cpp` `loadShader`, `createComputePipeline`, `createGraphicsPipeline`.
- **Observed:** buffers, images, views and samplers are pooled with `replace()` and deferred deletion. Pipelines, query pools and command buffers are plain RAII values owned by whoever created them. `DeletionQueue::defer` exists, but `Device` keeps the queue and `retireValue()` private. Shader paths are string literals inside passes (`"bloom/downsample.slang.spv"`), and nothing maps a shader file to the pipelines built from it.
- **Inference:** shader hot reload has no safe way to swap a pipeline while frames are in flight except `waitIdle`. It also has no index to find the pipelines a changed `.spv` affects: every pass would need its own reload code. Pooling pipelines would reuse the existing pattern (handle + `replace` + deferred deletion) instead of inventing a new one.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** shader hot reload, material permutations, async pipeline compilation.
- **Proposal:** WITH THE FEATURE. `PipelineHandle` in a `ResourcePool<Pipeline>` inside `Device`, `Device::replace(PipelineHandle, Desc)` deferring the old pipeline, and `CommandBuffer::bindPipeline(PipelineHandle)`. A small `render_graph::ShaderLibrary` records (path → handles, descs) when passes create pipelines through it, which is the only new indirection. A public `Device::retire(T&&)` (one line over `m_deletionQueue.push(retireValue(), …)`) covers other non-pooled objects. Size M; passes change only their `m_pipeline` type.

### A-RENDER-09: The queue model is single-queue in fact, but exposes three queues that may alias one VkQueue
- **Where:** `rhi/device.cpp` `Device::create`, `queryQueueFamilies`, `createDevice`, `createQueue`, `retireValue`; `rhi/queue.cppm/.cpp` `Queue::submit`; `rhi/device.cpp` `UploadManager::create(*result, result->graphicsQueue().family(), {})`; `render_graph/immediate.cppm` `submitImmediate`.
- **Observed:** when a device has no dedicated family, compute and transfer "fall back" to the graphics family. `createQueue` then calls `device.getQueue(family, 0)` for each, so `m_graphicsQueue`, `m_computeQueue` and `m_transferQueue` wrap the same `VkQueue`, each with its own timeline semaphore and `m_currentValue`. Each family gets `queueCount = 1`. `retireValue()` is `m_graphicsQueue.lastSubmitted() + 1`. `Queue::submit` has two fixed forms: one binary wait at `eColorAttachmentOutput` plus one binary signal, or none. It cannot wait on another queue's timeline. Uploads, immediates and frames all go to the graphics queue. `computeQueue()`/`transferQueue()` have no callers.
- **Inference:** today this is harmless because nothing uses the other queues. The first async-compute or transfer code would hit three traps. Work "on the compute queue" may run on the graphics `VkQueue` under a different timeline. Resources it frees are retired on the graphics timeline only, so they can be released while compute still uses them. And it cannot express the cross-queue wait it needs. Exposing the queues suggests a capability that isn't there.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** async compute, transfer-queue streaming, multithreaded submit (`VkQueue` needs external synchronization, and the aliases make that invisible).
- **Proposal:** NOW, size S: stop exposing `computeQueue()`/`transferQueue()` (or document that they may alias graphics, with an `isDedicated()`), so nobody builds on them. WITH THE FEATURE: a general `Queue::submit(SubmitDesc{cmds, waits{semaphore,value,stage}[], signals[]})`, and retire values as a small per-queue vector (`DeletionQueue` entries keyed by `{queue, value}`). See section 3.

### A-RENDER-10: Materials have no pipeline, and the two geometry paths disagree on what to draw
- **Where:** `assets/material.cppm` `MaterialTemplate` (doc: "Pipelines are not part of it"), `MaterialType{Opaque, Transparent}`; `render_graph/passes/static_mesh_render_system.cpp` `render` (filters `type() != m_type`); `render_graph/default_graph.cpp` (`addRenderSystem<StaticMeshRenderSystem>()` only for opaque; Transparent gets only `PointLightRenderSystem`); `render_graph/passes/gpu_driven.cpp` `GpuDrivenGeometryPass::execute` (one `m_pipeline` for every batch); `rhi/pipeline.cppm` `GraphicsDesc` (no topology/polygon mode/MSAA/depth bias/specialization constants).
- **Observed:** `DrawBatchRegistry` batches by `MaterialTemplate`, but the GPU-driven pass draws all batches with the same pipeline, and the batch grouping changes only the push constants. The CPU path draws only `MaterialType::Opaque`. The GPU path draws every batch, including transparent ones, into the G-buffer. Nothing in the repo creates a `Transparent` material today (searched), so the divergence is latent.
- **Inference:** the first transparent or alpha-tested material, or any second shading model, has nowhere to go. It would be invisible on one path and drawn opaque on the other, and a per-template pipeline needs a decision about who owns it (pass, template or cache). The `GraphicsDesc` gaps (lines for debug draw, depth bias for shadows, specialization for permutations) each need an edit in `pipeline.cppm` and `pipeline.cpp`, but those edits are additive.
- **Severity:** design problem
- **Confidence:** medium (the divergence is real; its impact waits for the first non-opaque material)
- **Affects:** material system, permutations, shadows, vegetation (alpha test), debug rendering.
- **Proposal:** WITH THE FEATURE: a template carries a shading key (`MaterialType` plus shader variant). A pass-owned map key → pipeline (built through A-RENDER-08's library) is looked up per `DrawBatch` in the GPU loop and per entity in the CPU system. Both paths then filter by the same key. Sizes: S for the filter fix, M for keyed pipelines.

### A-RENDER-11: Descriptor writing is duplicated in Device
- **Where:** `rhi/device.cpp` `Device::createImageView(ImageHandle,…)`, `createImageView(vk::Image,…)`, `replace(ImageViewHandle,…)`, `free(ImageViewHandle)`, `createSampler`.
- **Observed:** the Sampled/Storage descriptor-writing block appears verbatim in both `createImageView` overloads and again with `oldDescriptor` in `replace`. Freeing mirrors it in `free`. Each copy maps a failure to `ErrorCode::InitializationFailed` and drops `BindlessError::OutOfMemory`.
- **Inference:** a new descriptor kind (e.g. an input attachment, or an acceleration structure in the heap) must be added in four places in `device.cpp`, plus `BindlessHeap`, `ImageView` and `bindless.slang`. Missing one copy gives a view without a descriptor and no error. "Heap full" is reported as an init failure, so a streaming system cannot tell it apart from a real error.
- **Severity:** polish
- **Confidence:** high
- **Affects:** new descriptor kinds; error handling for bindless exhaustion.
- **Proposal:** one private `writeDescriptors(ImageView&, const Desc&, const ImageView* old)` and one `freeDescriptors(const ImageView&)` in `Device`, plus an `ErrorCode::OutOfDescriptors`. Size S.

### A-RENDER-12: Render-system plumbing is copied and scene-bound
- **Where:** `render_graph/passes/geometry_pass.cppm` and `post_effects.cppm` (identical `addRenderSystem<T>` templates and `m_renderSystems` loops in `createResources`/`execute`); `passes/render_system.cppm` `RenderContext{… scene::Scene& scene …}`.
- **Observed:** the two `addRenderSystem` templates and the create/render loops are line-for-line duplicates. `RenderContext` carries `Scene&`.
- **Inference:** a third pass with systems (shadow or depth prepass, terrain) copies it a third time. The concept is fine; the duplication is not.
- **Severity:** polish
- **Confidence:** high
- **Affects:** shadow, terrain and vegetation passes that host systems.
- **Proposal:** one small `RenderSystemList` member type (add, createResources(targets), render(ctx)) used by both passes. Size S. Change `Scene&` in `RenderContext` together with A-RENDER-07.

## 2. Extensibility traces (A)

### 2.1 A new render pass
Example: an SSAO compute pass between Geometry and Lighting.
- **Files touched:**
  - New: `render_graph/passes/ssao_pass.cppm` + `.cpp` (additive), `shaders/ssao.slang` (additive).
  - Modified: `render_graph/render_graph.cppm` (one `export import :ssao_pass;`), `render_graph/CMakeLists.txt` (source list), `render_graph/default_graph.cpp` (one `graph.add<SsaoPass>()`), plus the consumer that should read the result (`LightingPass` constructor + push constants + `lighting.slang`).
  - Central files that force edits: `render_graph.cppm` (umbrella), `default_graph.cpp`, the render_graph `CMakeLists.txt`. All three edits are one line each.
- **Boilerplate:**
  - The class: constructor, `info()`, `createResources`, `execute`, members for each handle.
  - Each resource is declared twice (constructor + `info()`, A-RENDER-03) and named by a string literal (A-RENDER-04).
  - A local `PushConstants` struct is mirrored by hand in Slang.
  - The pipeline goes through `createComputePipeline(device, name, "ssao.slang.spv")`.
- **Wiring:** `pool.addReference("Depth", Usage::SampledCompute)`, `pool.addReference("Normal", …)`, `pool.addTexture("AO", Usage::StorageWrite, {.format=R8_UNORM})`. Order and barriers come from `compile()`. Lighting reads `"AO"` by name.
- **Unit-testable today:** only the schedule outcome, if inputs are hand-built (`schedule.test.cpp`). The pass's declarations, its integration and its push-constant packing are not testable.
- **Verdict:** acceptable. Adding is local and mostly additive, but the double declaration and the string/format coupling make wrong wiring silent.
- **Smallest change to make it additive and safe:** A-RENDER-03 (single declaration), A-RENDER-04 (duplicate check, `format(handle)`). Optional: let `addDefaultPasses` take an extension hook. Not needed until users build their own graphs.

### 2.2 A new GPU resource type or bindless descriptor kind
**(a) A new image kind** (e.g. 3D storage volume, array texture):
- Usually only enum entries: `ImageType`/`ImageViewType` in `rhi/common.cppm` and `toVulkan` cases in `rhi/vulkan_conversions.cppm`.
- `Image::Desc` already has `type`/`arrayLayers`.
- In the graph, `TextureDesc` lacks type, depth and layers, so a graph-owned 3D or array texture needs fields in `render_graph/resource.cppm` plus the mapping in `ResourcePool::createResources`.
- Unit-testable: the extent and footprint helpers (`detail::mipExtent`, `subresourceFootprints`). Conversions are not, because `vulkan_conversions` is not re-exported (a deliberate trap #1).
- Verdict: cheap.

**(b) A new descriptor type in the heap** (e.g. acceleration structure, uniform texel buffer):
- Edits:
  - `DescriptorType` (`common.cppm`)
  - `BindlessHeap::Desc` + binding constant + `createLayout`/`createPool`/write/free functions + a `FreeList` (`bindless_heap.cppm/.cpp`)
  - the limit clamp in `Device::create`
  - descriptor writing in `Device` (four places, A-RENDER-11)
  - the owning resource class (new `:acceleration_structure` partition with `Desc`, private `create`, `friend Device`)
  - a new `ResourcePool<T>` member + `create/get/free/replace` overloads on `Device` (four more overloads on the hub)
  - `ResourceHandle` alias
  - `shaders/modules/bindless.slang` binding.
- An acceleration structure also needs features and extensions: `Device::createFeatureChain`, `queryCapabilities`, `queryExtensions` (the empty `Device::Desc` cannot request them), plus `CommandBuffer` build commands and a graph `Usage` for AS build/read (four switch tables in `resource.cppm`).
- Central files forcing edits: `device.cppm/.cpp` (hub), `bindless_heap.cpp`, `common.cppm`, `resource.cppm`.
- Unit-testable: `FreeList` and the new handle alias. The heap's slot management sits behind `vk::raii`.
- Verdict: costly but bounded: about 10 files, mostly appending.
- Smallest change: A-RENDER-11 (deduplicate writes). Then, only if a third or fourth pooled type appears, a generic `Device::create<T>(T::Desc)` / `free<T>` over a tuple of pools. Not now: four overloads is still readable, and the abstraction would not pay for itself yet.

### 2.3 The render side of "a new shader and its CPU/GPU data interface"
- **Pipeline creation:** `createComputePipeline(device, name, path)` or `createGraphicsPipeline(device, path, GraphicsDesc)`. There is no layout to define (one shared layout), so a new shader costs nothing on the descriptor side. Good.
- **Push constants:** a struct local to the pass, `static_assert(sizeof <= 128)`, `cmd.pushConstants(struct)`. The Slang struct is a hand-written mirror, and nothing checks offsets (build-side topic; not repeated).
- **Addresses and descriptors:** buffers via `pool.bufferAddress(handle, frameIndex)`; images via `device.get(pool.texture(h).view).storageDescriptor()->index`. That access path is repeated in three files as a local `storageIndex` helper (`lighting_pass.cpp`, `screen_passes.cpp`, and inline in `post_effects.cpp`). Per-frame data goes through `addBuffer(..., HostWrite, {.perFrame=true})` + `pool.write`.
- **Files touched:** the pass `.cpp` (struct + call), the shader. A shared struct (e.g. `CameraData`) lives in a pass partition interface (`scene_update_pass.cppm`), and changing it rebuilds every importer.
- **Unit-testable:** `sizeof` checks only. Packing logic (`LightingPass::Uniforms` assembly, `makeInstance`) is inside `execute` and reads the ECS (A-RENDER-07).
- **Verdict:** cheap.
- **Smallest improvement:** `ResourcePool::storageDescriptor(h)` / `sampledDescriptor(h)` helpers, which replace the three local `storageIndex` copies (S). Move the packing of CPU data into pure functions that take plain values (S each, once the FrameScene exists).

## 3. Planned extensions (B)

### 3.1 Async compute and multi-queue in the render graph
- **Exists today:** queues are created (possibly aliased, A-RENDER-09); timeline semaphores exist per `Queue`.
- **Where it stops:**
  - The graph records every pass into the one `CommandBuffer` the `Renderer` passes in (`FrameGraph::execute`).
  - `planTransitions` assumes one queue ("same queue, so a barrier suffices").
  - Barriers use `QueueFamilyIgnored`.
  - `retireValue` is graphics-only.
  - `Queue::submit` cannot wait on a timeline.
- **Where the design resists:** `Renderer::renderFrame` owns the submit, so the graph cannot split a frame into several submits.
- **Changes:**
  - NOW: hide or document the aliasing queues (A-RENDER-09, S).
  - WITH THE FEATURE:
    - a `RenderPass` queue hint (`Graphics|AsyncCompute`)
    - a scheduler step that partitions passes into per-queue submit groups and emits cross-queue timeline waits plus queue-family ownership transfers for exclusive resources (or creates resources `Concurrent`)
    - a general `Queue::submit(SubmitDesc)`
    - `Renderer` hands the graph a "frame submitter" instead of one command buffer
    - per-queue retire values in `DeletionQueue` (`{queue, value}`; `collect` per queue)
    - `UploadManager` keeps its own queue.
  - All of the scheduling part is pure and goes in `schedule.cpp` with tests.
  - NOT NEEDED: a generic job system in rhi.

### 3.2 GPU-driven rendering (culling, LOD, indirect for all geometry)
- **Exists today:**
  - `InstanceUpdatePass` → `ClearDrawCountsPass` → `CullingPass` (frustum culling per instance, compute) → `GpuDrivenGeometryPass` (task/mesh shaders, `drawMeshTasksIndirectCount` per batch).
  - Meshlet culling module exists (`shaders/modules/meshlet_cull.slang`).
  - Only static meshes with one shared pipeline.
- **Where it stops:**
  - No occlusion culling (no depth pyramid, which would also need persistent images, A-RENDER-05) and no LOD selection.
  - Mesh-shader-only: devices without mesh shaders fall back to the per-entity CPU loop.
  - `CommandBuffer` has no `drawIndexedIndirect(Count)` or `dispatchIndirect`.
  - Static instances are a snapshot (A-RENDER-01).
  - Transparent and alpha-tested materials are not separated (A-RENDER-10).
  - `maxDrawBatches = 256` is enforced by `assert` only (map 3.3): more templates in Release index past `DrawBatches` and `IndirectDrawCounts`.
- **Changes:**
  - NOW: shader bound check plus incremental instance store (A-RENDER-01); turn the `maxDrawBatches` assert into a returned error or a growth (S).
  - WITH THE FEATURE: indirect commands in `CommandBuffer` (additive); LOD data in `StaticMesh::GpuData` and selection in culling; HiZ (needs persistent images); per-batch pipelines (A-RENDER-10).
  - NOT NEEDED: a second instance format for the CPU path. The CPU path can read the same instance store.

### 3.3 Render graph resource tracking (barrier completeness, transient aliasing)
- **Exists today:** whole-resource state per declared access; images from Undefined at first use; buffers synced across frames; tested in `schedule.test.cpp` (9 cases).
- **Gaps:**
  - Undeclared accesses are invisible (A-RENDER-03).
  - No subresource (mip/layer) tracking: `BloomPass` transitions its mips by hand and must "Leave the state the graph expects".
  - The frame target is outside the graph.
  - Storage access is compute-only (`StorageRead/Write` → `ComputeStorage`), so a fragment or vertex shader writing storage images (decals, terrain painting) has no `Usage`.
  - Every transition is its own `pipelineBarrier2` call (`FrameGraph::recordTransitions` → `transitionImageLayout`/`memoryBarrier`), so nothing is batched.
- **Transient aliasing:** absent. Every resource gets its own VMA allocation, and resolution-dependent ones belong to the Renderer (A-RENDER-06).
- **Changes:**
  - NOW: A-RENDER-03 (single declaration), because every new pass written in the old style adds to the migration.
  - WITH THE FEATURE: aliasing. It needs (a) graph-owned textures (A-RENDER-06), (b) lifetimes from `m_order` (pure, testable), (c) placed allocations in `rhi` (`Allocator` + `Image::create` from an existing allocation).
  - Also WITH THE FEATURE: batched barriers (`CommandBuffer::barriers(span)`, S), graphics-stage storage usages (one enum value + four switch tables), and subresource ranges, when a second mip-chain pass appears.

### 3.4 Material and pipeline systems (pipeline libraries, permutations)
- **Exists today:** templates and instances with parameter blocks reached through BDA; one shared layout. That shared layout is a large simplification for permutations: no layout compatibility to manage.
- **Where it stops:** templates name no shader; pipelines are created ad hoc per pass and are not pooled; `GraphicsDesc` lacks specialization constants, topology and depth bias; there is no pipeline cache (`VkPipelineCache`) and no graphics pipeline library.
- **Changes:**
  - NOW: none. Strictly, nothing blocks yet, and retrofitting is about 10 `m_pipeline` members.
  - WITH THE FEATURE: pooled pipelines + shader library (A-RENDER-08), a shading key on templates (A-RENDER-10), `GraphicsDesc` additions, `VkPipelineCache` persisted to disk.
  - NOT NEEDED: `VK_EXT_graphics_pipeline_library`. With one layout, no vertex input and dynamic rendering, full pipelines compile fast enough until permutation counts are known.

### 3.5 Terrain with runtime deformation; dense instanced vegetation
- **Renderer needs:**
  - Instance buffers: CPU-written per frame (`HostWrite` + `perFrame`, exists) or compute-written (`StorageWrite` buffer → `ShaderRead`/`IndirectRead`, expressible today).
  - Compute-written geometry: buffers are fine, because they persist across frames. A deformed heightmap or splat map is an image that must persist, which is blocked by "first access = Undefined" and the lack of imports (A-RENDER-05).
  - Indirect draws: only the mesh-task variant exists. Vegetation without mesh shaders needs `drawIndexedIndirectCount`.
  - Alpha test: needs a pipeline variant (A-RENDER-10).
  - Instance counts in the millions: the per-frame `std::vector<InstanceData>` rebuild in `InstanceUpdatePass::execute` (CPU iteration over all dynamic entities) does not scale. Vegetation instances should be GPU-generated or uploaded once, via the incremental store of A-RENDER-01.
- **Changes:**
  - NOW: A-RENDER-01 (instance store; terrain chunks and vegetation tiles stream instances in and out).
  - WITH THE FEATURE: persistent and imported graph resources (A-RENDER-05), indirect variants, alpha-tested variant, a terrain pass using the `RenderSystem` list (A-RENDER-12).
  - NOT NEEDED: a separate "instancing" abstraction; draw batches already are one.

### 3.6 Multithreaded command recording and job-based frame work (render side)
- **Exists today:** nothing; everything is single-threaded by design (map 1.4).
- **Blockers found:**
  - `Device::get` references are unstable (A-RENDER-02), and pools, `DeletionQueue`, `BindlessHeap` free lists and `UploadManager` are unsynchronized.
  - `CommandBuffer` methods are `const` but mutate `m_graphicsSetBound` (`mutable`). That works for one thread per buffer, but it hides that recording is stateful.
  - `Renderer` owns one `CommandPool` and gives the graph exactly one `CommandBuffer`; command pools must be per thread.
  - Passes mutate shared CPU state while recording: `MaterialInstance::update`, the `Camera.aspect` write-back (A-RENDER-07), `ResourcePool::resizeBuffer` from inside `execute` (`CullingPass`, `InstanceUpdatePass`).
- **Changes:**
  - NOW: stable pool storage (A-RENDER-02, a bug fix anyway). Move CPU mutations out of `execute` into a prepare step (comes with the FrameScene). Each new pass written with mutations in `execute` makes parallel recording harder.
  - WITH THE FEATURE:
    - per-thread command pools and secondary or per-pass primary command buffers handed out by the graph
    - a mutex (or per-thread queues merged at frame end) around `Device` create/free and `DeletionQueue`
    - `UploadManager` per thread or locked
    - pass "prepare" (serial, may mutate and resize) vs "record" (parallel, read-only).
  - NOT NEEDED: lock-free pools.

### 3.7 Queue-ownership transfer / transfer queue for asset streaming
- **Exists today:** the `UploadManager` ring and overflow buffers, batching and timeline reclaim. Pure ring math is extracted and tested (`ring_allocator.test.cpp`). It is hard-wired to the graphics family at `Device::create`.
- **Where it stops:**
  - No release/acquire barriers (`QueueFamilyIgnored` everywhere), and `generateMipmaps` uses blits (not available on transfer-only queues).
  - The frame does not wait on the upload timeline. That is correct today only because uploads and frames share a queue.
  - Retire values ignore other queues.
  - `flushUploads()` is called by the app before each frame; the image upload's `finalState` transition runs on the upload queue.
- **Changes:**
  - WITH THE FEATURE:
    - `UploadManager::create(queue)` on the transfer queue
    - a release barrier at batch end, plus an acquire barrier recorded by the graphics side, e.g. the graph's first frame using the resource, or a small "pending acquires" list the Renderer drains into the frame command buffer
    - mip generation moved to graphics
    - the frame submit waits on the upload timeline value
    - per-queue retire values (shared with 3.1).
  - NOW: nothing beyond A-RENDER-02/09.

### 3.8 Render side of shader hot reload
- **Exists today:** shaders are loaded from `AEGIS_SHADER_DIR` by relative path at `createResources` (once, in `compile`).
- **Where it stops:**
  - No path → pipeline index.
  - Pipelines are not replaceable without `waitIdle` (A-RENDER-08).
  - `compile()` cannot be rerun (timers append), and rebuilding the graph leaks textures (A-RENDER-06).
  - Passes capture pipeline formats as literals (A-RENDER-04).
- **Changes:** WITH THE FEATURE: pooled pipelines with deferred replace, and the `ShaderLibrary` that records (path → pipeline handles + desc). Reload then means `library.reload(path)` → `device.replace(handle, desc)` with no pass code involved. NOW: nothing; the cost of retrofitting does not grow much.

## 4. Dependency direction: render_graph → aegis.scene, SceneView, and splitting render_graph

**Observed coupling** (import lines rechecked):
- `render_pass.cppm` (the pass base), `frame_graph.cppm/.cpp`, `passes/render_system.cppm`, `scene_update_pass`, `gpu_driven`, `lighting_pass.cpp`, `post_effects.cpp`, `screen_passes.cpp`, `static_mesh_render_system.cpp` import `aegis.scene`.
- `draw_batch_registry` imports `aegis.scene.registry`, includes entt and subscribes to entt signals.
- `FrameInfo` carries `Scene&` and `DrawBatchRegistry&`; `RenderPass::sceneInitialized` takes `Scene&`.
- The ECS is read in 7 pass bodies and written in one (A-RENDER-07).
- `aegis-tests-render_graph` links the whole legacy scene stack only for this reason (map C13).

**Is SceneView needed?** Yes, from the render side, for three concrete reasons, not for purity:
1. The GPU-driven instance bug (A-RENDER-01) is fixed by an incremental instance store, and that store *is* the main part of a SceneView.
2. Every planned feature (terrain, vegetation, shadows, TAA) adds passes. Written against `Scene&`, each one raises the retrofit cost, which engine-architecture.md already calls "touches every pass".
3. Today a pass can only be exercised by building an ECS world (`aegis-test` `createScene`), and its CPU packing logic cannot be unit-tested.

**NOW or later?** A minimal version NOW, before the new feature passes are written; the full extraction later.

**Smallest version:**
- `render_graph::FrameScene` (plain value, no entt): `CameraData camera`, light list (ambient, directional, point lights as `LightingPass` already packs them), environment descriptors (`irradiance/prefiltered/brdfLut/skybox` as `TextureDescriptor`s), `const InstanceStore& instances`.
- `InstanceStore` (render_graph, pure): slot-allocated `InstanceData` with batch bookkeeping and a dirty range. It replaces `DrawBatchRegistry`'s counting plus `InstanceUpdatePass`'s collection.
- A bridge in the app layer (`engine.cppm` today, `aegis.app` later): the entt signal handlers that were in `DrawBatchRegistry` move there and call `InstanceStore::add/remove/update`. A per-frame `extract(Scene&) -> FrameScene` fills camera and lights and sets `Camera.aspect` before the scene update.
- `FrameInfo` loses `Scene&` and `DrawBatchRegistry&` and gains `const FrameScene&`. `RenderPass::sceneInitialized` goes away (the store is incremental).
- `RenderContext` gets `const FrameScene&`. `StaticMeshRenderSystem` iterates the instance store instead of an entt view. That unifies the CPU and GPU paths over the same data and fixes the path divergence (A-RENDER-10's filter becomes one place).
- Cost: about 7 pass bodies change (mechanical), one bridge (~150 lines), and one copy per frame of camera and lights (small). Instances are not copied per frame.
- Unlocks: unit tests of `InstanceStore` and of the packing functions without entt or a GPU; `aegis-test` can render a hand-built `FrameScene`; `render_graph` and its tests stop linking `aegis-scene`; a later extract-on-one-thread / render-on-another split.

**Should render_graph be split (framework vs passes)?** Yes, but only after or together with the FrameScene. Otherwise the framework still imports `aegis.scene` through `FrameInfo` and nothing is gained.
- **Framework module** (`aegis.render_graph`): `resource`, `resource_pool`, `schedule`, `render_pass`, `frame_graph`, `error`, `shader`, `immediate`.
  - Imports `rhi` (+ `renderer` until A-RENDER-06).
  - Its tests (`schedule`, future declaration-table tests) link only that.
- **Passes module** (e.g. `aegis.render_passes`): every `passes/*`, `frustum`, `ibl`, `default_graph`, `ui_pass` (ImGui + `vulkan_hpp`), the instance store and `FrameScene`.
- **Why:**
  - (1) There is an obvious home for "add a pass" vs "change the graph".
  - (2) Framework tests stop dragging assets, scene and ImGui (C13).
  - (3) Today touching any pass interface rebuilds the umbrella's importers. With the split, editor panels that only show timings import the small framework module.
  - (4) The two exceptions to the style rules (ImGui and raw Vulkan in `ui_pass`) become confined to the passes module.
- **Why not more:** further splits (one module per pass) would only add CMake noise.
- **Size:** S once the FrameScene exists (file moves + one target).

## 5. What works well and should not change
- **One shared pipeline layout + bindless + buffer device address.** A new shader needs no descriptor-set or layout code, only a push-constant struct (trace 2.3). It also removes the hardest part of permutations (layout compatibility).
- **Generational handles + deferred deletion keyed by timeline values, including deferred return of bindless slots** (`Device::free(ImageViewHandle)`). Stale handles are detectable, freed descriptors are never overwritten while in flight, and the timeline keying extends naturally to per-queue retire values.
- **`Desc` structs + `create() -> std::expected`, dependencies passed in** (Context → Device → Renderer → FrameGraph). This is what lets `aegis-test` build the stack headless. The app proposal copies the pattern, rightly.
- **Pure scheduling extracted into `:schedule`** (`sortPasses`, `planTransitions`) with tests, and **pure helpers in `detail`** (`RingAllocator`, swapchain sizing, mip math, `grownCapacity`). This is the extract-don't-mock approach working; the same approach is proposed for the graph's declaration table (A-RENDER-03) and the instance store.
- **Declared usages drive both image usage flags and barriers.** A pass never writes Vulkan sync code (except Bloom's mip chain), and the barrier model is small enough to reason about.
- **`ResourcePool::resizeBuffer` keeps graph handles stable** while the GPU address changes, and passes fetch addresses per frame. Growable GPU-driven buffers are cheap to add.
- **Headless/offscreen `Renderer` mode.** It is the only reason device behaviour has a pass/fail signal at all; keep it first-class.
- **`UploadManager`'s non-blocking contract** (`NotReady` = flush and retry, an overflow path for large uploads, a ring of batches). It is the right shape for streaming, and needs only queue parameterization (3.7) and the A-RENDER-02 fix.
