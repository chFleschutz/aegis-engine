# Findings E: render stack, testability lens

Scope: `src/aegis/rhi`, `src/aegis/renderer`, `src/aegis/render_graph` (with `passes/`), and their consumers `tests/rhi`, `tests/render_graph`, `tests/CMakeLists.txt` and `src/aegis/test/main.cpp`. This was a read-only audit: nothing was built or run. **Observed** means read in the code. **Inference** means my reading of it. Known items from `docs/agent-notes.md` and the map (`docs/audit/map/render.md`, `map.md` M1–M16) are cited as evidence and not reported again as new.

Baseline is `build/audit/logs/ctest.log`: 112/112 pass. That is 71 rhi cases, 21 render_graph cases (by `TEST_CASE` count), 11 assets, 5 math, plus 3 `device` runs and discovery entries.

---

## 1. What is tested today (honest depth)

### tests/rhi (`aegis-tests-rhi`, links `aegis-rhi` only, 71 cases)

| File | Unit | Depth |
|---|---|---|
| `ring_allocator.test.cpp` (11) | `RingAllocator` | **Deep.** Covers wrap-around, head==tail ambiguity at both edges, every `AllocError`, and reclaim. This is the model for an extracted unit. |
| `image.test.cpp` (22) | `detail::calcMipLevels`, `bytesPerTexel`, `copyAlignment`, `mipExtent`, `subresourceFootprints` | **Deep.** Covers boundaries (12-byte texels, shift overflow, degenerate counts), and one table-wide invariant loop (`copyAlignment`). |
| `swapchain.test.cpp` (7) | `detail::chooseSwapchainExtent/chooseImageCount` | Complete for the policy, including the half-sentinel case. |
| `image_view.test.cpp` (5), `sampler.test.cpp` (4), `query.test.cpp` (4) | `defaultViewType`, `resolveAnisotropy`, `timestampDeltaMs` | Complete for small pure helpers. |
| `resource_pool.test.cpp` (5) | `rhi::ResourcePool<int>` | Medium. Covers push/pop/replace and generation wrap. Stale-handle rejection is assert-only and so untested (README policy). |
| `deletion_queue.test.cpp` (3) | `DeletionQueue` | Medium. Covers keep-alive, defer once, and partial collect. It does not cover the Device policy that feeds it (`retireValue() = lastSubmitted()+1`). |
| `free_list.test.cpp` (3) | `FreeList` | Complete. `BindlessHeap`, which wraps it, is untested. |
| `utility.test.cpp` (7) | `alignTo`, `roundUpTo`, `formatName` | `formatName` is spot-checked on 3 of 20 formats. |

None of these touch `Device`, `CommandBuffer`, `UploadManager`, `Queue`, `Swapchain` or `Pipeline`. That is correct, because all of them need a device.

### tests/render_graph (`aegis-tests-render_graph`, links `aegis-render-graph` and through it the legacy scene stack, 21 cases)

| File | Unit | Depth |
|---|---|---|
| `schedule.test.cpp` (9) | `detail::sortPasses`, `detail::planTransitions` | **Good, but on hand-built inputs only.** Covers RAW, writer chains, a cycle, image first use, layout-class change, buffer cross-frame sync, read-only and HostWrite. It does **not** cover the inputs `FrameGraph::compile` actually builds (name resolution, reads-before-writes ordering, kinds), the tie-break order of independent passes, or a pass touching one resource twice. |
| `draw_batch_registry.test.cpp` (4) | `DrawBatchRegistry::addInstance/removeInstance/setDynamic` | Shallow. It calls the counters directly. `connect()` and the four entt signal handlers (the code that runs in production) are never exercised. |
| `gpu_driven.test.cpp` (4) | `detail::grownCapacity` | Complete for the helper. The `ensureCapacity`/`resizeBuffer` path that uses it is not covered. |
| `frustum.test.cpp` (4) | `Frustum::extractFrom/intersects` | Medium. Only the identity and scaled matrices are tested, with no real perspective matrix. Only the CPU extraction is tested; culling itself runs in `culling.slang`. |

### Device level (`aegis-test`, 3 ctest entries labelled `device`, skipped in CI)

`src/aegis/test/main.cpp` (about 1490 lines) runs the `verify*` checks (uploads, overflow, image and cubemap upload, round trips, mips, samplers, BDA, compute) and draws a triangle or a scene through `addDefaultPasses`. Halfway through a headless run it calls `resize({800,600})`. It fails on any `eError` validation message (M11) and, in scene mode, on a frame that is "one flat colour" (`verifyOffscreenFrame`). It saves `--screenshot` but never compares it with anything. CI runs `ctest -LE device` (`.github/workflows/build.yml`), so none of this runs on a PR.

**Bottom line:** the extracted leaves are tested well. The decision logic sits one level up, in `FrameGraph::compile`, `ResourcePool`, the pass `execute` bodies, `UploadManager`, device selection and the `DrawBatchRegistry` handlers. It has no unit tests, and in CI it has no tests at all.

---

## 2. Pure logic worth testing that is not tested

Ranked by bug risk × how often extensions touch it.

| # | Unit (file, symbol) | Test-visible today? | Why it matters |
|---|---|---|---|
| P1 | `render_graph/resource_pool.cpp` `ResourcePool::resolveReferences`, the usage-merge loop in `createResources`, the `perFrame` stride in `createBuffer` | No (needs `Device&`, `Renderer&`) | Every new pass goes through it. Name typos, duplicate creators (M12) and kind mismatches surface only on a GPU. |
| P2 | `render_graph/frame_graph.cpp` `FrameGraph::compile` (accesses ordered "reads before writes", error prefixing, kinds) | No | It is the glue between `sortPasses` and `planTransitions`. Its input-building is untested even though both ends are. |
| P3 | `render_graph/resource.cppm` `detail::isWrite/layoutClass/toResourceState/toImageUsage/toBufferUsage` | **Yes**, untested | Adding a `Usage` silently changes barriers (see E-RENDER-03). |
| P4 | `draw_batch_registry.cpp` `connect` and `on*Construct/Destroy` | Partly (only the null-instance path, with no GPU) | Runs on every ECS change. It has a counter-underflow bug (E-RENDER-06). |
| P5 | `lighting_pass.cpp` uniform gathering in `LightingPass::execute`; `scene_update_pass.cpp` `CameraData` assembly; `gpu_driven.cpp` `makeInstance` and `static_mesh_render_system.cpp` model/normal matrix | No (inline in `execute`) | Touched by every lighting or camera feature. `scene::Scene` is GPU-free and default-constructible, so only extraction is missing. |
| P6 | `upload_manager.cpp` `UploadManager::upload(ImageHandle…)` (size validation, overflow-vs-ring choice, regions) | No | It has a release-build over-read (E-RENDER-07). |
| P7 | `device.cpp` `Device::createPhysicalDevice` scoring, `queryQueueFamilies` | No (`vk::raii` in signatures) | Device choice on multi-ICD machines, and a prerequisite for software-Vulkan CI (E-RENDER-09). |
| P8 | `vulkan_conversions.cppm` `toVulkan(ResourceState)`, `toRHI(vk::Format)` | No (`vk::` types) | The barrier stage/access table. Clang's default `-Wswitch` already covers completeness where there is no `default:`. |
| P9 | `schedule.cpp` `sortPasses` tie-break | Yes | Independent passes come out in reverse add order (E-RENDER-05). |
| P10 | `shader.cppm` `groupCount` | Yes | Trivial. Low value. |

Considered and **not** worth a unit test:
- **`Renderer::resize` scaling.** `scaleFactor` is always `1.0` (`createResolutionDependentTexture`), so deleting the field beats testing it.
- **Frame-slot bookkeeping in `Renderer`.** It is about 5 lines, only meaningful against real fences, and covered by the device runs.
- **`Device::retireValue`.** A one-liner.
- **CPU pipeline-desc validation.** None exists, the validation layers do it, and a CPU copy of their rules would be over-engineering. The one cross-pass contract worth checking is formats (E-RENDER-11).
- **`toVulkan(Format)`.** No `default:`, so `-Wswitch` together with warnings-as-errors already forces completeness. I am inferring this: the project's CMake adds no `-Wall`, but Clang enables `-Wswitch` by default.

---

## 3. Blockers per unit

| Blocker | Units it traps | Observed |
|---|---|---|
| **B1. Declaration storage needs a device.** `ResourcePool(rhi::Device&, renderer::Renderer&)` is the only constructor. Pass constructors take `ResourcePool&`. | P1, P2, the whole default graph | Pass constructors are otherwise pure: they only call `addTexture/addBuffer/addReference` (e.g. `SkyBoxPass::SkyBoxPass`, `LightingPass::LightingPass`, `UiPass::UiPass` only stores `Context&`). |
| **B2. Plan logic interleaved with materialisation.** `FrameGraph::compile` runs resolve → sort → **`m_pool.createResources()` (GPU)** → **pass `createResources` (GPU)** → `planTransitions` → **`createQueryPool` (GPU)**. | P1, P2 | `frame_graph.cpp:24-95`. |
| **B3. Logic inside `execute` next to command recording.** | P5 | `LightingPass::execute` builds `Uniforms` from the ECS and then calls `pool.write`, `cmd.*`. |
| **B4. GPU-owning dependency in a handler.** | P4 | The handlers dereference `material.instance->materialTemplate()`. `MaterialInstance::create` needs `rhi::Device&` (`assets/material.cppm:80`). |
| **B5. `vk::` types in the signature.** | P7, P8 | `queryQueueFamilies(const vk::raii::PhysicalDevice&, …)`, `toVulkan(ResourceState) -> VulkanState{vk::…}`. Tests cannot name `vk::` types (tests/README "Gotcha"). |
| **B6. Assert-only input validation.** | P4, P6, rhi `ResourcePool` stale handles | tests/README tells tests to "test around" asserts. Release ctest runs with them compiled out. |
| **B7. Umbrella re-export rule.** | none new | Not a real blocker for this stack: `rhi.cppm` and `render_graph.cppm` already `export import` every relevant partition, and `render_graph::detail` is exported. New helpers placed in existing partitions are visible automatically. |
| **B8. Link graph.** `aegis-tests-render_graph` links the scene, assets, core and imgui stack (C13) and through `aegis-render-graph` the `compile_shaders` dependency (M6). | none | **Not a testability blocker**: `scene::Scene` and `scene::Registry` are `= default` constructible and GPU-free. The cost is build and link time and conceptual coupling, not missing tests. Fixing it means the SceneView proposal (engine-architecture.md §6), which is L-sized and app-driven. Do not do it for tests' sake. |
| **B9. `FrameInfo` holds `Scene&`.** | P5 | Same as B8: it couples passes to the ECS, but tests can construct a `Scene`. It is a blocker only for testing passes *without* the scene stack, which nobody needs yet. |

---

## 4. Smallest seam per blocker

| Blocker | Seam | Replaces | Unlocks | Over-engineering check |
|---|---|---|---|---|
| B1 + B2 | **Plan/materialise split** (E-RENDER-01): a pure `detail::compilePlan(entries, passInfos) -> std::expected<GraphPlan, std::string>`. A device-free `ResourceDeclarations` (the current `m_entries` plus `add*`, `describe`, `name`) that pass constructors take. | The first, pure half of `FrameGraph::compile` and of `ResourcePool::createResources`. | Compile-only tests of the real `addDefaultPasses` graph (both paths), declaration validation, and plan snapshots. | No new abstraction layer. It removes interleaving from `compile()` and moves code that already exists. One new value type (`GraphPlan`) and one split class. |
| B3 | `detail::` free functions in the owning partitions: `gatherLighting(scene, settings) -> LightingPass::Uniforms`, `makeCameraData(camera, transform, extent)`, `instanceTransform(mat4)`. | Inline blocks in `execute`. `makeInstance` and `StaticMeshRenderSystem::render` share the last one. | Light cap, defaults, directional normalisation, normal-matrix correctness. | It is the existing README pattern (stateless helper in `detail`). The `execute` bodies shrink. |
| B4 | Per-entity tracking API on `DrawBatchRegistry` that takes plain values (`track(entity, shared_ptr<const MaterialTemplate>, bool dynamic)`, `untrack(entity)`, `setDynamic(entity, bool)`). The entt handlers become 2-line adapters. | Counter arithmetic keyed on *current* component state. | Tests with `MaterialTemplate` only (no `MaterialInstance`). Fixes E-RENDER-06 and the map's `replace<Material>` staleness. | The map `entity → {template, dynamic}` is the state the registry needs anyway to be correct. |
| B5 | `detail::rankPhysicalDevice(DeviceCandidate)` and `detail::pickQueueFamilies(span<const QueueFamilyInfo>, bool presentation)` over plain structs. The `vk::raii` code unpacks and delegates. | The scoring loop and `queryQueueFamilies` body. | Device choice on mixed iGPU/CPU machines, a deterministic lavapipe choice. | It is the `chooseSwapchainExtent` precedent. |
| B5 (tables) | `static_assert` invariants **inside** `:vulkan_conversions` over a constexpr list of all enumerators (no test target needed). | Nothing. It adds compile-time checks. | Catches `toRHI(toVulkan(f)) != f` and a write state with no write access when someone edits a table. | Zero infrastructure. Only worth it because the tables are constexpr already. |
| B6 | Public-boundary validation returns `Error` (add `ErrorCode::InvalidArgument`). `assert` stays for internal invariants. Validation goes in a pure `detail::planImageUpload`. | `assert(data.size() == …)` in `UploadManager::upload`, and the assert on `maxDrawBatches` in `DrawBatchRegistry::addInstance`. | Testable error paths in debug **and** release. | It replaces asserts, so there is no new type except one enum value. |

---

## Findings

### E-RENDER-01: Graph declarations and the compile plan cannot exist without a GPU
- **Where:** `render_graph/resource_pool.cppm` `ResourcePool::ResourcePool(rhi::Device&, renderer::Renderer&)`; `render_graph/frame_graph.cpp` `FrameGraph::compile`; `FrameGraph::add` (`std::make_unique<T>(m_pool, …)`).
- **Observed:** Pass constructors only declare resources (e.g. `SkyBoxPass::SkyBoxPass` calls `pool.addReference("SceneColor", …)` twice). `compile()` does pure resolve and sort, then `m_pool.createResources()` and each pass's `createResources` (pipelines, shaders), then `planTransitions`, then `createQueryPool`. `ResourcePool::createResources` mixes the pure usage merge (`imageUsage[target] | detail::toImageUsage(...)`) with `m_renderer.createTexture`. The schedule tests feed hand-built `PassDecl`/`EntryDecl` and never the graph `compile()` builds.
- **Inference:** The default graph's wiring (17 resource-name strings across 13 passes, two geometry paths) is verified only by `aegis-test --scene` on a GPU, and CI never runs that. A misspelt `addReference` or a reordered pass list fails only on a developer GPU. This is the single largest testability gap in the render stack, and it sits exactly where features get added (new passes).
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new pass and resource, `default_graph.cpp`, tests of compile errors, plan snapshots.
- **Proposal:**
  1. Move the pure half of `compile()` into `:schedule` as `detail::compilePlan(std::span<const EntryInfo>, std::span<const RenderPass::Info>) -> std::expected<GraphPlan, std::string>`. `EntryInfo` is name, usage, and kind/desc. `GraphPlan` holds: resolved target per entry, order, merged `ImageUsage`/`BufferUsage`/host-written per created entry, kinds, and transitions. `compile()` becomes `plan = compilePlan(...)` → create resources from the plan → pass `createResources` → query pools. A second `compile()` (today it appends `m_timers` again) can be rejected in one place.
  2. Split the declaration list out of `ResourcePool` into `ResourceDeclarations` (no rhi or renderer members). Pass constructors take `ResourceDeclarations&`, which is a mechanical change to 13 signatures. `ResourcePool` (lookups after compile) keeps `Device&`.
  - **Tests unlocked:** see section 5, items 1–7.
  - **Size:** M (1 is S on its own and already makes P1 and P2 testable with hand-built entries; 2 is what lets the *real* passes be compiled in a test).

### E-RENDER-02: A pass touching one resource twice gets both barriers before it runs
- **Where:** `render_graph/frame_graph.cpp` `FrameGraph::execute` (`recordTransitions(frameInfo.cmd, m_transitions[i]); pass.execute(...)`) and `compile` (comment "A pass reads before it writes", which loops `{ infos[index].reads, infos[index].writes }`); `render_pass.cppm` `RenderPass::Info` doc ("in the order it accesses them").
- **Observed:** `planTransitions` emits one transition per access. `execute` records all of a pass's transitions up front. `compile` reorders a pass's accesses to reads first, writes second, which contradicts the `Info` doc.
- **Inference:** Failure scenario: a new pass declares `addReference("SceneColor", Usage::SampledCompute)` and `addReference("SceneColor", Usage::StorageWrite)` (a read-modify-write, e.g. TAA or an in-place blur). The plan becomes `ReadOnly ← prev`, then `General ← ReadOnly`, both recorded before `execute`. The pass then samples an image in `General` layout that its descriptor and intent assume is `ReadOnly`. That is a validation error at best, and wrong sync at worst. No current pass does this (checked every `info()`), so this is an extension trap, not a live bug. Nothing rejects it at compile time.
- **Severity:** design problem
- **Confidence:** high (mechanism); no current trigger.
- **Affects:** any read-modify-write pass (TAA, in-place post effects); plan tests.
- **Proposal:** In `compilePlan` (E-RENDER-01), reject a pass whose accesses to one resolved resource need different `LayoutClass`es (or merge them into one state when the classes agree), with an error naming the pass and the resource. Fix the `Info` doc to match the reads-then-writes rule. Unlocks the test "rejects a pass accessing one image in two layouts". S.

### E-RENDER-03: `default:` in the Usage tables makes a new Usage silently barrier-free
- **Where:** `render_graph/resource.cppm` `detail::isWrite`, `layoutClass`, `toImageUsage`, `toBufferUsage`.
- **Observed:** All four end in `default: return false / LayoutClass::None / ImageUsage::None / BufferUsage::None`. `toResourceState` lists every case and has no `default:`.
- **Inference:** Failure scenario: someone adds `Usage::ColorAttachmentRead` or `Usage::StorageReadWrite` and updates `toResourceState` (which the compiler forces) but not `isWrite`. Then `planTransitions` (`isWrite(from) || isWrite(usage)`) emits no barrier after it: a silent data race that only sync validation on a GPU can catch. `layoutClass` returning `None` for a new image usage likewise suppresses layout transitions. Clang's default `-Wswitch` would flag every one of these if the `default:` were gone. These are the tables an extension touches first (map §6.2: "adding a `Usage` touches four `switch` tables").
- **Severity:** design problem
- **Confidence:** high
- **Affects:** adding any `Usage`; barrier correctness; tests of the tables.
- **Proposal:** Replace `default:` with explicit cases in the four functions (S, no new code). Add `resource_usage.test.cpp` with consistency invariants that are test-visible today (seam: none). For example: every `isWrite` usage maps to a `ResourceState` that is not a read-only state; every image usage has a non-`None` `layoutClass` and `toImageUsage`; `HostWrite` maps to no GPU state.

### E-RENDER-04: Reference resolution checks only that a name exists, not kind or uniqueness
- **Where:** `render_graph/resource_pool.cpp` `ResourcePool::resolveReferences`, `createResources`.
- **Observed:** A reference binds to the first non-reference entry with the same name (duplicates were already known: M12/D7; GeometryPass and GpuDrivenGeometryPass both create `Position…Depth`, but only one is ever added). There is no check that a reference's `Usage` fits the target's kind. `createResources` ORs `toImageUsage` into buffer entries (then ignores it) and `toBufferUsage` into texture entries.
- **Inference:** Failure scenario: `addReference("Final", Usage::ShaderRead)` (a buffer usage on an image) resolves fine. `layoutClass(ShaderRead)` is `None`, which differs from the creator's `General`, so a transition to `ResourceState::ShaderRead` (layout `eReadOnlyOptimal`) is recorded. But `toImageUsage(ShaderRead)` is `None`, so the image lacks `Sampled` usage. The result is a validation error at runtime only. Conversely, an image usage on a buffer name produces a memory barrier with attachment stages. All of this is pure, and all of it is detectable at compile.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new pass; `addDefaultPasses` variants; compile-error tests.
- **Proposal:** In `compilePlan`: reject (1) a duplicate created name, (2) an image-only usage (`ColorAttachment`, `DepthAttachment`, `DepthRead`, `Sampled*`) on a buffer, and (3) a buffer-only usage (`ShaderRead`, `IndirectRead`, `HostWrite`) on a texture. Return errors naming the pass and the resource. Tests: section 5, items 3–5. S once E-RENDER-01 exists (item 1 alone is testable with hand-built entries).

### E-RENDER-05: Independent passes run in reverse add order, and cross-pass CPU dependencies are invisible
- **Where:** `render_graph/schedule.cpp` `detail::sortPasses` (Kahn from sinks, then `std::ranges::reverse(order)`); `passes/gpu_driven.cppm` `CullingPass`/`GpuDrivenGeometryPass` (`const InstanceUpdatePass& m_instances`).
- **Observed:** With two passes and no edges, `ready` gets `{0,1}`, `order` becomes `{0,1}` and is reversed to `{1,0}`. `CullingPass::execute` reads `m_instances.staticCount()/dynamicCount()`, which `InstanceUpdatePass::execute` sets in the same frame. That order holds only because Culling also declares `DynamicInstances`.
- **Inference:** Failure scenario: a new pass constructed with `const InstanceUpdatePass&` (e.g. stats or a debug-draw pass) reads its counts but declares none of its buffers, and is added after it. Because it is independent, the reverse tie-break schedules it *before* `InstanceUpdatePass`, so it sees last frame's counts. That is an off-by-one-frame bug with no error. The `add()` doc ("Order only matters among passes writing the same resource") makes the reversal look intentional, but nothing tests or documents it, and timing panels list passes out of add order.
- **Severity:** design problem
- **Confidence:** medium (mechanism high; impact depends on future passes).
- **Affects:** pass authoring, the editor profiler order, schedule tests.
- **Proposal:** Make the topological sort stable (among ready passes take the lowest add index; a `std::priority_queue` or ordered set replaces `std::queue`, with no reverse step) and state in `add()` that independent passes keep add order. Unlocks the test "independent passes keep their add order". S.

### E-RENDER-06: DrawBatchRegistry counters underflow for a Material without an instance
- **Where:** `render_graph/draw_batch_registry.cpp` `onDynamicConstruct`, `onDynamicDestroy`, `onMaterialConstruct`, `setDynamic`.
- **Observed:** `onMaterialConstruct` counts only `if (material.instance)`. `onDynamicConstruct`/`onDynamicDestroy` call `setDynamic` whenever `registry.all_of<assets::Material>(entity)`, without checking that the material was counted. `setDynamic(false)` does `++m_staticCount; --m_dynamicCount;`. `assets::Material{}` has a null `instance` (`assets/components.cppm:18`). The editor adds exactly that (`scene_panel.cppm:309`, `drawAddComponentItem<aegis::assets::Material>` → `registry.add<T>(entity)`), and the default main camera carries `DynamicTag` (`scene_defaults.cppm:26`).
- **Inference:** Failure scenario: an entity has `Material{}` (null instance) and has its `DynamicTag` added or removed, or is destroyed with entt removing `DynamicTag` before `Material`. Then `setDynamic` moves a count that was never added. With no dynamic mesh instances, `m_dynamicCount` wraps to `UINT32_MAX`. `InstanceUpdatePass::execute` then calls `ensureCapacity(…, UINT32_MAX, …)`, which tries to `resizeBuffer` about 400 GB and prints an error every frame. `instanceCount()` is skewed for culling. The destroy-order variant depends on entt pool order, which I did not verify. The tests never exercise `connect()`, which is how this slipped through.
- **Severity:** bug
- **Confidence:** medium (mechanism high; the exact editor click path is not run).
- **Affects:** the GPU-driven path, the editor, scene reset; DrawBatchRegistry tests.
- **Proposal:** Track counted entities: `std::unordered_map<entt::entity, Tracked{const MaterialTemplate*, bool dynamic}>`. Expose plain-value `track/untrack/setDynamic(entity, …)` and make the handlers 2-line adapters that look up `Tracked` instead of re-reading component state. This also fixes the known `replace<Material>` staleness (map §3.3).
  - Test today (seam: none) with `scene::Registry` + `connect` + `add<assets::Material>(e)` + `add<DynamicTag>(e)`: counts stay 0/0. This is a bug-report test that fails today.
  - Test the template paths through `track(...)` with `MaterialTemplate` (no GPU).
  - S.

### E-RENDER-07: Image-upload validation is assert-only, and the decision logic is trapped next to recording
- **Where:** `rhi/upload_manager.cpp` `UploadManager::upload(ImageHandle, …)`, `UploadManager::stage`.
- **Observed:** `assert(data.size() == last.sourceOffset + last.size && "Upload data does not match image layout");` is followed by `std::memcpy(staging->data + footprint.stagingOffset, data.data() + footprint.sourceOffset, footprint.size)`. `assert(false && "Image format cannot be uploaded…")` is followed by `makeError(ErrorCode::Unknown)`. `ErrorCode` has no `InvalidArgument`. The overflow-vs-ring decision (`size > m_overflowThreshold`) and the region list are built inline between `m_device->createBuffer` and `cmd.*` calls.
- **Inference:** Failure scenario: in a Release build (CI builds and tests release), `upload(image, data)` with `data` shorter than the image's mip chain (a caller bug, e.g. a decoder that returns RGB for an RGBA image) makes `memcpy` read past the caller's buffer. That is UB and a possible crash or garbage texture, where debug would have trapped. The comment says "trap it instead", but in release there is no trap. The planning part (footprints, size check, staging size and alignment, overflow choice, `BufferImageCopy` regions) is pure and decides correctness, yet it is reachable only through `aegis-test`. The ring itself is already extracted and tested (`RingAllocator`).
- **Severity:** bug (release over-read); design problem (testability).
- **Confidence:** high (mechanism); the trigger needs a caller bug.
- **Affects:** asset loading (`assets/upload`), new texture formats, staging tests.
- **Proposal:** Add `detail::planImageUpload(Extent3D, Format, mipLevels, arrayLayers, generateMips, dataSize, overflowThreshold) -> std::expected<ImageUploadPlan, ErrorCode>` in `:upload_manager`. The plan holds footprints, staging size, alignment, `useOverflow`, and regions relative to offset 0. Add `ErrorCode::InvalidArgument`. `upload()` keeps only stage → memcpy → record. Batching (in-flight FIFO, reclaim) stays device-tested: it is small and tied to real timeline waits. S.

### E-RENDER-08: Per-frame CPU data assembly is buried in `execute` and duplicated
- **Where:** `passes/lighting_pass.cpp` `LightingPass::execute` (builds `Uniforms`); `passes/scene_update_pass.cpp` `SceneUpdatePass::execute` (builds `CameraData` and writes `camera.aspect` back into the ECS); `passes/gpu_driven.cpp` `makeInstance` and `passes/static_mesh_render_system.cpp` `StaticMeshRenderSystem::render` (both compute `glm::inverse(glm::mat3{ model })` with row-major packing).
- **Observed:** Lighting stops at `maxPointLights` with `break` (known), defaults the directional light to `{0,0,1,0}`, and normalises `forward()`. `SceneUpdatePass` computes `extent.x / extent.y` with no zero guard. The model/normal-row packing is written twice, with a comment that the transpose is "implied by the row layout".
- **Inference:** These are where lighting and camera features land: shadows, more light types, jitter for TAA. They can be tested today because `scene::Scene` is default-constructible and GPU-free (`scene.cppm:30`). Only their position inside `execute` blocks it. The duplicated normal-matrix math is the kind of thing that drifts between the CPU and GPU-driven paths.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** lighting, camera, instancing features; both geometry paths.
- **Proposal:**
  - Add `detail::gatherLighting(scene::Scene&, float ao, LightingViewMode) -> LightingPass::Uniforms`.
  - Add `detail::makeCameraData(const Camera&, const GlobalTransform&) -> CameraData`, and move the aspect write-back out of the render pass into the camera system or the frame loop.
  - Add `detail::instanceTransform(const glm::mat4&) -> {mat3x4, normal rows}`, used by both paths.
  - Each `execute` keeps only `pool.write` and `cmd.*`. Tests: section 5, items 9–12. S each.
  - Do **not** wait for SceneView (engine-architecture §6) to do this.

### E-RENDER-09: Device and queue selection policy is untestable and ties CPU and integrated GPUs
- **Where:** `rhi/device.cpp` the physical-device scoring loop (`score += 1000` for discrete, `score += properties.limits.maxImageDimension2D`; `std::ranges::sort(candidates)`; `candidates.back()`), `Device::queryQueueFamilies`; `rhi/device.cppm` `Device::Desc` (empty).
- **Observed:** Non-discrete devices score only `maxImageDimension2D`. A tie is broken by the higher enumeration index (pair sort). There is no preference for integrated over CPU or virtual, and no override. The queue-family logic is pure flag arithmetic over `vk::raii` objects.
- **Inference:** Failure scenario: a Linux laptop with an Intel iGPU and Mesa's lavapipe ICD installed (both ship in `mesa-vulkan-drivers`). Both report 16384, so whichever enumerates last wins, and the engine may render on the CPU rasterizer with no message. I did not verify the limits or the order on real hardware. The same policy decides which device a CI software-Vulkan run gets (E-RENDER-13).
- **Severity:** bug (device choice); design problem (testability).
- **Confidence:** medium
- **Affects:** dev machines, software-Vulkan CI, future multi-GPU selection; device-selection tests.
- **Proposal:**
  - Add `detail::rankPhysicalDevice(DeviceCandidate{type, apiVersion, maxImageDimension2D, queuesComplete, extensionsSupported}) -> std::optional<std::uint32_t>`, with the order discrete > integrated > virtual > CPU and "unsuitable" as `nullopt`.
  - Add `detail::pickQueueFamilies(std::span<const QueueFamilyInfo>, bool presentation)`.
  - Use the empty `Device::Desc` for a `preferredDeviceName` / `allowCpu` override that `aegis-test` can pass.
  - This is the `chooseSwapchainExtent` pattern. S–M.

### E-RENDER-10: The barrier stage/access table cannot be checked by tests, but could check itself
- **Where:** `rhi/vulkan_conversions.cppm` `toVulkan(ResourceState)`, `toRHI(vk::Format)` (has `default:`).
- **Observed:** The table that turns graph decisions into Vulkan barriers returns `VulkanState{vk::ImageLayout, vk::PipelineStageFlags2, vk::AccessFlags2}`. Every function is `constexpr`. `toRHI(vk::Format)` ends in `default:`, so a new `Format` missing from it compiles.
- **Inference:** Tests cannot reach it (B5). A fake device would not test it either, because a fake would not interpret Vulkan masks. Its invariants are cheap to state at compile time, and they guard the most error-prone edits (adding a `ResourceState` or a `Format`).
- **Severity:** polish
- **Confidence:** high
- **Affects:** adding a `ResourceState` or `Format`; barrier correctness.
- **Proposal:** Add a constexpr `allFormats` / `allResourceStates` array next to the enums in `:common` (it is also reusable by doctest loops over `formatName`/`bytesPerTexel`). Add `static_assert`s in `:vulkan_conversions`:
  - `toRHI(toVulkan(f)) == f` for every format;
  - every state whose name implies a write has a write bit in `accessMask`;
  - image states other than `Unknown` never map to `eUndefined`.
  - S.

### E-RENDER-11: Cross-pass format contracts are duplicated literals
- **Where:** `passes/ui_pass.cpp` `UiPass::createResources` (`static_cast<VkFormat>(vk::Format::eR16G16B16A16Sfloat); // "Final", see PostProcessingPass`); `passes/screen_passes.cpp` `SkyBoxPass::createResources` (`rhi::Format::RGBA16_SFLOAT`, `rhi::Format::D32_SFLOAT` literals rather than `GeometryPass::depthFormat`).
- **Observed:** The pipeline attachment formats repeat what other passes declared in `addTexture`. The pool knows the format (`ResourceDescription::format`), but there is no lookup by handle.
- **Inference:** Changing "Final" or "SceneColor" to another format (an HDR swapchain, R11G11B10 for bandwidth) breaks UI or sky-box pipeline creation or rendering only on a GPU. A `pool.format(handle)` accessor removes the duplication and makes a plan test able to assert the contract.
- **Severity:** polish
- **Confidence:** high
- **Affects:** format changes, new passes reusing a target.
- **Proposal:** Add `ResourcePool::format(ResourceHandle)` (resolving references), and use it in `UiPass`/`SkyBoxPass`/`TransparentPass` pipeline creation. With E-RENDER-01, a plan test can check "every reference used as an attachment resolves to a texture with a format". S.

### E-RENDER-12: A partial failure in `Renderer::resize` leaves a view on a retired image
- **Where:** `renderer/renderer.cpp` `Renderer::resize` (the loop over `m_resDependent`).
- **Observed:** `m_device.replace(texture.image, …)` succeeds, which swaps the pool slot and queues the old `VkImage` for deletion at `retireValue()`. If `m_device.replace(texture.view, …)` then fails, it only `std::println`s. `resize` returns `void`.
- **Inference:** Failure scenario: an allocation failure, or a failed bindless write in `Device::replace(ImageViewHandle…)` (which returns `InitializationFailed`), after the image swap. The view handle keeps the old `VkImageView`, whose image the deletion queue destroys a frame later. Every pass sampling that texture then uses a destroyed image: GPU use-after-free, with no error to the caller. This is rare (it needs resource exhaustion) and only device-testable. I list it because `resize` is the frame-graph contract every resolution-dependent feature relies on. The `scaleFactor` field (always `1.0`) is dead generality in the same loop.
- **Severity:** bug
- **Confidence:** medium (mechanism high, trigger rare)
- **Affects:** resize, dynamic resolution features.
- **Proposal:** Create the new view against the new image *before* committing either swap (or roll back the image swap on view failure). Make `resize` return `std::expected<void, Error>` so callers (`Engine::resize`, `aegis-test`) stop before `FrameGraph::resized()`. Delete `ResolutionDependentResource::scaleFactor` until dynamic resolution exists. S.

### E-RENDER-13: Device verification is coarse, image-blind and never runs in CI
- **Where:** `tests/CMakeLists.txt` (3 `device` tests); `src/aegis/test/main.cpp` `verifyOffscreenFrame`; `.github/workflows/build.yml` (`ctest -LE device`).
- **Observed:** Three ctest entries cover about 12 `verify*` checks and two render paths. Scene-mode correctness is judged by "not one flat colour". The `--screenshot` PNG is written but never compared. CI has no Vulkan device. `Context::validationErrorCount` counts `eError` only (M11).
- **Inference:** A shading, tone-mapping or barrier regression that does not raise a validation error passes everywhere. A failure points at "aegis-test-scene", not at the check that failed. Everything above the pure leaves (graph, passes, uploads, swapchain) has no PR gate. agent-notes ideas 1–2 cover headless examples and reference images for examples; this finding is about the render stack's own harness.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every render feature; CI.
- **Proposal:** See section 6, items (c) and (e).
  - (1) Add `--compare <png> --tolerance <t>` to `aegis-test`, reusing the readback in `verifyOffscreenFrame`, with references under `tests/golden/`.
  - (2) Add a Linux debug CI job that runs `ctest -L device` on lavapipe.
  - (3) Optionally `--check <name>`, so ctest registers one entry per `verify*` without restructuring `main.cpp`.
  - S–M.

---

## 5. Candidate tests, ranked

| # | TEST_SUITE / TEST_CASE | Unit | Seam | Value | Effort |
|---|---|---|---|---|---|
| 1 | `render_graph::addDefaultPasses` / "the CPU-driven default graph compiles to a plan without a device" | default graph + `compilePlan` | E-01 (both parts) | Highest: proves all 17 name contracts and the pass order without a GPU, in CI | M (seam), S (test) |
| 2 | `render_graph::addDefaultPasses` / "the GPU-driven default graph compiles, and culling runs before GPU-driven geometry" | same | E-01 | Covers the second path, which only mesh-shader GPUs exercise | S after 1 |
| 3 | `render_graph::compilePlan` / "rejects a reference to a name no pass creates, naming the pass" | `compilePlan` | E-01 part 1 | Most common authoring error | S |
| 4 | `render_graph::compilePlan` / "rejects two passes creating the same name" | same | E-01 part 1 + E-04 | Turns M12 from a comment into a check | S |
| 5 | `render_graph::compilePlan` / "rejects a buffer usage on a texture and an image usage on a buffer" | same | E-01 part 1 + E-04 | Kind mismatch | S |
| 6 | `render_graph::compilePlan` / "merges every access's usage into the created resource" (Bloom: `StorageWrite` plus explicit `Sampled`; Final: `StorageWrite \| ColorAttachment \| TransferSrc`) | same | E-01 part 1 | Wrong usage flags are a top validation-error source | S |
| 7 | `render_graph::compilePlan` / "a HostWrite buffer is created in CPU-writable memory, others GPU-only" | same | E-01 part 1 | Memory-class choice | S |
| 8 | `render_graph::sortPasses` / "independent passes keep their add order" | `sortPasses` | none (fails today, E-05) | Pins scheduling semantics | S |
| 9 | `render_graph::planTransitions` / "rejects a pass accessing one image in two layouts" (or `compilePlan`) | plan | E-02 | Prevents silent wrong barriers | S |
| 10 | `render_graph::Usage` / "every write usage maps to a writing resource state; every image usage has a layout class and image usage flag" | `detail` tables | none | Guards the tables of E-03 | S |
| 11 | `render_graph::DrawBatchRegistry` / "a material without an instance never moves counts when its entity becomes dynamic" | handlers via `scene::Registry` | none (fails today, E-06) | Bug report | S |
| 12 | `render_graph::DrawBatchRegistry` / "untracking an entity removes it from the batch it was counted in, even after its material changed" | `track/untrack` | E-06 | Fixes replace staleness | S |
| 13 | `render_graph::gatherLighting` / "point lights beyond the maximum are dropped, the rest kept in view order" | `detail::gatherLighting` | E-08 | Lighting features | S |
| 14 | `render_graph::gatherLighting` / "a scene without lights yields zero intensities and the default direction" | same | E-08 | Defaults | S |
| 15 | `render_graph::instanceTransform` / "normal rows are the inverse-transpose under non-uniform scale" | `detail::instanceTransform` | E-08 | Shared by both geometry paths | S |
| 16 | `render_graph::makeCameraData` / "the frustum and view-projection come from the same matrices" | `detail::makeCameraData` | E-08 | Camera/culling consistency | S |
| 17 | `rhi::planImageUpload` / "rejects data whose size does not match the mip chain" | `detail::planImageUpload` | E-07 | Release over-read | S |
| 18 | `rhi::planImageUpload` / "an upload above the overflow threshold uses a dedicated buffer; below it, the ring" | same | E-07 | Staging policy | S |
| 19 | `rhi::planImageUpload` / "generateMips stages only mip 0" | same | E-07 | Mip path | S |
| 20 | `rhi::rankPhysicalDevice` / "an integrated GPU outranks a CPU device with equal limits" | `detail::rankPhysicalDevice` | E-09 | Device choice | S |
| 21 | `rhi::pickQueueFamilies` / "falls back to the graphics family for compute and transfer; never for present" | `detail::pickQueueFamilies` | E-09 | Queue policy before async compute lands | S |
| 22 | `rhi::formatName` / "names every format" (loop over `allFormats`) | `formatName`, `bytesPerTexel` | E-10's list | Completeness | S |
| 23 | `render_graph::ResourcePool` / "per-frame copies are 256-byte aligned and the offset scales with the frame index" | stride helper in `detail` | small `detail::perFrameStride` | Per-frame buffers | S |
| 24 | device: `aegis-test --headless --scene --compare tests/golden/scene.png` | whole stack | E-13 | Catches non-validation regressions | M |

---

## 6. Strategy

**Is extract-rather-than-mock enough?** For leaves, yes: the rhi suite proves it works. It stops scaling at the *orchestrators* (`FrameGraph::compile`, `UploadManager::upload`, device selection, the pass `execute` bodies), where today's rule ("lift a stateless helper into `detail`") yields at most fragments. The upgrade is not mocking. It is **plan/execute separation**: each orchestrator first computes a plain-value plan (pure, unit-tested), then a thin executor turns the plan into Vulkan calls (device-tested). `sortPasses`/`planTransitions` already show this. `compile()` just stops halfway.

**(a) Fake or null `rhi::Device`.** To be useful it would have to fake:
- `Device` (20+ create/free/get overloads, `upload`, queues, bindless heap, `capabilities`);
- `CommandBuffer` (about 30 recording calls, holding `const Device&` and `vk::raii::CommandBuffer`);
- `Queue`, `Swapchain`, `QueryPool`;
- and, through `Device::get`, `Buffer`/`Image`/`ImageView` objects that wrap VMA allocations (`Buffer::write/data()` are used by `ResourcePool::write`).

All of these are concrete classes with private `create`, `friend Device` and value semantics. Faking them means interfaces plus virtual dispatch across the whole RHI, or a second link-time implementation of the module. That is L-sized and fights the design.

What it would unlock: Renderer frame pacing, UploadManager batching and pass call sequences. But a fake cannot judge whether a barrier or layout is *right*; only the validation layers can. **Not recommended.** The only "fake device" worth having is a real software Vulkan (e).

**(b) Compile-only render-graph tests.** Recommended, and the top priority. Two things stop it today: `ResourcePool` needs `Device&`/`Renderer&` just to store declarations (B1), and `compile()` interleaves the plan with resource and pipeline creation (B2). `CommandBuffer` is not a blocker: `recordTransitions` already consumes a plan. The smallest seam is E-RENDER-01: `detail::compilePlan` plus device-free `ResourceDeclarations`.

Add a **plan snapshot** test: print the default graph's plan (order, plus per-pass `from→to` per resource) and compare it with a checked-in text. Reviewers then see every scheduling or barrier change as a diff.

**(c) Golden images via `aegis-test --screenshot`.**
- **Feasible.** `verifyOffscreenFrame` already reads back RGBA8 and writes a PNG (stb). Add `--compare <png>` with a per-pixel tolerance (e.g. max channel delta ≤ 8 on ≥ 99.5% of pixels) and write a diff image on failure.
- **References:** keep them small (the 800×600 resize point or a fixed 256×256) in `tests/golden/` (a few PNGs, not the assets submodule). Generate them on the reference platform, i.e. lavapipe in CI, which is deterministic for a pinned Mesa.
- **Vendor drivers:** GPUs differ in filtering and precision, so local runs on vendor GPUs need a looser tolerance or an opt-in.
- **Scenes:** use the deterministic `--scene` set (DamagedHelmet + cube + KloppenheimSky, fixed camera, fixed frame count).
- **Without (e)** it only runs on developer machines, which is still worth having because it catches what validation never reports (agent-notes "Verifying").

**(d) Headless engine mode.** Worth doing for the *app* layer (engine-architecture.md §3, agent-notes idea 1): it turns the examples into device tests. For the render stack it adds little beyond `aegis-test --scene`, which already drives the same `Renderer` + `addDefaultPasses` headless. The bigger risk is two setup paths drifting apart (engine-architecture §1). Do it as part of the app redesign and then build `aegis-test`'s scene mode on the same setup. Do not prioritise it for render testability.

**(e) Software Vulkan in CI.** Inferred feasible, not verified.
- **Requirements** (`Context::vulkanVersion` = 1.3, device apiVersion ≥ 1.3, dynamic rendering, synchronization2, timeline semaphores, buffer device address, descriptor indexing, timestamps) are within lavapipe's Vulkan 1.3 conformance.
- **Mesh shaders:** whether `VK_EXT_mesh_shader` is available depends on the Mesa version (recent lavapipe has it; Ubuntu 24.04's stock Mesa may not). `GpuDrivenGeometryPass::createResources` returns an error without it, so `aegis-test-scene-gpu-driven` must *skip* rather than fail when the capability is missing (needs a small change).
- **Headless:** needs no X server or surface. The LunarG SDK the job already downloads includes the validation layers, so debug runs keep sync validation.
- **Cost:** `apt-get install mesa-vulkan-drivers` (or a newer Mesa PPA), pin the ICD with `VK_DRIVER_FILES`, and run `ctest --preset linux-clang-debug -L device` in the debug job.
- **Risks:** CPU time (the IBL precompute and 600 frames; current timeouts are 300 s), lavapipe-specific driver bugs, and device choice (only one device in CI, but see E-09).
- **Windows** (SwiftShader) is possible but not worth it: Linux proves the device code once.

### Recommendation

Move to three tiers, in this order:
1. **Plan/execute separation for the orchestrators** (E-01, E-07, E-09) plus `detail` extraction of per-frame data (E-08). This is where features land, and it is GPU-free.
2. **Plan snapshot tests of the real default graph**, both paths. These are cheap and are the best regression net for scheduling and barriers.
3. **Device tests on lavapipe in CI, with tolerance-based golden images** (E-13). This is the only thing that proves the executors.

Leave `Renderer` to device tests, because it has no pure logic worth extracting once `scaleFactor` is gone. Do not build a fake `rhi::Device`. Do not split the render_graph target for test link time; it is not a testability blocker (B8).

**Stop doing:**
1. **"Test around the assert" for input validation at public boundaries** (tests/README "What we test"). It makes release builds unsafe (E-07) and leaves the invariants unverified. Keep `assert` for internal invariants only, and return `Error` for caller mistakes.
2. **Treating `--screenshot` as verification.** It saves an image nobody compares.
3. **`default:` in enum mapping tables you own** (E-03). It turns the compiler's completeness check off exactly where extensions touch.
4. Also **update tests/README**: its case count, layout tree, the "later render_graph / assets" wording and the renderer/platform scope are stale (M8, M9).
