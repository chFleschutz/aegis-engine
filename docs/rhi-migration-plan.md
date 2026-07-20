# RHI Migration Plan — splitting `graphics/` onto `rhi` / `renderer` / `platform`

Detailed, ordered steps for retiring the old volk-based `Aegis::Graphics` stack and moving
the engine onto the new `aegis.rhi`, `aegis.renderer`, `aegis.platform` modules plus two new
modules: **`aegis.render_graph`** and **`aegis.assets`**.

## Decisions locked

- Frame graph / passes / render systems live in a **new `aegis.render_graph` module** (not renderer partitions).
- Mesh vertex/index buffers use **buffer device address**, not bindless descriptor handles (per CLAUDE.md).

## The one hard constraint

The old stack (volk + `VK_NO_PROTOTYPES` + singleton `VulkanContext`) and the new stack
(`vk::raii` + owned `rhi::Device`) **cannot share a frame**. They own the device and swapchain
independently. Therefore:

- Everything *above* the GPU context (assets, render graph) can be built and tested alongside the old stack.
- The device + swapchain + frame-loop substitution in `engine.cppm` is **a single atomic commit** (Phase 4). volk dies there.

## Target module layout

```
aegis.platform      window (+ input, filesystem)
aegis.rhi           + :sampler  + :query  + finished :upload_manager  + general descriptor pool
aegis.renderer      frame loop, swapchain lifetime, resolution-dependent textures, Texture
aegis.render_graph  (NEW) frame graph, render passes, render systems, draw batch registry, frustum
aegis.assets        (NEW) vertex, static_mesh, mesh_preprocessor, texture, material, loaders, render components
```

---

## Phase 0 — RHI foundations (unblocks everything; no engine behavior change)

These are on the critical path — assets cannot move until upload works.

- [x] **Buffer upload path** (`rhi/upload_manager.cppm/.cpp`) — done. The command-buffer
      begin/enqueue lifecycle, batch submit, and staging reclaim all landed: `upload(const Buffer&, …)`
      stages + records a copy without blocking, `flushPending` submits and reclaims on a timeline,
      stalling only when a full ring of command buffers is outstanding.
- [x] **Staging ring reclaim** — the ring math was extracted verbatim into `RingAllocator`
      (`:ring_allocator`, imports no Vulkan) and covered by 9 cases in `tests/rhi/ring_allocator.test.cpp`,
      including wrap-around, wrapped-gap allocation, and reclaim-then-succeed. `StagingBuffer` now
      just adds the base pointer.
- [ ] **Implement image upload** — the remaining `UploadManager` gap (`upload(const Image&, …)` is a
      `return false` TODO). Note there is **no primitive underneath it**: `CommandBuffer` has only
      `copyBuffer`, so this needs `copyBufferToImage` (and `copyImageToBuffer`, for readback
      verification) added first. Detailed step-by-step plan: see "Next step" below.
- [ ] **Re-establish upload verification** — `verifyUpload()` was deleted in `cb686cd`, leaving a bare,
      result-discarding `m_device->flushUploads()` in `test/main.cpp` that never has anything to flush.
      **The buffer path is currently unexercised at runtime.** Restore a readback round-trip covering
      buffer *and* image (recoverable via `git show cb686cd^:src/aegis/test/main.cpp`).
- [ ] **Add `rhi:sampler`** — port `graphics/resources/sampler.cppm` to an RHI resource + handle
      (`Device::createSampler` → pooled `SamplerHandle`). `BindlessHeap::writeSampler` already consumes
      `vk::Sampler`; give it a first-class owner. The old type is a thin `VkSampler` wrapper bound to the
      `VulkanContext` singleton — that binding is the only real redesign. **Do not carry forward its
      bug:** `sampler.cppm:33` hardcodes `mipmapMode` to `LINEAR` and silently ignores the
      `CreateInfo::mipmapMode` field.
- [ ] **Add `rhi:query`** — GPU timestamp query pool + `CommandBuffer::writeTimestamp` / resolve,
      replacing `graphics/gpu_timer.cppm` + `GPUScopeTimer`. Porting hazards: it uses the pre-sync2
      `vkCmdWriteTimestamp` (new stack is Vulkan 1.3 / sync2, so `writeTimestamp2` with
      `vk::PipelineStageFlags2`), relies on the `VulkanContext` singleton and a global
      `MAX_FRAMES_IN_FLIGHT`, and its `resolveTimings` shadows `queryCount` while asserting against the
      outer one — the assert only holds by coincidence. Drop the shadowing; also note the existing
      `aquireQueryIndices` typo is not worth preserving.
- [ ] **Expose an ImGui-compatible descriptor pool** from `rhi::Device` (or document that the UI layer owns one). Old code feeds `VulkanContext::descriptorPool()` into `ImGui_ImplVulkan_Init`. `BindlessHeap::m_pool` is **not** reusable for this: it is `eUpdateAfterBind`, has no `eCombinedImageSampler` pool size, and lacks `eFreeDescriptorSet`.
- [ ] **Add buffer-device-address plumbing** to `rhi::Buffer`. This was previously listed as "confirm
      helpers exist" — **they do not.** There is no `deviceAddress()` getter and no
      `BufferUsage::ShaderDeviceAddress`; `Buffer::create` never sets `eShaderDeviceAddress` nor calls
      `getBufferAddress`, so any BDA use fails validation today. The device feature *is* already enabled
      (`device.cpp`, `.setBufferDeviceAddress(true)`) — only the RHI surface is missing. This is real
      work on the Phase 2 critical path, not a checkbox.
  - [ ] While here: `BufferUsage::CpuVisible` is dead — `toVulkan(BufferUsage)` never maps it. Wire it up or delete it.
- [ ] Extend `test/main.cpp`: upload a buffer + a texture + time a pass. Build + run clean.

**Exit criteria:** `test/main.cpp` uploads a texture and buffer, renders with a timestamped pass, runs clean.

### Bugs found while surveying Phase 0 (fix alongside the work that touches them)

Both are on the image-upload path and are invisible today only because every image in the tree is
single-mip:

- `image.cpp` — `Image::create` resolves `fullMipChain` into a local `mipLevels` and passes it to
  `vk::ImageCreateInfo`, but the constructor stores `desc.mipLevels`. An image created with
  `Image::fullMipChain` reports `mipLevels() == UINT32_MAX` while the real image has a proper chain.
- `command_buffer.cpp` — the `ImageHandle` overload of `transitionImageLayout` sets
  `.levelCount = image.arrayLayers()` where it means `image.mipLevels()`.

---

## Phase 1 — Platform (window + input)

- [ ] Make `platform::Window` the canonical window. Diff its API against `Aegis.Core.Window` and add any missing accessors used by the engine/editor.
- [ ] Add **`platform:input`** partition ported from `Aegis.Core.Input` (currently bound to `Core::Window`).
- [ ] Repoint `Core::Window` consumers to `platform::Window` (temporary alias acceptable to stage the change).
- [ ] Build + run an example still on the old renderer (window/input change is orthogonal to rendering).

**Exit criteria:** engine constructs a `platform::Window`, input works, old renderer still drives frames.

---

## Phase 2 — `aegis.assets` on RHI (independent of the frame loop)

Create the module and port in dependency order. Each item is created against an `rhi::Device` +
`UploadManager`; the old renderer keeps drawing throughout.

- [ ] Create `aegis/assets/` dir + `CMakeLists.txt` + `aegis.assets` primary module; add to `src/aegis/CMakeLists.txt` (`add_subdirectory(assets)`, link).
- [ ] Port **`vertex`** (`resources/vertex.cppm`) — pure data, first.
- [ ] Port **`mesh_preprocessor`** (`resources/mesh_preprocessor.cppm`, meshopt) — no GPU dependency.
- [ ] Port **`static_mesh`** (`resources/static_mesh.cppm`):
  - [ ] Rework `MeshData` from `Bindless::DescriptorHandle` vertex/index/meshlet buffers to **buffer device address**.
  - [ ] Upload vertex/index/meshlet buffers via `UploadManager`.
  - [ ] Update the shaders that read these (`shaders/**/vertex_geometry_bindless.slang`, `gpu-driven/*mesh*`, `task_meshlet_cull`) to consume BDA. Track shader edits as their own sub-checklist.
- [ ] Port **`texture`** (`resources/texture.cppm`) onto `rhi::Image`/`ImageView` + `rhi:sampler` + bindless heap. Cover `solidColor` / `solidColorCube` used by `loadDefaultAssets()`.
- [ ] Port **material** (`material/material_template.cppm`, `material/material_instance.cppm`) — build pipelines via `rhi::Pipeline::GraphicsDesc` instead of the old `Pipeline::GraphicsBuilder`.
- [ ] Port **loaders** (`loader/{loader,gltf_loader,fast_gltf_loader,obj_loader}.cppm`) to emit the new mesh/texture/material types.
- [ ] Move **render components** (`components.cppm`: `Mesh`, `Material`, `Environment`) into `aegis.assets` (or `scene` if you prefer components centralized there).
- [ ] Verify by loading a glTF asset headlessly (small harness or extended `test/main.cpp`) and confirming uploaded resources.

**Exit criteria:** a glTF/OBJ loads into new mesh + material + texture types on the RHI, BDA-backed, with default PBR assets reproducible.

---

## Phase 3 — `aegis.render_graph` on RHI (built alongside, not yet wired in)

- [ ] Create `aegis/render_graph/` dir + `CMakeLists.txt` + `aegis.render_graph` module; depends on `rhi` + `renderer` + `scene` + `assets`.
- [ ] Reimplement **`FrameGraph` + `FGResourcePool`** on `rhi::ImageHandle` and the resolution-dependent-texture mechanism `renderer::Renderer` already provides (replacing the old `VkImage`/attachment allocation).
- [ ] Port **`draw_batch_registry`** and **`frustum`**.
- [ ] Port **render passes** one at a time against `rhi::CommandBuffer` (`beginRendering` / `transitionImageLayout` / `bindPipeline` — the API `test/main.cpp` already exercises). Order suggestion:
  - [ ] `geometry_pass` (+ multi-attachment G-buffer: 5 color + depth) and `scene_update_pass`
  - [ ] `culling_pass` + `gpu_driven_geometry` (GPU-driven path; gate on `rhi::Device::Capabilities::meshShaders`)
  - [ ] `lighting_pass`, `sky_box_pass`
  - [ ] `present_pass`, `post_processing_pass`, `bloom_pass`, `transparent_pass`
  - [ ] `ui_pass` (depends on ImGui wiring — see Phase 4)
  - [ ] `ssao_pass` (currently disabled; port last or defer)
- [ ] Port **render systems** (`bindless_static_mesh_render_system`, `point_light_render_system`, base `render_system`).
- [ ] Keep both CPU-driven and GPU-driven paths (mirror old `Renderer::createFrameGraph` branch), or explicitly defer one and note it.

**Exit criteria:** the full frame graph compiles against the RHI and can execute given a `renderer::Renderer::FrameInfo` + scene, verified in a standalone harness before touching the engine.

---

## Phase 4 — Atomic engine cutover (volk dies here)

Single commit. Replace the GPU stack in `engine.cppm`:

- [ ] Swap members: `Core::Window`→`platform::Window`, `Graphics::Renderer`→`renderer::Renderer` + `render_graph::FrameGraph`, construct `rhi::Context` + `rhi::Device`.
- [ ] Move `loadDefaultAssets()` (default textures, PBR template/instance, pipeline build) onto `aegis.assets` + `rhi::Pipeline`.
- [ ] Rewire `run()`: `m_renderer.renderFrame([&](const auto& frameInfo){ m_frameGraph.execute(...); })`.
- [ ] Rewire `loadScene` / `sceneChanged` / `sceneInitialized` to the new frame graph + draw batch registry.
- [ ] Wire **ImGui** to the new device + descriptor pool (replaces old `Renderer::setupUI`).
- [ ] Handle resize via `renderer::Renderer::resize` + `needsResize` (already implemented) instead of the old `recreateSwapChain`.
- [ ] Update the engine accessor `renderer()` return type and any callers.
- [ ] Build + run `Template-Scene` → `Simple-Scene` → `Sponza` / `Helmets` / `Eval-Scene`.

**Exit criteria:** every example runs on the new stack; no code path touches `VulkanContext` or volk.

---

## Phase 5 — Editor / UI / examples repoint

- [ ] Repoint `editor/*` (esp. `editor_layer`, `panels/renderer_panel`, `panels/statistics_panel` — already partially on new modules) from `Aegis::Graphics::*` to `aegis::render_graph` / `aegis::assets`.
- [ ] Repoint `ui/*` and `scene` render-component references.
- [ ] Update every `examples/*` scene that referenced `Aegis::Graphics` types.
- [ ] Build + run each example.

**Exit criteria:** no remaining `import Aegis.Graphics.*` outside `graphics/` itself.

---

## Phase 6 — Delete old stack + remove volk

- [ ] Delete `src/aegis/graphics/` and its `add_subdirectory(graphics)` + `aegis-graphics` link in `src/aegis/CMakeLists.txt`.
- [ ] Remove volk dependency, `graphics/vulkan/volk_impl.cpp`, and `graphics/vulkan/vulkan_include.h`.
- [ ] Remove `Aegis.Core.Window` if fully superseded by `platform::Window`.
- [ ] Grep for stragglers: `VK_CHECK`, `AGX_ASSERT` in migrated code, `#include ".../vulkan_include.h"`, `import Aegis.Graphics`.
- [ ] Full clean build (warnings-as-errors) + run all examples.

**Exit criteria:** `graphics/` gone, volk gone, all examples green.

---

## Testing strategy

**Done — the framework landed.** doctest, vendored under `external/`, one executable per module
(`aegis-tests-rhi`, `aegis-tests-math`), CTest integration via `doctest_discover_tests`, gated behind
`AEGIS_BUILD_TESTS`. Layout, conventions, how to add a module, and the extraction guidance:
**`tests/README.md`** — read that before adding tests, it records three traps that each cost a build
cycle to rediscover.

Scope stays narrow on purpose: **pure, GPU-independent logic** that survives the migration.

### What is covered now

36 cases. Every item originally listed here is done:

- [x] Staging ring allocator — extracted to `RingAllocator` (`:ring_allocator`) and covered by
      `tests/rhi/ring_allocator.test.cpp` (9 cases: head advance, alignment, zero-size, oversize,
      exhaustion, wrap-around, wrapped-gap allocation, reclaim-then-succeed, capacity).
- [x] `FreeList` (`bindless_heap.cppm`) — `tests/rhi/free_list.test.cpp`. **The suspicion recorded
      here was wrong:** `pop()` is correct — it drains the free list first, then hands out `m_head++`
      while `m_head < m_capacity`, and only then reports exhaustion. The first test pinned it as
      correct and closed the question.
- [x] `ResourcePool` / generational `ResourceHandle` — `tests/rhi/resource_pool.test.cpp` (5 cases).
- [x] `utility::alignTo` — `tests/rhi/utility.test.cpp`.
- [x] `Aegis.Math` — `tests/math/math.test.cpp` (5 cases).
- [x] Bonus, not originally planned: swapchain extent-clamping + image-count policy
      (`chooseSwapchainExtent` / `chooseImageCount`, extracted to `aegis::rhi::detail`) and
      `detail::calcMipLevels`.

### What to test next

- [ ] Image subresource footprint math (per-mip byte size / offset / extent, and the array-layer
      packing contract) — extract to `aegis::rhi::detail` in `:image` alongside `calcMipLevels` and
      test there. This is exactly where silent off-by-one bugs live, and it lands with the image
      upload work above.
- [ ] `bytesPerTexel(Format)` — no such helper exists anywhere in the tree yet; the image upload needs one.
- [ ] Frustum-culling math (once frustum lands in `aegis.render_graph`, Phase 3).

**Known gap, accepted:** `UploadManager`'s batch state machine (`m_inFlight`, `m_recordIndex`
wraparound, the `flushPending` drain path) and `StagingBuffer`'s pointer arithmetic have no unit
tests — both need a live `Device`. That is what the `test/main.cpp` readback round-trip covers, which
is why restoring it is a Phase 0 checklist item and not optional polish.

### What to defer

- Anything requiring a live `vk::Device` (pipelines, real rendering, uploads end-to-end) — covered by
  `src/aegis/test/` (`aegis-test`). **Correction to the earlier note here:** the `examples/` targets do
  *not* cover this. They still run the old `Aegis::Graphics` stack and will not catch an RHI
  regression; `aegis-test` is the only `main()` that reaches a real `rhi::Device`.
- Any test against `Aegis::Graphics::*` — that code is scheduled for deletion; testing it is throwaway.

### After the rework (Phase 6+)

Grow `test/main.cpp` into a proper **headless RHI harness** (it's already a seed of one) for
device-level integration tests, once the API has stopped moving. Coverage-oriented work belongs here.

### Framework choice — settled

**doctest.** Minimal compile-time overhead (single header, ~4× faster to include than Catch2 v3) in a
modules-heavy, warnings-as-errors build; header-only with trivial single assertions; ships CTest
integration. Tests stay ordinary `.cpp` TUs that `import aegis.rhi;`, so the framework never touches
module interfaces. Vendored as a submodule under `external/`'s `SYSTEM` subdirectory so its headers
are immune to warnings-as-errors. Full rationale in `tests/README.md`.

---

## Cross-cutting risks / decisions to settle early

- **BDA mesh layout** (Phase 2) ripples into every geometry shader — sequence the shader edits with the `StaticMesh` change, don't leave them for later.
- **GPU-driven vs CPU-driven** paths both need porting in Phase 3, or one must be explicitly deferred with a note.
- **ImGui ownership** — decide whether the UI layer or a render-graph UI pass owns the descriptor pool and init, before Phase 3's `ui_pass`.
- **Convention drift** — new code follows the RHI conventions (lowercase `aegis::` namespaces, 4-space indent, `std::expected` + `makeError`, trailing return types, `[[nodiscard]]`, `Desc` structs), not the old `Aegis::Graphics` tabs/`VK_CHECK` style.

## Next step — the image upload path

Phase 0's remaining items are independent of each other; do them one at a time rather than as one
push. **Image upload first** — it is the only one that blocks the Phase 2 `texture` port, and it is
verifiable end to end on its own. Sampler is *not* needed to upload a texture, only to sample one, so
keeping it out lets this step be verified by byte-exact readback rather than by eyeballing a quad.

1. **Extract the footprint math** into `aegis::rhi::detail` in `image.cppm` — `bytesPerTexel(Format)`,
   `mipExtent(Extent3D, level)`, and `subresourceFootprints(...)` returning per-mip
   `{ mipLevel, extent, offset, size }`. Plain values only: a test TU cannot name a `vk::` type at all
   (see `tests/README.md`). `detail::calcMipLevels` is the precedent and `:image` is already re-exported
   by `rhi.cppm`. Fold `generateMipmaps`'s four open-coded `>>` shifts onto `mipExtent`.
   - **Decide and document the source-layout contract.** Recommend mip-major with array layers packed
     contiguously *inside* each mip (matches KTX2, and makes each mip exactly one copy region with
     `layerCount = arrayLayers`, so a cubemap is 1 region per mip rather than 6). A caller passing
     layer-major data otherwise gets silently scrambled faces.
2. **Fix the two bugs** listed under Phase 0 above (`image.cpp` mip resolution, `command_buffer.cpp`
   `levelCount`). Prerequisites, not drive-bys — step 4 drives its loop off `mipLevels()`.
3. **Add the copy primitives:** a plain `BufferImageCopy` struct in `:commands`, then
   `CommandBuffer::copyBufferToImage` / `copyImageToBuffer`, handle-based like every other image method.
   `copyImageToBuffer` earns its place beyond the test (screenshots, GPU-driven readback).
4. **Implement `UploadManager::upload`**, changing the signature from `const Image&` to
   `ImageHandle` (consistent with `Device::upload(BufferHandle, …)` and with what the transition/copy
   calls need). Mirror the buffer path's structure exactly. Align the staging allocation to
   `max(4, bytesPerTexel)` — Vulkan requires `bufferOffset` to be a multiple of both.
   - **Leave the image in `CopyDst` (or `CopySrc` after mip generation) and say so in the doc comment.**
     `UploadManager` cannot know which read state the caller wants — `ShaderReadVertex` vs
     `ShaderReadFragment` vs `ShaderReadCompute` — and guessing bakes a wrong barrier into every
     texture. The caller's transition is also what establishes the real dependency, since
     `generateMipmaps` ends with `dstStageMask = eNone`.
5. **Restore `verifyUpload()`** in `test/main.cpp` covering buffer, multi-mip image (a distinct
   constant per mip, so a mip-offset mistake cannot pass), and a 6-layer cubemap — the cubemap is the
   only case that actually exercises the layout contract from step 1, and `solidColorCube` depends on
   it in Phase 2. Fix the discarded `[[nodiscard]]` on `flushUploads()` while there.
6. **Unit-test the extracted math** by extending `tests/rhi/image.test.cpp`.

**Verify:** `ctest --preset windows-clang-debug -R "rhi::"`, then a full warnings-as-errors build and
a clean validation-layer run of `aegis-test`.
