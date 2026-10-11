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

**Exit criteria — met:** `test/main.cpp` uploads a texture and buffer, renders with a timestamped pass
(~20k frames timed per 6 s run), and runs validation-clean.

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

68 cases. Every item originally listed here is done:

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

**Phase 0 is complete** (exit criteria met). The one foundation gap it surfaced is that shaders cannot
yet receive data: there is no `CommandBuffer::pushConstants` / `dispatch`, and `Pipeline::*Desc` takes
`vk::` types nothing outside `aegis.rhi` can construct. Decide the convention before Phase 2's BDA
shader edits. The obvious candidate is one shared pipeline layout (bindless set plus a fixed
push-constant range, e.g. 128 bytes for all stages), so `*Desc` drops `setLayouts` /
`pushConstantRanges` entirely and `pushConstants` takes plain bytes. A compute round-trip in
`aegis-test` (read through one BDA pointer, write through another) then verifies BDA end-to-end, which
today is only checked by validation.

**Phase 1 (platform window + input) is independent** and can go before, after, or in parallel.

**Verify any of them with:** `ctest --preset windows-clang-debug`, a full warnings-as-errors build, and
a clean validation-layer run of `aegis-test`. Note that `aegis-test` is the *only* `main()` reaching a
real `rhi::Device`; `examples/` still runs the old stack and will not catch an RHI regression.

> **Running one test module:** `ctest --preset windows-clang-debug -L rhi` (or `-L math`). Each case
> is labelled with its module by `aegis_add_test`. `-R "rhi::"` does not work — CTest registers cases
> under their `TEST_CASE` name, not the `TEST_SUITE` name. `AGENTS.md` and `tests/README.md` are fixed.
