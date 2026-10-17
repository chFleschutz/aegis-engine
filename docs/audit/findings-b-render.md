# Findings b: render stack (rhi, renderer, render_graph), bugs lens

This is a read-only analysis. Nothing was built or run. Paths are relative to `src/aegis/` unless they start with `shaders/`, `examples/` or `docs/`. **Observed** means read in the code; **Inference** means my own reasoning about how the code behaves at runtime.

The baseline matters for confidence. All 3 `device` ctest runs pass (`build/audit/logs/ctest.log`). Debug builds enable sync validation (`rhi/context.cpp`, `validate_sync`), and SYNC-HAZARD messages are reported at error severity. So every barrier path those runs reach is already clean under sync validation. The findings below are about paths the device runs never reach: failures, resize edge cases, runtime scene changes, and limits.

### B-RENDER-01: Failed swapchain recreation leaves the Renderer holding freed image views
- **Where:** `rhi/device.cpp` `Device::createSwapchain(const Swapchain::RecreateDesc&)`; `renderer/renderer.cpp` `Renderer::resize`, `Renderer::beginFrame`; `rhi/resource_pool.cppm` `ResourcePool::pop`
- **Observed:** The function frees first and creates second: `for (const auto imageView : desc.oldSwapchain.imageViews()) free(imageView); return Swapchain::create(*this, desc);`. On failure, `Renderer::resize` runs `std::println("Failed to recreate swapchain"); return;` and keeps `m_swapchain` unchanged. That old `Swapchain::m_imageViews` still holds the popped handles, and `needsRecreation()` stays true. `ResourcePool::pop/get` check generations only with `assert`.
- **Inference:** Suppose `vkCreateSwapchainKHR` fails, for example with `VK_ERROR_SURFACE_LOST_KHR`, an out-of-memory error, or a native-window-in-use error on some platforms.
  - The loop calls `resize` again on the next iteration (`needsResize()` is still true). That frees the same stale handles a second time. In debug this asserts. In release, `pop` pushes the slot onto the free list twice, so two later resources share one pool slot.
  - If a frame renders before a successful retry, `acquireNextImage` returns `m_imageViews[i]`, a stale handle. Then `Device::get` happens inside the `noexcept` `renderFrame`. In debug this asserts. In release, `std::optional::value()` throws on an empty slot, which ends in `std::terminate`. If the slot was reused, the call silently resolves to an unrelated view.
- **Severity:** bug
- **Confidence:** high for the code path; medium for how often creation fails in practice
- **Affects:** every windowed run and any future present-mode, HDR or fullscreen switch. No test reaches swapchain recreation failure.
- **Verify by:** code reading. A unit test cannot reach it (it needs a surface). A fault-injection run of windowed `aegis-test --frames 600`, forcing `Swapchain::create` to fail once, should hit the stale-handle assert on the next frame.
- **Fix sketch:** Create the new swapchain first and free the old views only on success, for example by letting `Swapchain` hand its views back to the Device in a `retire` step. Make `Renderer::resize` return `std::expected` so the caller can stop rendering.

### B-RENDER-02: `Renderer::resize` swallows per-texture failures and leaves images, views and extent out of step
- **Where:** `renderer/renderer.cpp` `Renderer::resize`; `rhi/image.cpp` `Image::create` (explicit `mipLevels` is not clamped); `render_graph/passes/post_effects.cpp/.cppm` `BloomPass` (`mipLevels{ 6 }`), `BloomPass::createMipViews`
- **Observed:**
  - The loop over `m_resDependent` uses `println` + `continue` when `replace(image)` fails, and `println` alone when `replace(view)` fails after the image was already replaced. `m_extent` is set before the loop.
  - The function has no guard against a zero extent. Both consumers filter minimize: `Window::isMinimized()` checks for a 0x0 framebuffer, in `engine.cppm` `run` and `test/main.cpp`. Offscreen `resize({0,0})` is unguarded.
  - `Image::create` uses `desc.mipLevels` as given unless it is `fullMipChain`. The "Bloom" graph texture always requests 6 mips.
- **Inference:** There are three concrete scenarios.
  1. **Small window.** The user drags the window to, say, 20x20 (not minimized). The Bloom image is recreated with 6 mips, but at most `floor(log2(20)) + 1 = 5` are valid. The result is VUID-VkImageCreateInfo-mipLevels-00958, or a creation failure that gets printed and skipped. If creation fails, "Bloom" keeps its old, larger image while SceneColor and the rest shrink. `BloomPass::resized` then rebuilds mip views on the old image, and the threshold dispatch covers the old mip-0 extent: it reads SceneColor outside its bounds and writes Bloom regions the next passes never see.
  2. **View failure.** `replace(image)` succeeds but `replace(view)` fails. The pooled view still points at the old `VkImage`, which was pushed to the deletion queue with `retireValue()`. Once that value completes, every frame samples a destroyed image.
  3. **Offscreen zero extent.** `resize({0,0})` sets `m_extent = 0`. Every image creation fails, so `FrameInfo::extent` is 0. `SceneUpdatePass` then computes `aspect = x / 0`, and the projection becomes NaN.
- **Severity:** bug
- **Confidence:** medium. Scenario 1 needs a window under 32 px in both dimensions, and I found no `glfwSetWindowSizeLimits`.
- **Affects:** resize, any resolution-scale feature (`scaleFactor` already exists), and any pass with a fixed mip count. The "image + view in one step" logic is pure and testable once extracted.
- **Verify by:**
  - Windowed `aegis-test --frames 600`, shrinking the window below 32x32. Expect a VUID-VkImageCreateInfo-mipLevels-00958 error or the "Failed to recreate resolution dependent image 'Bloom'" print.
  - Unit test candidate: a `detail::` helper that clamps a requested mip count to `calcMipLevels(extent)`.
- **Fix sketch:** Clamp `mipLevels` to the extent in `Image::create`, or have Bloom derive its mip count from the extent. Treat a zero extent as "skip frame, keep needsResize" inside `Renderer`. Return an error from `resize` instead of keeping a half-replaced set.

### B-RENDER-03: Frame-loop errors are lost, and a failed submit or present leaves sync state inconsistent
- **Where:** `renderer/renderer.cpp` `Renderer::renderFrame` (`noexcept`, returns `void`), `beginFrame`, `endFrame`; `rhi/queue.cpp` `Queue::submit`; `render_graph/passes/ui_pass.cpp` `UiPass::createResources` (`.ImageCount = 2`)
- **Observed:**
  - `renderFrame` returns nothing.
  - `beginFrame` returns `FrameBeginFailed` (for example when `graphicsQueue().wait` fails) and `renderFrame` just returns, with no print.
  - In `endFrame`, a submit or present failure prints and returns before `m_currentFrame` advances. Acquire has already signalled `imageAvailable`.
  - `Queue::submit` increments `m_currentValue` before `submit2` and keeps the increment on failure.
  - ImGui's Vulkan backend is told `ImageCount = 2`. That is a literal, not `Renderer::maxFramesInFlight`, and ImGui cycles its own vertex/index buffers on every `RenderDrawData`.
- **Inference:**
  - (a) **Device lost.** `wait` fails, so every `renderFrame` returns silently. `Engine::run` keeps looping on a frozen window, and no caller can tell. `aegis-test` would also count frames that never rendered.
  - (b) **Submit failure.** The slot keeps a signalled `imageAvailable`. The next acquire in the same slot then violates VUID-vkAcquireNextImageKHR-semaphore-01286, and the acquired swapchain image is never presented. Repeated failures run out of images.
  - (c) **Present failure** (not out-of-date, for example surface lost). The slot does not advance, but ImGui's buffer index does. ImGui's ring and the renderer's slots drift out of phase. ImGui then rewrites the vertex buffer that the other, still in-flight, frame reads. The same drift appears if `maxFramesInFlight` is ever raised above 2.
- **Severity:** bug
- **Confidence:** high that the errors are dropped; medium on how likely (b) and (c) are, since they need submit or present failures.
- **Affects:** device-lost handling, headless examples as tests (they need an exit signal), and changes to frames in flight.
- **Verify by:** code reading. Fault injection that fails one `endFrame` submit in windowed `aegis-test --frames 600` should produce VUID-vkAcquireNextImageKHR-semaphore-01286 on the next frame.
- **Fix sketch:** Have `renderFrame` return `std::expected<void, Error>` and let callers stop on fatal errors. On a failed submit, recreate the slot's `imageAvailable` semaphore. Pass `Renderer::maxFramesInFlight` to ImGui.

### B-RENDER-04: GPU-driven static instances are a one-time snapshot, while the draw batch registry follows the ECS live
- **Where:** `render_graph/passes/gpu_driven.cpp` `InstanceUpdatePass::sceneInitialized`, `collectInstances`, `CullingPass::execute`; `shaders/gpu-driven/culling.slang` `main`; `assets/material.cpp` `MaterialInstance::update`; `engine.cppm` `Engine::loadScene`
- **Observed:**
  - "StaticInstances" is a `HostWrite` buffer that is *not* per-frame. Only `sceneInitialized` writes it, baking `mesh.meshDataAddress()` and `material.parameterAddress(0)`, and it calls `material.instance->update(0)` once.
  - `DrawBatchRegistry` changes its counts on every Material construct and destroy.
  - Culling has no bound check: `InterlockedAdd(pc.counts[instance.drawBatch], 1, drawID); pc.visibility[batch.offset + drawID] = instanceID;`
  - `MaterialInstance::update(frame)` writes block `frame` only when it is dirty. Only dynamic instances call it per frame.
  - `Engine::loadScene` calls `sceneInitialized` without `waitIdle`.
- **Inference:** Each case below is a runtime ECS change after load in GPU-driven mode.
  1. **Static entity added.** The registry counts it but nothing writes it, so it is never drawn (the CPU path draws it).
  2. **Static entity destroyed.** If it held the last reference to its mesh, `StaticMesh` frees its buffers (deferred). "StaticInstances" still holds the old address, so a few frames later culling reads `mesh->bounds` and `meshletCount` from a freed buffer: GPU page fault or garbage. Meanwhile `batch.count` is one lower than the instances culling appends, so `batch.offset + drawID` runs into the next batch's range or past the buffer end.
  3. **`setParameter` on a material used only by static instances.** Nothing ever calls `update(0)` again, so the edit never reaches the GPU. If dynamic instances share the material, `update(0)` runs while slot 0 records, rewriting block 0 while the slot-1 frame, which reads block 0 through the static instances, may still be executing.
  4. **`loadScene` while frames are in flight** (scene switching). This overwrites "StaticInstances" while the GPU reads it.

  No code does any of this at runtime today: `setParameter` is only called at setup, and nothing destroys entities. Each case is the first thing an editor or gameplay feature would do.
- **Severity:** bug (latent)
- **Confidence:** high on the code facts; low that any current example triggers it.
- **Affects:** editor entity and material editing, scripting spawn/despawn, scene switching (the `docs/engine-architecture.md` proposal), and the agreement between the CPU and GPU-driven paths.
- **Verify by:** code reading. Device repro: add a `--scene --gpu-driven` variant to `aegis-test` that destroys one static entity at frame 60. Expect a device fault or a GPU-AV out-of-bounds report. Unit-testable part: a pure planner that maps registry counts to instance slots.
- **Fix sketch:** Make one source of truth. Either rebuild StaticInstances (per frame or on a dirty flag) from the registry's signals, or keep static data per-frame. Add `if (drawID >= batch.count) return;` to culling. Static instances should read the current frame's material block.

### B-RENDER-05: `DrawBatchRegistry` limits and lifecycle are guarded only in debug
- **Where:** `render_graph/draw_batch_registry.cpp` `addInstance`, `removeInstance`, `connect`; `render_graph/resource_pool.cpp` `ResourcePool::write`; `rhi/buffer.cpp` `Buffer::write`; `render_graph/passes/gpu_driven.cpp` (`DrawBatches` and `IndirectDrawCounts` sized from `maxDrawBatches`)
- **Observed:**
  - `assert(m_batches.size() < maxDrawBatches ...)` is the only limit check.
  - `ResourcePool::write` and `Buffer::write` do not bounds-check `offset + size` (`Buffer::write` only asserts that the buffer is mapped).
  - `removeInstance` dereferences `*id` after an `assert(id && ...)`.
  - `connect()` calls `disconnect()` and then recounts the registry, but never clears `m_batches`, `m_batchIds` or the counts.
- **Inference:**
  - **Batch overflow (release build).** The 257th material template makes `InstanceUpdatePass::execute` write 257 × 8 bytes into a 2048-byte per-frame stride. The frame-0 copy overwrites frame 1's copy. The frame-1 copy writes past the end of the buffer into VMA's mapped block. Culling then increments `counts[256]` past the counts buffer, and `drawMeshTasksIndirectCount` uses `countBufferOffset = 1024 = size`.
  - **Reconnect.** A second `connect` (for a new scene or registry) double-counts everything.

  Only one `MaterialTemplate` exists today (`assets/defaults.cpp` `pbrTemplate`), so none of this triggers now. Adding material types is the obvious next feature.
- **Severity:** design problem
- **Confidence:** high on the code; low on current triggers.
- **Affects:** new material templates or shader permutations, scene switching, and `draw_batch_registry.test.cpp` (cheap to extend: reconnect, the 257th batch, removing an unknown template).
- **Verify by:** unit tests on `DrawBatchRegistry` (no GPU needed). Doing `connect` twice on the same registry should keep the counts. The 257th `addInstance` should report an error instead of asserting.
- **Fix sketch:** Make `addInstance` return `std::expected` (or grow the GPU buffers through `ensureCapacity`). Clear state in `connect`. Bounds-check `ResourcePool::write` against `bufferSize`.

### B-RENDER-06: `UploadManager` and deferred deletion depend on ordering contracts that nothing states or checks
- **Where:** `rhi/upload_manager.cpp` `UploadManager::upload(const Buffer&, ...)`, `flushPending`, `reclaimFront`; `rhi/device.cpp` `Device::retireValue`, `Device::flushUploads`; consumers `engine.cppm` `run`, `render_graph/immediate.cppm` `submitImmediate`, `assets/upload.cppm` `uploadBlocking` (all `std::ignore = device.flushUploads()`)
- **Observed:**
  - A buffer upload records `copyBuffer` with no barrier before it. Only `makeTransferWritesVisible()` runs, at flush time.
  - `retireValue()` is `graphicsQueue.lastSubmitted() + 1`, which ignores the open upload batch.
  - `flushPending` returns `std::nullopt` both when there was nothing to submit and when `queue.submit` failed.
  - `reclaimFront` runs `std::ignore = queue.wait(...)` and then reclaims staging either way.
- **Inference:**
  - (a) **Write-after-read.** Uploading new data into a buffer that an in-flight frame still reads (mesh streaming, a `MaterialInstance` moved to GPU-only memory, any "update buffer" feature) is a write-after-read hazard. Nothing orders the copy after the earlier frame's reads; sync validation would flag it (SYNC-HAZARD-WRITE-AFTER-READ).
  - (b) **Free before flush.** A resource is freed while the open batch still references it, and a frame submit comes before the next flush. The resource is retired at the frame's value and destroyed before the batch runs: use-after-free in the copy. Today `Engine::run` and `aegis-test` flush before every `renderFrame`, so this only happens if a pass uploads during recording.
  - (c) **Failed batch submit.** It reads as "nothing to do". The textures in the batch never reach `finalState` (`ShaderRead`), so every later frame samples images still in an undefined layout. That is a validation error, but the error that caused it is gone.
- **Severity:** design problem
- **Confidence:** medium
- **Affects:** asset streaming, runtime uploads, and async transfer (the dedicated transfer queue is unused). `UploadManager` batching is not unit-tested.
- **Verify by:** for (a), an `aegis-test` check that renders a frame reading buffer X, uploads into X without waiting, and expects a SYNC-HAZARD-WRITE-AFTER-READ error; the other parts by code reading.
- **Fix sketch:** Record a buffer barrier before the copy (or document "upload targets only new buffers"). Have `flushUploads` return `std::expected<std::optional<TimelineValue>, Error>`. Let `retireValue` cover an open batch, or flush in `Renderer::beginFrame`.

### B-RENDER-07: Resolution-dependent textures are never freed and stay registered forever
- **Where:** `renderer/renderer.cpp` `Renderer::createResolutionDependentTexture`, `~Renderer`; `render_graph/resource_pool.cpp` `ResourcePool::~ResourcePool` (`ownedByPool` only)
- **Observed:** `m_resDependent` only grows; there is no unregister or free API. `~Renderer` only calls `waitIdle`. The graph's `ResourcePool` frees only fixed-size textures; its comment says resolution-dependent ones "belong to the renderer".
- **Inference:** Destroying and rebuilding the `FrameGraph` while the Renderer lives leaks every resolution-dependent image and view (G-buffer, Depth, SceneColor, Bloom, Final), including their bindless slots. Rebuilds would happen for a runtime GPU-driven toggle (the `AppDesc` proposal), a pass-list change, or an editor "rebuild graph". Each later `resize` still reallocates the orphans at full resolution. They are released only at Device teardown.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** runtime graph reconfiguration, multiple views or graphs per renderer, and the editor.
- **Verify by:** code reading. An `aegis-test` check that builds, destroys and rebuilds a FrameGraph, then resizes, could compare `bindlessHeap()` free-slot counts if those were exposed.
- **Fix sketch:** Return an owning handle (or add `Renderer::release(Texture)`) and free it from `ResourcePool::~ResourcePool` like fixed-size textures.

### B-RENDER-08: Pass ordering puts every reader after the last writer, so adding a writer silently reorders earlier readers
- **Where:** `render_graph/schedule.cpp` `detail::sortPasses`; `render_graph/default_graph.cpp` `addDefaultPasses`
- **Observed:** The writer chain advances `producers[target]` across all reference writes first. Only then do readers get an edge to the *final* producer: `dependencies[p].push_back(producer->second)`. No reader ever gets an edge to an earlier writer, and there is no write-after-read edge.
- **Inference:** Suppose a pass is added after Bloom that writes "SceneColor" in place, such as a debug overlay, an outline, or an in-place tonemap. Bloom (`reads = { SceneColor }`) is then ordered after the new pass and blooms the overlay. The new pass also gets no edge to Bloom, so it may run before Bloom with no barrier for the write-after-read case. `planTransitions` then sees the new order and emits barriers for that order, so validation stays silent while the output is wrong. Declaration order is documented as mattering only "among passes writing the same resource" (`FrameGraph::add` comment), which hides this.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** adding any pass that writes an existing resource (the main extension point), and `schedule.test.cpp`, where the case is cheap to add.
- **Verify by:** unit test: passes A (creates X), B (reads X), C (writes a reference to X), in that order. Today B comes out after C; the expected order is A, B, C.
- **Fix sketch:** Resolve each read against the producer *at that point in declaration order*, and add an edge from the next writer to every earlier reader.

### B-RENDER-09: The `DepthWrite` state omits depth-attachment read access
- **Where:** `rhi/vulkan_conversions.cppm` `toVulkan(ResourceState)`, case `DepthWrite`
- **Observed:** `.accessMask = eDepthStencilAttachmentWrite` only. `ColorAttachment` includes both read and write.
- **Inference:** Any barrier *into* `DepthWrite` from an earlier depth write never makes those writes visible to depth-test reads. Examples: a depth prepass followed by geometry with `Load`, or two `DepthAttachment` passes, which `planTransitions` handles as write→write. That is a read-after-write hazard, which sync validation flags (SYNC-HAZARD-READ-AFTER-WRITE). The current graph clears depth on first use and only reads it afterwards, so nothing triggers this today.
- **Severity:** polish
- **Confidence:** medium
- **Affects:** depth prepass, shadow maps, decals, anything with two depth-writing passes. The `ResourceState` → access table is not unit-testable today because it returns `vk::` types.
- **Verify by:** `aegis-test --headless --scene` after adding a second `DepthAttachment` pass with `Attachment` load = Load. Expect SYNC-HAZARD-READ-AFTER-WRITE.
- **Fix sketch:** Add `eDepthStencilAttachmentRead` to `DepthWrite`. Consider expressing the table in RHI-level stage and access enums so a test can check it.

### B-RENDER-10: `Device::createImageView` leaks a bindless slot on partial failure (code duplicated in two overloads)
- **Where:** `rhi/device.cpp` both `Device::createImageView` overloads
- **Observed:** The sampled descriptor is written and stored on the local `view`. If the storage descriptor then fails, the function returns `makeError(InitializationFailed)` and the local view is destroyed. The sampled slot is never pushed back to `m_freeSampledImages`. The two overloads are identical apart from their image source.
- **Inference:** Once storage slots run out, every failed creation of a `Sampled | Storage` view also loses one sampled slot permanently. The error code (`InitializationFailed`) also hides the real cause, an exhausted heap.
- **Severity:** polish
- **Confidence:** high
- **Affects:** bindless heap capacity under load. `BindlessHeap` allocation paths have no unit tests; only `FreeList` does.
- **Verify by:** code reading; a unit test on an extracted "write descriptors for usage" helper using a fake heap.
- **Fix sketch:** Merge the overloads into one helper and free the earlier slot on failure. Add an `ErrorCode` for heap exhaustion.

### B-RENDER-11: Skipped work leaves graph resources undefined, and one-shot reports are process-wide
- **Where:** `render_graph/passes/lighting_pass.cpp` `LightingPass::execute` (`static bool reported`); `render_graph/passes/scene_update_pass.cpp` `SceneUpdatePass::execute`; `render_graph/passes/lighting_pass.cpp` `maxPointLights` loop
- **Observed:**
  - Without an `assets::Environment`, Lighting returns before writing "SceneColor". The graph still transitions that image from `Undefined` every frame.
  - Without a camera, `SceneUpdatePass` returns without writing this slot's "CameraData".
  - Point lights past 128 are dropped with `break` and no log. `PointLightRenderSystem` still draws a sprite for every light.
  - The `reported` flag is a function-local static.
- **Inference:** A scene without an environment presents uninitialized memory (garbage or black, depending on the driver), not a defined fallback. A scene without a camera draws with stale or uninitialized camera data. With 129+ lights, the extra sprites appear but light nothing. With two graphs or engines in one process (tests), only the first reports the missing environment.
- **Severity:** polish
- **Confidence:** high
- **Affects:** new scenes and examples, and multi-graph tests.
- **Verify by:** `aegis-test --headless --scene --screenshot` on a scene with no environment.
- **Fix sketch:** Clear SceneColor (or write a fallback) when lighting is skipped. Log the light cap once per pass instance. Make `reported` a member.

## Map candidates

- **Renderer never frees resolution-dependent textures or offscreen targets:** confirmed, reported as B-RENDER-07. At process exit the Device pools reclaim them, so the leak is per graph rebuild, not per run.
- **`Device::createSwapchain(RecreateDesc)` frees old views before creating the new swapchain:** confirmed, reported as B-RENDER-01. Worse than the map says: a retried `resize` frees the same handles twice.
- **`DrawBatchRegistry::maxDrawBatches` enforced by assert only:** confirmed, reported as B-RENDER-05. Unreachable today because only one `MaterialTemplate` exists. The release-build effect is a write past the mapped `DrawBatches` copy, because `ResourcePool::write` has no bounds check either.
- **`DrawBatchRegistry` hooks Material construct/destroy but not replace:** confirmed as a fact, not reported separately. With a single template, batch membership never changes, so the counts stay right today. It becomes part of B-RENDER-04 and B-RENDER-05 once a second template exists.
- **`LightingPass` silently caps at 128 point lights:** confirmed, folded into B-RENDER-11 as polish. The visible mismatch is that point-light sprites are not capped.
- **A failed `UploadManager` submit leaks ring bytes:** rejected. `RingAllocator::reclaim` sets `m_tail` to the end offset of the next reclaimed batch, so a lost batch's bytes come back when a later batch is reclaimed. The real problems on that path (a lost error, images left in an undefined layout) are in B-RENDER-06.
- **`Context::validationErrorCount` counts errors only, so warnings pass:** confirmed as a fact, not a defect. Validation reports VUID violations and SYNC-HAZARDs at error severity; warnings are performance and best-practice messages. At most the doc wording needs polish. The count being process-global matters only for multi-engine tests.
- **`ResourcePool` name uniqueness not enforced:** confirmed, harmless today because `GeometryPass` and `GpuDrivenGeometryPass` are mutually exclusive in `addDefaultPasses`. A duplicate would silently split writers from readers (`resolveReferences` takes the first match). Cheap to fix in `resolveReferences` and to unit-test once `resolveReferences` takes plain entries.

## Notes for lens a

- One graphics queue for everything makes the global memory barriers for buffers and the timeline-keyed deletion correct. Async compute or transfer would need queue-aware `retireValue` and ownership transfers.
- `DeletionQueue::push` releases through a no-op lambda's capture destructor. It works, but the API is subtle; a typed `retire(T&&)` would read better.
- `Device` destruction order (upload manager → layout → heap → deletion queue → pools → allocator → device) is correct but implicit in member order; only the class comment protects it.
- Non-pooled RAII objects (`Pipeline`, `QueryPool`, `CommandBuffer`) have no deferred deletion. Correctness relies on the owner idling first (`~Engine` calls `waitIdle` before `m_frameGraph.reset()`). A rebuilt graph or a hot-reloaded pipeline would need `Device::retire` for them too.
- `FrameInfo.target` lives outside the graph, so `PresentPass` and the Renderer hand-code its transitions (Unknown → ColorAttachment → CopyDst → ColorAttachment → Present). It is correct, but the target is the one image the graph does not track.
- The image barriers in `planTransitions` cover the whole image. `BloomPass` handles per-mip states itself and restores the whole-image state at the end, which works but has to be repeated by any future per-mip pass.
- `Renderer::FrameInfo` and `render_graph::FrameInfo` both exist, and the consumer copies one into the other.
- `UploadManager` waits on the previous upload batch on every flush (`maxOutstandingBatches = 2` leaves one in flight). Simple, but it serializes loading.
- The ImGui Vulkan backend submits on the graphics `VkQueue` outside `Queue::submit`, so it never shows up in the timeline. Harmless single-threaded, but nothing tracks it.
- The `vk::`-typed state table (`toVulkan(ResourceState)`) is the main reason barrier derivation has no unit tests.
