# RHI Migration Plan — splitting `graphics/` onto `rhi` / `renderer` / `platform`

Detailed, ordered steps for retiring the old volk-based `Aegis::Graphics` stack and moving
the engine onto the new `aegis.rhi`, `aegis.renderer`, `aegis.platform` modules plus two new
modules: **`aegis.render_graph`** and **`aegis.assets`**.

## Decisions locked

- Frame graph / passes / render systems live in a **new `aegis.render_graph` module** (not renderer partitions).
- Mesh vertex/index buffers use **buffer device address**, not bindless descriptor handles (per AGENTS.md).

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
- [x] **Image upload** — `UploadManager::upload(ImageHandle, data, generateMips)` replaces the old
      `return false` stub, mirroring the buffer batch lifecycle. The missing primitives landed with it:
      `BufferImageCopy` in `:commands`, plus `CommandBuffer::copyBufferToImage` / `copyImageToBuffer`.
      `Device::upload(ImageHandle, …)` is the public entry point.
  - [x] The per-mip offset math is extracted into `aegis::rhi::detail` in `:image`
        (`bytesPerTexel`, `mipExtent`, `copyAlignment`, `subresourceFootprints`) and unit-tested.
  - [x] **Source layout contract, decided and documented on `subresourceFootprints`:** mip-major,
        array layers packed contiguously *inside* each mip, tightly packed. Matches KTX2 and makes each
        mip one copy region with `layerCount = arrayLayers`, so a cubemap costs 1 region per mip
        rather than 6. There is no way to detect a caller mismatch at runtime.
  - [x] **Staging offsets deliberately diverge from source offsets.** Vulkan requires a copy's
        `bufferOffset` to be a multiple of both 4 and the texel block size, but loaders hand over
        tightly packed data (a 3×3 `R8_UNORM` mip 0 is 9 bytes, so mip 1 would land illegally).
        `SubresourceFootprint` therefore carries both `sourceOffset` and `stagingOffset`, and `upload`
        copies mip by mip rather than in one memcpy.
  - [x] **Ownership of the read-state transition is the caller's.** `upload` leaves the image in
        `CopyDst` (`CopySrc` after mip generation) because `UploadManager` cannot know which of
        `ShaderReadVertex` / `ShaderReadFragment` / `ShaderReadCompute` is wanted; guessing would bake a
        wrong barrier into every texture. That transition is also what establishes the real execution
        dependency, since `generateMipmaps` ends with `dstStageMask = eNone`.
- [x] **Upload verification re-established** — `verifyUploads()` in `test/main.cpp` covers a buffer
      round-trip, a 3-mip 2D texture (distinct fill per mip, so a wrong mip offset cannot pass), and a
      6-face cubemap (the only case that exercises the array-layer packing contract, which
      `solidColorCube` will depend on in Phase 2). All three compare byte-exactly through a `CpuRead`
      buffer. This also re-covers the buffer path, which had been unexercised at runtime since `cb686cd`.
- [x] **Add `rhi:sampler`** — `Device::createSampler(Sampler::Desc)` → pooled `SamplerHandle`; the
      sampler registers itself in the bindless heap and owns its `SamplerDescriptor`
      (`Sampler::descriptor()`). New enums `Filter` / `SamplerMipmapMode` / `SamplerAddressMode`, named
      after their Vulkan counterparts like `Format`.
  - [x] **Naming clash resolved first** (own commit): `SamplerHandle` was already the bindless slot
        alias. Bindless slot indices are now `…Descriptor` (`Descriptor<T>`, `SampledImageDescriptor`,
        `StorageImageDescriptor`, `SamplerDescriptor`) and `…Handle` always means a pooled resource.
        The rule is recorded in `AGENTS.md`.
  - [x] Old `mipmapMode` bug not carried forward — the field is honoured.
  - [x] **Second inherited bug dropped:** the old `Sampler(config, mipLevels)` derived `maxLod` from
        the texture's mip count, forcing one sampler per texture. The bindless heap keeps samplers
        separate from images, so `maxLod` now defaults to `Sampler::LodClampNone` and the image view
        bounds the mips — one sampler can serve every texture.
  - [x] Anisotropy is a requested maximum (default 16; ≤ 1 disables) clamped to the device limit by
        `detail::resolveAnisotropy`, unit-tested. The enum mapping itself returns `vk::` types and is
        unreachable from tests; `aegis-test`'s `verifySamplers()` creates one sampler per enum value
        under validation, and pins that a freed sampler's bindless slot is reused.
  - [ ] Deferred: a sampler cache deduplicating identical `Desc`s. Wanted by the Phase 2 `texture`
        port, not before.
- [x] **Add `rhi:query`** — `Device::createQueryPool(QueryPool::Desc)` returns a timestamp
      `QueryPool` by value (like `Fence`). `CommandBuffer::resetQueries` / `writeTimestamp` use sync2
      `writeTimestamp2`, at `TimestampStage::TopOfPipe` / `AllCommands`. `QueryPool::readTimestamps`
      never blocks (`ErrorCode::NotReady` if unwritten), and `elapsedMs` converts via
      `detail::timestampDeltaMs`, which masks to the queue family's `timestampValidBits` so a wrapped
      counter still yields the forward delta (unit-tested). None of the old hazards carried over: no
      singleton, no global frame count, no shadowed `queryCount`.
  - [ ] Deferred: the old `GPUTimerManager`'s per-frame bookkeeping (named scopes, one pool per frame
        in flight, results for the profiler panel). That belongs to whoever owns the frame loop, so it
        lands with the frame graph in Phase 3 (per-node timing), not in the RHI. `aegis-test`'s
        `FrameTimer` is the pattern to follow.
- [x] **ImGui descriptor pool — decided: the UI layer owns it, the RHI exposes nothing.** The vendored
      ImGui (1.92.4) Vulkan backend creates and owns its own pool when `InitInfo::DescriptorPoolSize > 0`,
      and `ImGui_ImplVulkan_AddTexture` allocates from it. `BindlessHeap::m_pool` stays unusable for this
      (`eUpdateAfterBind`, no `eCombinedImageSampler`, no `eFreeDescriptorSet`). See Phase 4's ImGui item.
- [x] **Add buffer-device-address plumbing** — `BufferUsage::ShaderDeviceAddress` maps to
      `eShaderDeviceAddress`; `Buffer::deviceAddress()` returns a `DeviceAddress` queried at creation
      (asserts if the buffer lacked the usage). The VMA allocator already had
      `VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT`. `aegis-test` creates BDA buffers under
      validation, which rejects a missing usage or allocation flag.
  - [x] `BufferUsage::CpuVisible` deleted — CPU visibility is `MemoryUsage`'s job; the flag was
        redundant as well as dead.
  - [ ] **Gap found, not in scope here — shaders cannot receive an address yet.** There is no
        `CommandBuffer::pushConstants` (nor `dispatch`), and `Pipeline::*Desc` takes
        `std::span<vk::PushConstantRange>` / `std::span<vk::DescriptorSetLayout>`, which no code outside
        `aegis.rhi` can name (the same trap as for tests). Fixing it means settling a pipeline-layout
        convention for bindless (e.g. every pipeline shares one layout: the bindless set plus one fixed
        push-constant range), which is a design decision, not plumbing. See Next step.
- [x] **Timed pass in `test/main.cpp`** — one `QueryPool` per frame in flight brackets the rendering
      pass; each frame reads back its slot's previous timestamps after `beginFrame`'s wait. The run
      fails (exit 1) if any read is not ready or no frame was timed.

**Exit criteria — met (after the review below).** `test/main.cpp` uploads a texture and buffer and
renders with a timestamped pass, clean under core **and synchronization** validation (~28k frames per
10 s run). The first claim of "met" was wrong: it held only under core validation, which does not check
synchronization, and sync validation then reported three hazards every frame (R1). Sync validation is now
always on in debug builds (R2), and `aegis-test` exits non-zero on any validation error.

### Phase 0 review — all fixed

A full read of `rhi/`, `renderer/` and `test/main.cpp`, plus an `aegis-test` run with synchronization
validation. Every item below is fixed, one commit each, and each fix that a device run can show is
covered by a new `aegis-test` check. (For the record: before R2, the environment switch that worked was
`VK_LAYER_ENABLES=VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT`; the layer's suggested
`VK_LAYER_VALIDATE_SYNC=1` produced no reports with SDK 1.4.328.1. Validation errors go to stderr.)

**Blocking** (fixed in the order R2, R1, R3, R4, R5, R8, R6, R7):

- [x] **R1 — Barrier stage masks are wrong; three sync hazards per frame.** Reported by sync validation:
  - `ResourceState::Unknown` has source stage `eNone`, so a transition from it does not chain with
    anything earlier. Swapchain: the transition does not wait for the acquire semaphore (signalled for
    `eColorAttachmentOutput`), so it can write the image while the presentation engine still reads it
    (`WRITE_AFTER_READ`). Reused attachments (the depth buffer shared by both frames in flight): the
    transition does not wait for the previous frame's writes. Fix: give `Unknown` a source stage of
    `eAllCommands` (or the caller's real previous state).
  - `ResourceState::Present` has destination stage `eNone`, so the render-finished semaphore signal does
    not cover the layout transition (`PRESENT_AFTER_WRITE`). Fix: a destination stage inside the
    signal's stage mask.
  - `aegis-test` moves the depth texture to `ResourceState::Attachment`, which is color-only (color stages
    and access), so depth tests are not ordered after the transition (`WRITE_AFTER_WRITE`). Fix the
    harness to use `DepthWrite`, and rename `Attachment` → `ColorAttachment` so the API cannot invite
    this again.
  - Verify: zero sync-validation reports over a multi-second `aegis-test` run.
  - **Fixed:** `Unknown` → source `eAllCommands`, `Present` → destination `eAllCommands`, `Attachment`
    renamed `ColorAttachment`, harness depth uses `DepthWrite`. Zero sync reports since.
- [x] **R2 — Turn on synchronization validation in debug builds by default.** Enable it in `Context`
  through `VK_EXT_layer_settings` (`validate_sync`), not an environment variable, so "validation-clean"
  includes synchronization from now on. Without this, R1 went unnoticed through all of Phase 0.
  - **Fixed**, plus: `Context::validationErrorCount()` counts reported errors and `aegis-test` fails on
    any, so a broken run can no longer look green to someone not reading stderr. The callback also
    labelled warnings "Error"; it now prints the real severity.
- [x] **R3 — `Queue::submit` signals its timeline at `eAllGraphics`, which excludes copy and compute.**
  In sync2, `ALL_GRAPHICS` covers only graphics-pipeline stages. So a signalled timeline value does not
  guarantee an upload batch's copies have finished. Yet `UploadManager` reclaims staging and
  `DeletionQueue` retires resources on exactly that value, and Phase 3's compute passes would not be
  covered either. Fix: signal at `eAllCommands` (both `submit` overloads).
  - **Fixed.** Not observable by validation today (every consumer host-waits first).
- [x] **R4 — Buffer uploads have no visibility barrier.** `UploadManager::upload(Buffer…)` records a copy
  and nothing else; a later shader read (Phase 2 vertex/index data through BDA) has no memory dependency
  on that copy. Image uploads leave this to the caller by contract; buffer uploads have no such contract
  and no barrier. Fix: end each batch with a global barrier (copy write → all-commands read). This is
  independent of R3, which is about the host wait.
  - **Fixed:** `CommandBuffer::makeTransferWritesVisible()`, recorded at the end of every upload batch.
    Like R3, only a consumer that does not host-wait would show it; Phase 2 will be the first.
- [x] **R5 — Image type is inferred from the extent, so a 1-pixel-tall image becomes 1D.**
  `deriveImageType` returns `e1D` whenever `extent.y == 1`. `solidColor` (1×1) would be a 1D image, and
  `solidColorCube` (1×1, 6 layers) cannot even be cube-compatible (that requires 2D), so it becomes a
  `1DArray`. Both are needed by `loadDefaultAssets()` in Phase 2. The upload tests use 2×2 and 4×4, which
  is why this was missed. View type has the same problem in reverse: any 6-layer view is forced to
  `eCube`, so a compute pass cannot write cube faces through a `2DArray` view (IBL), and cube arrays are
  impossible. Fix: an explicit image type on `Image::Desc` (default 2D) and an optional explicit view
  type on `ImageView::Desc`. Add a 1×1 and a 1×1×6 case to `verifyUploads()`.
  - **Fixed:** `ImageType` (`Dim1D`/`Dim2D`/`Dim3D`/`Cube`, default `Dim2D`; only `Cube` sets cube
    compatibility) and an optional `ImageViewType`, defaulting to `detail::defaultViewType` (unit-tested:
    a 6-layer 2D image is an array, not a cube). `verifyUploads()` round-trips 1×1 and 1×1×6 and creates
    a default-typed view of every test image, so validation checks view against image.
- [x] **R6 — Uploads larger than the staging ring can never succeed.** `UploadManager::Desc` declares
  `overflowThreshold` "for the overflow path", but no overflow path exists. Any single upload over the
  64 MB ring returns `false` every time, and the documented response (flush and retry) loops forever.
  Phase 2 hits it: a 4096×2048 RGBA32F HDR environment map is 128 MB. Fix: implement the overflow path
  (a dedicated staging buffer for oversize uploads, retired through the `DeletionQueue`), or at least
  return an error the caller can tell apart from "full right now".
  - **Fixed, both:** uploads above `overflowThreshold` get a dedicated staging buffer, freed through
    the `DeletionQueue` when their batch is submitted; `upload` now returns `std::expected<void, Error>`
    where only `ErrorCode::NotReady` means "flush and retry". `aegis-test` round-trips 80 MB.
  - **Found while fixing it — `RingAllocator` handed out in-flight memory.** An allocation ending
    exactly at the tail left `head == tail`, which reads as an *empty* ring, so the next allocation
    overwrote data the GPU had not consumed yet. An existing test pinned that exact-fit behaviour as
    correct. Fixed (an allocation may not end at the tail; one byte is given up) with two new cases.
- [x] **R7 — `generateMipmaps` does not check format support.** It blits with `eLinear` without checking
  `eBlitSrc` / `eBlitDst` / `eSampledImageFilterLinear` for the format; for formats without linear
  filtering (commonly 32-bit float) that is invalid usage. Phase 2's texture loading relies on
  `upload(…, generateMips = true)`. Fix: check the format features, and fail (or fall back) explicitly.
  - **Fixed:** `Device::supportsMipGeneration(Format)` (blit src/dst + linear filter, not depth); an
    upload asking for mips of an unsupported format returns `ErrorCode::FormatNotSupported` and records
    nothing. `aegis-test` generates RGBA8 mips and checks that D32 is refused.
- [x] **R8 — Renderer textures are invisible to shaders, and `resize` is broken.**
  `Renderer::createTexture` builds its `ImageView::Desc` without `.usage`, so `Device` never writes
  bindless descriptors for it; every renderer texture lacks a `SampledImageDescriptor`. `resize` repeats
  the omission and also drops `.range` (so multi-mip/array textures get a 1-mip, 1-layer view), and it
  dereferences `*newImage` right after reporting that it failed. `ResolutionDependentResource::name` is a
  `std::string_view` that dangles for any non-literal name. Fix all four together; the G-buffer depends
  on them in Phase 3, but the renderer is foundation code and the fixes are small.
  - **Fixed, plus a fifth:** `createTexture` passed `desc.mipLevels` straight into the view range, so a
    `fullMipChain` texture got a view of `UINT32_MAX` mips; views now use the resolved count. Resize
    recreates from the stored *requested* `Texture::Desc`, so a full chain is recomputed for the new size.
    `aegis-test` creates a full-chain texture under a temporary name and checks it keeps its bindless
    slot and mip coverage across a resize.

**Needed before specific Phase 2 items** (not blocking Phase 1):

- [ ] **Shader-facing pipeline layout / `pushConstants` / `dispatch`** — see the BDA gap above and
  "Next step". Blocks every shader that reads through BDA.
- [ ] **`Pipeline::GraphicsDesc` has no fixed-function state.** Depth test/write/compare (hard-coded
  on, on, `eLess`), cull mode (hard-coded back, CCW), per-attachment blending (hard-coded off) and a
  stencil format are all fixed. The old builder exposed them, and old passes use them: the sky box needs
  `LESS_OR_EQUAL`, no depth write and front culling; the transparent and UI passes need blending. Needed
  by the Phase 2 material port and Phase 3 passes. Add them to `GraphicsDesc` with today's values as
  defaults.

**Minor** (all fixed):

- [x] `Swapchain::present` treats `eSuboptimalKHR` as a failure (prints "Failed to present") and doesn't
  flag a recreate, and `Renderer::needsResize` never consults `Swapchain::needsRecreation`, so an
  acquire-time suboptimal never triggers a resize either.
- [x] `CommandBuffer::beginRendering` asserts `colorAttachments.size() < maxColorAttachments`; should be
  `<=` (off by one: 8 attachments are rejected).
- [x] `std::string_view::data()` is passed to Vulkan as a null-terminated string in debug labels,
  `debug::setName` and shader `entryPoint`. Literals and `std::string`s happen to work; a substring view
  over-reads. Fixed with null-terminated copies (also the application name).
- [x] Physical-device selection never requires present support, because the queue-family fallbacks
  always mark the set complete, and a GPU without a graphics queue trips an `assert` instead of being
  skipped.
- [x] Neither `Device` nor `Renderer` waits for the GPU on destruction. Early-exit paths (a failed
  `Engine::create` after work was submitted) destroy objects the GPU may still use.
- [x] Bindless heap capacities are hard-coded to 1024 each (`// TODO: query pd for max values`). Now
  `BindlessHeap::Desc`'s defaults (16384 / 4096 / 2048) clamped to the update-after-bind limits.
- [x] The swapchain uses `eExclusive` sharing with no queue-family ownership transfer. Harmless while
  present and graphics share a family (selection prefers that), wrong otherwise. Now `eConcurrent`
  across both families when they differ; untested here, since this GPU presents from the graphics
  family.

### Bugs found and fixed on the image-upload path

The first two were latent, invisible only because every image in the tree was single-mip:

- [x] `image.cpp` — `Image::create` resolved `fullMipChain` into a local `mipLevels` and passed it to
      `vk::ImageCreateInfo`, but the constructor stored `desc.mipLevels`, so such an image reported
      `mipLevels() == UINT32_MAX`. The private constructor now takes the resolved count as its own
      parameter, so it cannot be bypassed again.
- [x] `command_buffer.cpp` — the `ImageHandle` overload of `transitionImageLayout` set
      `.levelCount = image.arrayLayers()` where it meant `image.mipLevels()`.
- [x] `copyAlignment` rounded the required offset alignment up to the next **power of two**, which is
      wrong for `RGB32_SFLOAT`: the requirement is `lcm(4, texelBlockSize)` = 12, and 16 is not a
      multiple of 12. It now uses `std::lcm`.
  - This had a second half. `copyAlignment` feeds `RingAllocator::allocate`, which routed alignment
    through `utility::alignTo` — and that **asserts power-of-two**, so the correct value would have
    aborted in debug. Fixing the arithmetic alone would have converted a validation error into an
    assert. A general `utility::roundUpTo` was added beside `alignTo` (which stays as the faster
    bit-trick path for the power-of-two callers that dominate), and `RingAllocator` now uses it.
    Behaviour is unchanged for every pre-existing caller; a test pins the two functions as identical
    across powers of two.

**Testing lesson worth carrying forward:** a regrouping of `bytesPerTexel`'s switch briefly put
`RGBA16_UNORM` (8 bytes) in the 4-byte group and the whole suite stayed green, because the tests
checked its sibling `RGBA16_SFLOAT` but not `RGBA16_UNORM`. The `Format` enum's `// 16-bit` comments
mean *per channel*, which makes that grouping look plausible. The suite now covers the per-channel
families exhaustively, and `copyAlignment` has a case that walks **every** copyable format and asserts
the alignment divides by both 4 and that format's texel size — so a newly added format cannot
reintroduce the class of bug.

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
  - [ ] **Shader ripple:** the old shaders sample through combined `Sampler2D` (`bindless.slang`'s
        `SampledImage2D` typedefs it), but the new heap binds `eSampledImage` (binding 0) and
        `eSampler` (binding 2) separately. Textured shaders must move to a `Texture2D` + `SamplerState`
        pair indexed by a `SampledImageDescriptor` and a `SamplerDescriptor`. Rename `bindless.slang`'s
        "handle" terminology to "descriptor" while there, to match the C++ naming rule.
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
- [ ] Wire **ImGui** to the new device (replaces old `Renderer::setupUI`): set `DescriptorPoolSize` (at least 1 for the font plus one per `AddTexture`) instead of passing a pool, and `UseDynamicRendering = true` with the swapchain format.
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

75 cases. Every item originally listed here is done:

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
- [x] Image subresource footprint math — `bytesPerTexel`, `mipExtent`, `copyAlignment`, and
      `subresourceFootprints` in `aegis::rhi::detail` (`:image`), covered by `tests/rhi/image.test.cpp`.
      Includes the array-layer packing contract and the staging/source offset divergence.
- [x] `utility::roundUpTo` — `tests/rhi/utility.test.cpp`, including agreement with `alignTo` on
      powers of two.
- [x] `detail::resolveAnisotropy` — `tests/rhi/sampler.test.cpp` (4 cases).
- [x] `DeletionQueue` — `tests/rhi/deletion_queue.test.cpp` (3 cases).
- [x] `detail::timestampDeltaMs` — `tests/rhi/query.test.cpp` (4 cases), including counter wrap.
- [x] `detail::defaultViewType` — `tests/rhi/image_view.test.cpp` (5 cases).
- [x] `RingAllocator` head-meets-tail — 2 new cases in `tests/rhi/ring_allocator.test.cpp`, and the
      existing exact-fit subcase corrected (it pinned the bug).

### What to test next

- [ ] Frustum-culling math (once frustum lands in `aegis.render_graph`, Phase 3).
- [x] ~~Sampler description → Vulkan mapping~~ — **not unit-testable as planned**: the mapping
      returns `vk::` types, which a test TU cannot name. Only the plain-value part
      (`resolveAnisotropy`) was extracted and tested; the mapping is covered by `aegis-test`.

**Fixed — freed resources were released while in-flight frames still used them.** Three parts:
- The bindless bindings lacked `eUpdateUnusedWhilePending`, so by the spec *any* descriptor write
  while a frame was pending (creating an image view or sampler mid-frame) was invalid. Added.
- `Device::free` returned bindless slots to the heap immediately; a new resource could overwrite a
  descriptor an in-flight frame still read. Slot release now goes through the `DeletionQueue`
  (`defer`), whose `collect` now actually invokes its entries.
- **Worse, found while fixing it:** every deletion was tagged with the last *completed* timeline
  value, so `DeletionQueue` destroyed objects at the very next `setFrameCompleted` even if a later
  in-flight frame used them — a GPU use-after-free with 2 frames in flight. Deletions are now tagged
  with `Device::retireValue()` = the graphics queue's last submitted value + 1, covering the frame
  being recorded. (All submissions go through the graphics queue today; if transfer/compute queues
  get their own submissions, retirement needs a per-queue value.)

`Device::replace(ImageViewHandle, …)` still rewrites descriptors in place; documented as requiring
idle, which `Renderer::resize` guarantees. `aegis-test` now checks that a freed sampler slot is *not*
reused before retirement and *is* after; `DeletionQueue` is unit-tested (3 cases).

**Known gap, accepted:** `UploadManager`'s batch state machine (`m_inFlight`, `m_recordIndex`
wraparound, the `flushPending` drain path) and `StagingBuffer`'s pointer arithmetic have no unit
tests — both need a live `Device`. `test/main.cpp`'s readback round-trip is their only coverage, which
is why it is a Phase 0 deliverable rather than optional polish. Extend it, not the unit suite, when
that state machine changes.

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
- **ImGui ownership** — *decided:* the UI layer owns the descriptor pool via the backend's `DescriptorPoolSize` (Phase 0). Who calls `ImGui_ImplVulkan_Init` (UI layer vs a render-graph `ui_pass`) is still open for Phase 3.
- **Shader-facing pipeline layout** — how pipelines expose the bindless set and push constants without `vk::` types in `Pipeline::*Desc`. Blocks shaders consuming BDA (Phase 2) and every pass (Phase 3).
- **Convention drift** — new code follows the RHI conventions (lowercase `aegis::` namespaces, 4-space indent, `std::expected` + `makeError`, trailing return types, `[[nodiscard]]`, `Desc` structs), not the old `Aegis::Graphics` tabs/`VK_CHECK` style.

## Next step — settle the shader-facing pipeline layout

**Phase 0 is complete** — exit criteria met under core and synchronization validation, and every
review item fixed.

The remaining foundation work needs a decision before Phase 2's shader edits: shaders cannot yet
receive data. There is no `CommandBuffer::pushConstants` / `dispatch`, and `Pipeline::*Desc` takes
`vk::` types nothing outside `aegis.rhi` can construct. The obvious candidate is one shared pipeline
layout (bindless set plus a fixed push-constant range, e.g. 128 bytes for all stages), so `*Desc` drops
`setLayouts` / `pushConstantRanges` entirely and `pushConstants` takes plain bytes. A compute
round-trip in `aegis-test` (read through one BDA pointer, write through another) then verifies BDA
end-to-end, which today is only checked by validation. Extend `GraphicsDesc` with fixed-function state
(depth, cull, blend, stencil format) in the same pass, since both reshape `GraphicsDesc`.

**Phase 1 (platform window + input) is independent** of all of this and can go in parallel.

**Verify any of them with:** `ctest --preset windows-clang-debug`, a full warnings-as-errors build, and
a run of `aegis-test` that exits 0 — it fails on any validation error, core or synchronization (both
are on in debug builds). Note that `aegis-test` is the *only* `main()` reaching a
real `rhi::Device`; `examples/` still runs the old stack and will not catch an RHI regression.

> **Running one test module:** `ctest --preset windows-clang-debug -L rhi` (or `-L math`). Each case
> is labelled with its module by `aegis_add_test`. `-R "rhi::"` does not work — CTest registers cases
> under their `TEST_CASE` name, not the `TEST_SUITE` name. `AGENTS.md` and `tests/README.md` are fixed.
