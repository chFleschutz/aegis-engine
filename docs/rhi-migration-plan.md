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

- [ ] **Finish `UploadManager`** (`rhi/upload_manager.cppm/.cpp`). Currently stubbed:
  - [ ] Implement command-buffer begin/enqueue lifecycle (remove the `// TODO: Begin cmd` / `// TODO: Enqueue` gaps).
  - [ ] Implement image upload: layout transition to `TransferDst`, per-mip/per-layer buffer→image copies, transition to shader-read.
  - [ ] Confirm staging ring-buffer reclaim works across frames (the `reclaim`/`isRegionFree` path).
  - [ ] Verify in `test/main.cpp` by uncommenting the buffer-upload block and adding a texture upload.
- [ ] **Add `rhi:sampler`** — port `graphics/resources/sampler.cppm` to an RHI resource + handle. `BindlessHeap::writeSampler` already consumes `vk::Sampler`; give it a first-class owner.
- [ ] **Add `rhi:query`** — GPU timestamp query pool + `CommandBuffer::writeTimestamp` / resolve, replacing `graphics/gpu_timer.cppm` + `GPUScopeTimer`.
- [ ] **Expose an ImGui-compatible descriptor pool** from `rhi::Device` (or document that the UI layer owns one). Old code feeds `VulkanContext::descriptorPool()` into `ImGui_ImplVulkan_Init`.
- [ ] **Confirm buffer-device-address helpers** exist on `rhi::Buffer` (address getter, `BufferUsage::ShaderDeviceAddress`). Needed for the mesh model in Phase 2.
- [ ] Extend `test/main.cpp`: upload a buffer + a texture + time a pass. Build + run clean.

**Exit criteria:** `test/main.cpp` uploads a texture and buffer, renders with a timestamped pass, runs clean.

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

There is no unit-test framework yet. **Add a lightweight one now, but scope it narrowly** —
don't attempt broad coverage while the code is still being moved and deleted.

**Framework choice is deferred.** Pick one before writing the first batch (see criteria below).
Until then, tests are just plain `.cpp` translation units that `import aegis.rhi;` etc., so the
choice doesn't block designing what to test.

### What to test now (pure logic, GPU-independent, survives the migration)

These are stable end-points on the Phase 0–3 critical path where bugs are *silent*
(no crash — wrong index or corrupted memory). High value, tiny cost:

- [ ] `StagingBuffer` ring allocator (`allocate` / `isRegionFree` / wrap-around, reclaim across frames).
- [ ] `FreeList` (`bindless_heap.cppm`) — allocate/free/reuse. **Note:** `pop()` looks inverted
      (`if (m_head < m_capacity) return nullopt; return m_head++;` returns "exhausted" while
      headroom remains, hands out indices only past capacity). Confirm/fix with the first test.
- [ ] `ResourcePool` / generational `ResourceHandle` — allocate → free → reallocate → stale-handle rejection.
- [ ] `utility::alignTo` and other small numeric helpers.
- [ ] `Aegis.Math` and frustum-culling math (once frustum lands in `aegis.render_graph`, Phase 3).

### What to defer

- Anything requiring a live `vk::Device` (pipelines, real rendering, uploads end-to-end) — covered
  cheaply for now by the example smoke tests (`Template-Scene` → `Simple-Scene` → `Sponza`).
- Any test against `Aegis::Graphics::*` — that code is scheduled for deletion; testing it is throwaway.

### After the rework (Phase 6+)

Grow `test/main.cpp` into a proper **headless RHI harness** (it's already a seed of one) for
device-level integration tests, once the API has stopped moving. Coverage-oriented work belongs here.

### Framework selection criteria (decide before first batch)

- Minimal compile-time overhead — matters with a modules-heavy, warnings-as-errors build.
- Clean coexistence with C++23 modules (tests import the `aegis.*` modules; framework stays in
  ordinary `.cpp` TUs).
- Low friction to add a single assertion; richer matchers/mocking are a nice-to-have, not required
  for the pure-logic tests above.
- Integrates with the existing CMake + Ninja preset flow and the `test/` target.

---

## Cross-cutting risks / decisions to settle early

- **BDA mesh layout** (Phase 2) ripples into every geometry shader — sequence the shader edits with the `StaticMesh` change, don't leave them for later.
- **GPU-driven vs CPU-driven** paths both need porting in Phase 3, or one must be explicitly deferred with a note.
- **ImGui ownership** — decide whether the UI layer or a render-graph UI pass owns the descriptor pool and init, before Phase 3's `ui_pass`.
- **Convention drift** — new code follows the RHI conventions (lowercase `aegis::` namespaces, 4-space indent, `std::expected` + `makeError`, trailing return types, `[[nodiscard]]`, `Desc` structs), not the old `Aegis::Graphics` tabs/`VK_CHECK` style.

## Suggested first action

Start Phase 0 by finishing `UploadManager` (image path) and adding `rhi:sampler` + `rhi:query` —
these three unblock all of Phase 2, and each is verifiable in isolation via `test/main.cpp`.
