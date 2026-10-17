# Verification: render stack

Scope: `docs/audit/findings-a-render.md`, `findings-b-render.md` and `findings-e-render.md`. I re-read every cited symbol in the code. I reproduced the scheduler claims by running `sortPasses`, copied verbatim, as a standalone program, and the pool-reallocation claim with a standalone model of the pool. I ran all four device baselines. Paths are relative to `src/aegis/` unless stated.

## 1. Summary counts (bug and design-problem findings; polish counted separately)

| File | verified | unverified | refuted | reclassified | polish |
|---|---|---|---|---|---|
| findings-a-render.md (10) | 10 (A-01 with corrections) | 0 | 0 | 0 | A-11, A-12 kept |
| findings-b-render.md (8) | 8 (B-03 sub-claim (c) refuted) | 0 | 0 | 2 (B-02 bug → bug (latent); B-04 bug (latent) → bug) | B-09 verified and reclassified polish → bug (latent); B-10, B-11 kept |
| findings-e-render.md (11) | 10 | 1 (E-09) | 0 | 1 (E-07 bug → bug (latent) + design problem) | E-10, E-11 kept |

No finding is refuted as a whole. One sub-claim is refuted: B-RENDER-03 (c).

## 2. Verdict table

| ID | short title | severity (filed → verified) | verdict | evidence | canonical |
|---|---|---|---|---|---|
| A-RENDER-01 | GPU-driven static snapshot vs live batch registry | bug → bug | VERIFIED, with corrections (§5) | `gpu_driven.cpp:105-115`: only `sceneInitialized` writes StaticInstances. `culling.slang:60-63`: no `drawID < batch.count` check. Editor destroy at `editor/panels/scene_panel.cppm:282` | B-RENDER-04 |
| A-RENDER-02 | `Device::get` reference dangles when the pool grows (UploadManager) | bug → bug | VERIFIED (reproduced as a model) | `device.cpp:251` passes `get(dst)`. `upload_manager.cpp:126` calls `stage` → `:256` `createBuffer` → `resource_pool.cppm:112` `emplace_back` → `:131` `copyBuffer(…, dst, …)`. Standalone model: dangling=true at capacity 256 | A-RENDER-02 |
| A-RENDER-03 | Pass resources declared twice, never cross-checked | DP → DP | VERIFIED | `frame_graph.cpp:44-52,72-83` take edges and barriers only from `info()`. `resource_pool.cpp:212-218` takes usage flags from every entry | A-RENDER-03 |
| A-RENDER-04 | String wiring, first-match resolve, duplicated format literals | DP → DP | VERIFIED | `resource_pool.cpp:190-196` (first match, `break`). `ui_pass.cpp:50`, `screen_passes.cpp:41,46`, `post_effects.cpp:211,214` | A-RENDER-04 |
| A-RENDER-05 | No persistent or imported images; no read-before-overwrite | DP → DP | VERIFIED | `schedule.cpp:127-130`: an image's first use comes from undefined. `:48-58`: readers depend on the final producer only | A-RENDER-05 |
| A-RENDER-06 | Renderer owns graph textures; never freed on graph rebuild | DP → DP | VERIFIED | `renderer.cpp:137`: only `emplace_back`, literal `1.0`. `resource_pool.cpp:36`: frees `ownedByPool` only. `renderer.cppm` has no release API. `frame_graph.cpp:92`: timers append | A-RENDER-06 |
| A-RENDER-07 | Passes read and write the ECS; registry reached two ways | DP → DP | VERIFIED | `scene_update_pass.cpp:28-30` (aspect write-back). `gpu_driven.cpp:89-90` (ctor ref) vs `:205` (`frameInfo.drawBatches`) | A-RENDER-07 |
| A-RENDER-08 | Pipelines not pooled; only pooled resources can be retired | DP → DP | VERIFIED | `device.cppm:244`: `retireValue` is private. `createPipeline` returns a value (`device.cpp:280-298`) | A-RENDER-08 |
| A-RENDER-09 | Three queues that may alias one VkQueue | DP → DP | VERIFIED | `device.cpp:497-501` (fallback), `:512` `getQueue(family, 0)`. No callers of `computeQueue()`/`transferQueue()` outside rhi | A-RENDER-09 |
| A-RENDER-10 | Materials have no pipeline; CPU and GPU paths disagree | DP → DP | VERIFIED (latent) | `static_mesh_render_system.cpp:45` filters by `type()`. `gpu_driven.cpp:325-340` draws every batch with one pipeline. No `MaterialType::Transparent` anywhere | A-RENDER-10 |
| B-RENDER-01 | Failed swapchain recreate frees old views first; retry frees them again | bug → bug | VERIFIED (code path; trigger is rare) | `device.cpp:305-311` frees before `Swapchain::create`. `renderer.cpp:189-193` returns and leaves `m_needsResize`. `engine.cppm:166-176`: `renderFrame` runs in the same iteration | B-RENDER-01 |
| B-RENDER-02 | `resize` swallows failures; Bloom has 6 fixed mips; zero extent | bug → bug (latent) | VERIFIED facts; triggers not reachable in practice today (§5) | `renderer.cpp:221-240` (`println` + `continue`). `post_effects.cpp:23-24` (6 mips). `image.cpp:75-77` (no clamp). `platform/window.cppm:37` | B-RENDER-02 |
| B-RENDER-03 | Frame-loop errors lost; submit/present failure; ImGui `ImageCount` literal | bug → bug | (a), (b) VERIFIED; (c) REFUTED (§3) | `renderer.cpp:144-146` (silent return). `:347-351` (returns before the slot advances). `queue.cpp:27,71` (increment before submit). `ui_pass.cpp:60-61` | B-RENDER-03 |
| B-RENDER-04 | Static instances are a one-time snapshot | bug (latent) → bug | VERIFIED. Cases 1-2 are reachable today through the editor and `loadModel` | Same as A-01. `material.cpp:152-160`: `update` writes only when dirty. `engine.cppm:197-204`: `loadScene` has no `waitIdle` (case 4 stays latent) | B-RENDER-04 |
| B-RENDER-05 | `DrawBatchRegistry` limits and lifecycle guarded only by assert | DP → DP | VERIFIED | `draw_batch_registry.cpp:65` (assert only), `:23-38` (`connect` never clears), `:77-79`. `rhi/buffer.cpp:24`: no bounds check | B-RENDER-05 |
| B-RENDER-06 | Upload and deletion ordering contracts unstated | DP → DP | VERIFIED | `upload_manager.cpp:131`: no barrier before the copy. `:224-225`: `nullopt` on failure. `:297`: `std::ignore` on wait. `device.cpp:238-241` | B-RENDER-06 |
| B-RENDER-07 | Resolution-dependent textures never freed | DP → DP | VERIFIED | Same evidence as A-06 | A-RENDER-06 |
| B-RENDER-08 | Readers depend on the last writer; no write-after-read edge | DP → DP | VERIFIED (reproduced) | Verbatim `sortPasses` on A (creates X), B (reads X), C (writes ref X) → order `0 2 1` | A-RENDER-05 |
| B-RENDER-09 | `DepthWrite` access mask lacks read | polish → bug (latent) | VERIFIED | `vulkan_conversions.cppm:208-214`: only `eDepthStencilAttachmentWrite`. A second depth pass with Load would not have its load and test reads covered | B-RENDER-09 |
| E-RENDER-01 | Graph declarations and plan need a GPU | DP → DP | VERIFIED | `resource_pool.cpp:26-30`: ctor needs `Device&` and `Renderer&`. `frame_graph.cpp:26-93` interleaves pure and GPU steps | A-RENDER-03 |
| E-RENDER-02 | A pass touching one resource twice gets both barriers up front | DP → DP | VERIFIED (latent) | `frame_graph.cpp:72-83`: reads then writes. `render_pass.cppm:50` doc says "in the order it accesses them". `frame_graph.cpp:151-152` | E-RENDER-02 |
| E-RENDER-03 | `default:` in the Usage tables | DP → DP | VERIFIED | `render_graph/resource.cppm:71,105,143,154`. `toResourceState` has no `default` | E-RENDER-03 |
| E-RENDER-04 | Reference resolution checks name only, not kind or uniqueness | DP → DP | VERIFIED | `resource_pool.cpp:190-199`, `:212-218` (ORs image and buffer usage into every entry) | A-RENDER-04 (kind check is E-04's addition) |
| E-RENDER-05 | Independent passes reorder; hidden CPU dependencies | DP → DP | VERIFIED (reproduced + live) | Two independent passes → `1 0`. Live run prints "Instance Update \| Scene Update \| …", while `default_graph.cpp:10,13` adds Scene Update first | E-RENDER-05 |
| E-RENDER-06 | DrawBatchRegistry counter underflow (`Material{}` + DynamicTag) | bug → bug | VERIFIED by trace, with a concrete editor trigger (§5) | `draw_batch_registry.cpp:128-131,84-95`. `scene_defaults.cppm:26`. `scene_panel.cppm:309,282`. entt 3.15 `registry.hpp:540-543` destroys pools in reverse | E-RENDER-06 |
| E-RENDER-07 | Image upload size check is assert-only | bug → bug (latent) + DP | VERIFIED; needs a caller bug | `upload_manager.cpp:156` (assert), `:164-168` (memcpy). `assets/texture.cpp:111` passes the caller's span unchecked | B-CONTENT-12 |
| E-RENDER-08 | Per-frame CPU packing buried in `execute`, duplicated | DP → DP | VERIFIED | `gpu_driven.cpp:51-52` and `static_mesh_render_system.cpp:56` (same normal matrix). `scene_update_pass.cpp:30` (no zero guard) | E-RENDER-08 |
| E-RENDER-09 | Device scoring ties CPU and integrated GPUs | bug → bug (unconfirmed) | UNVERIFIED (mechanism holds; needs a multi-ICD machine; this host has one RTX 4070) | `device.cpp:445-452` | E-RENDER-09 |
| E-RENDER-12 | Partial `resize` failure leaves a view on a retired image | bug → bug (rare) | VERIFIED, with a correction (§5) | `renderer.cpp:211-240`. `device.cpp:165-189` | B-RENDER-02 |
| E-RENDER-13 | Device verification coarse; never in CI | DP → DP | VERIFIED | `.github/workflows/build.yml:77,131` (`-LE device`). `test/main.cpp:1084,1133` ("not uniform") | E-RENDER-13 |
| polish A-11, A-12, B-10, B-11, E-10, E-11 | — | polish | KEPT (B-10 fact verified at `device.cpp:85-97,110-122`) | — | A-RENDER-11 ← B-RENDER-10, D-02. E-RENDER-11 → A-RENDER-04 |

## 3. Refuted findings

**B-RENDER-03 (c), present failure desynchronizes ImGui's buffer ring from the renderer's slots (refuted sub-claim).** `endFrame` stores `timePoint = *newSubmitTime` (`renderer.cpp:352`) before it presents. So after a failed present, the next frame reuses the same slot and `beginFrame` waits on that latest submission (`renderer.cpp:300`). On a single queue that completes all earlier frames. Every ImGui vertex/index buffer the next frames reuse therefore belongs to a completed frame. With `maxFramesInFlight == 2 == ImageCount`, nothing in flight is overwritten.

What survives is narrower: the literal `.ImageCount = 2` (`ui_pass.cpp:60-61`) becomes a real race only if `maxFramesInFlight` is raised above 2. That is polish/latent and should be fixed by passing `Renderer::maxFramesInFlight`. Parts (a) and (b) stand:
- (a) Errors are dropped silently.
- (b) After a failed submit, `imageAvailable` stays signalled and the next acquire reuses it.

## 4. Merged duplicates (canonical ← others)

- **B-RENDER-04** (bug) ← A-RENDER-01, B-APP-06, A-APP-01 (partly: the static/dynamic rule; its transform part is app-only).
- **A-RENDER-02** (bug): no duplicate found.
- **B-RENDER-02** (bug, latent) ← E-RENDER-12 (scenario 2 of B-02).
- **A-RENDER-06** (DP) ← B-RENDER-07.
- **A-RENDER-03** (DP) ← E-RENDER-01. Both propose the same device-free declaration table plus a pure compile plan.
- **A-RENDER-04** (DP) ← E-RENDER-04 (duplicate-creator part; its kind check rides along), E-RENDER-11 (polish, formats), D-08 (name literals), D-01 (partly: G-buffer declared twice).
- **A-RENDER-05** (DP) ← B-RENDER-08 (the write-after-read / read-before-overwrite ordering is A-05 part (b)).
- **E-RENDER-05** (DP): related to A-RENDER-05, but a separate defect (tie-break, not edges).
- **A-RENDER-07** (DP) ← B-APP-10 (partly: aspect lag from the write-back in `SceneUpdatePass`).
- **E-RENDER-08** (DP) ← D-06 (normal matrix computed twice).
- **A-RENDER-08** (DP) ← A-BUILD-04 (shader path strings, pass-owned pipelines).
- **A-RENDER-09** (DP) ← D-18 (partly: unused queue accessors).
- **A-RENDER-11** (polish) ← B-RENDER-10, D-02.
- **B-RENDER-03** (bug) ← D-22 (partly: `println`-and-continue in the frame loop), A-FOUND (logging remark at `findings-a-foundation.md:57`).
- **B-CONTENT-12** (bug) ← E-RENDER-07, E-CONTENT-09 (A-CONTENT-08 cites the same assert).
- **E-RENDER-13** (DP) ← A-BUILD-10, A-BUILD-11 (partly), E-BUILD lavapipe item C10.
- **B-RENDER-06** (DP): related to A-CONTENT-10 (same `flushPending`/`reclaimFront` path, different angle), not merged.

## 5. Corrections to facts

1. **A-RENDER-01: the out-of-bounds write is not reachable on destroy.** A-01 says the extra append "writes one element past the end of both buffers". Capacity starts at 65536 + 4096 and grows by `bit_ceil` (`gpu_driven.cpp:27`, `gpu_driven.cppm:22-28,59-60`), and it never shrinks. After a destroy the registry count drops below the capacity, so the extra write lands at index `count_new`: either the next batch's first slot or slack inside the buffer.
   - With today's single template, the visible effect is this. `drawMeshTasksIndirectCount` clamps to the new `instanceCount` (`gpu_driven.cpp:339-340`), and `InterlockedAdd` order is nondeterministic. So the destroyed instance can still be drawn while a live one drops out (flicker).
   - The freed-mesh read (`mesh->bounds` and `meshletCount` through a stale address, `culling.slang:55,65`) stands.
2. **B-RENDER-04: the trigger exists today.** "Nothing destroys entities" is wrong. The editor's "Destroy Selected Entity" (`scene_panel.cppm:282`) and `Engine::loadModel` after `loadScene` both reach cases 1-2 on the default GPU-driven path (`engine.cppm:100-102`). Severity goes back to bug.
3. **A-RENDER-02: growth factor.** MSVC's vector grows 256 → 384, not 512 (observed in the standalone run). The trigger is narrow: a buffer upload over 32 MB (`upload_manager.cppm:84`) while the buffer pool's free list is empty and `size == capacity`. Overflow staging buffers return their slot to the free list at flush, which makes repeats less likely. Image uploads are safe: the image reference is into `m_images`, and staging only grows `m_buffers`.
4. **B-RENDER-01: the failure is immediate, and unrecoverable anyway.** `Engine::run` calls `renderFrame` in the same iteration after a failed `resize` (`engine.cppm:166-176`), so the stale handle is used right away. Also, per the Vulkan spec, `oldSwapchain` is retired even when `vkCreateSwapchainKHR` fails, so the next acquire on it is invalid whatever happens to the views. The fix must treat a failed recreate as fatal or retry before rendering.
5. **B-RENDER-02: minimize check and mip limit.** `isMinimized()` is `m_width == 0 || m_height == 0` (`platform/window.cppm:37`), not "0x0". The mip limit uses the larger dimension, so 6 mips are invalid only when both dimensions are under 32 px. Windows' minimum tracking width for a decorated window prevents that. There is no caller of offscreen `resize({0,0})`. Hence "bug (latent)".
6. **E-RENDER-12 / B-RENDER-02 scenario 2: only `vkCreateImageView` failure triggers it.** In `Device::replace(ImageViewHandle…)` the bindless writes reuse the old slots (`bindless_heap.cpp:57-60`) and cannot fail when the usage is unchanged, which is the case in `resize`. Only a `vkCreateImageView` failure triggers it.
   - A related case is not in the findings. If a replace adds Storage and the storage pop fails after the sampled write, the old sampled slot already points at the new view, which is then destroyed immediately (`device.cpp:170-183`). That is the D-02 leak family.
7. **E-RENDER-06: concrete trigger, no "unverified" caveat.** The Material storage is created before DynamicTag's (`engine.cppm:260` → `draw_batch_registry.cpp:28,34-37` on an empty registry), and entt destroys pools in reverse.
   - So: select the main camera (DynamicTag, `scene_defaults.cppm:26`) → Add Component "Default Material" (null instance) → Destroy. `onDynamicDestroy` then runs `setDynamic(false)`: `m_dynamicCount` wraps to UINT32_MAX when no dynamic mesh exists, and `m_staticCount` goes up by 1.
   - `instanceCount()` wraps back to the right total, so culling is unaffected. What happens is a failed multi-hundred-GB `resizeBuffer` plus a log line every frame (`gpu_driven.cpp:38-41,119`), and the GPU-driven path only. The editor cannot add DynamicTag, so the add-tag variant is not reachable from the UI.
8. **E-RENDER-05: wording.** The order is a reversed Kahn traversal from sinks. It is the reverse of add order for independent sources, not in general. The live run confirms the reorder (Instance Update before Scene Update).
9. **E-RENDER-09: what drives the score.** The +1000 discrete bonus is smaller than the gap between common `maxImageDimension2D` values (16384 vs 32768). The score is therefore dominated by the image limit, not the device type (inference; not run on multi-GPU hardware).
10. **E-RENDER-07: where the size check is missing.** `Texture::create(device, desc, mip0)` (`assets/texture.cpp:97-112`) is the public entry with no size check. The fix belongs at that boundary too (B-CONTENT-12).
11. **Outside this group, found in passing:** `Scene::reset()` is an empty TODO (`scene/scene.cppm:74-77`). So a second `loadScene` appends a second default scene to the same registry. Already covered by C-03.

## 6. Run log

All runs used `build/audit/src/aegis/test/aegis-test.exe` (debug, core + sync validation), one at a time under `build/audit/verify/gpu.lock`. Output is in `build/audit/verify/render/`.

| Command | Exit | Key output |
|---|---|---|
| `--headless --frames 120 --screenshot triangle.png` | 0 | All `verify*` lines pass, including "Overflow upload verified: 80 MB" and "Renderer textures verified: 10 mips, bindless slot 5 kept across a resize". No VUID, validation or SYNC-HAZARD lines. |
| `--headless --scene --frames 120 --screenshot scene.png` | 0 | "Frame graph compiled: Scene Update \| Geometry \| Lighting \| Sky Box \| Transparent \| Bloom \| Post Processing \| UI \| Present". "Offscreen scene frame verified: 800x600 … not uniform". Resize at frame 60 is clean. No validation output. |
| `--headless --gpu-driven --frames 120 --screenshot gpu.png` | 0 | "Frame graph compiled: Instance Update \| Scene Update \| Clear Draw Counts \| Culling \| GPU Driven Geometry \| …". This is evidence for E-RENDER-05: add order is Scene Update first. No validation output. |
| `--frames 600` (windowed) | 0 | "Input verified: key state, key binding … and mouse button". "Timed pass verified: 598 frames". No validation output. |
| `build/audit/tests/render_graph/aegis-tests-render_graph.exe` | 0 | 21/21 cases, 53 assertions pass |
| `build/audit/tests/rhi/aegis-tests-rhi.exe` | 0 | 8529 assertions pass |
| `clang++ -std=c++23 sort_passes.cpp` (verbatim `schedule.cpp:16-95`) | 0 | `A,B(read X),C(write ref X) -> 0 2 1`; `two independent -> 1 0`; `SceneUpdate,InstanceUpdate,Culling -> 1 0 2` |
| `clang++ -std=c++23 pool_realloc.cpp` (model of `rhi::ResourcePool` storage) | 0 | `capacity 384 -> … dangling=true` |

Screenshots (viewed):
- `triangle.png`: 640x480. A single inverted RGB-interpolated triangle (blue top-left, green top-right, red bottom) on white.
- `scene.png` (CPU path) and `gpu.png` (GPU-driven): 800x600. A sci-fi helmet, lit and textured, over the cloudy-sky environment, with the ImGui overlay listing 9 and 12 passes respectively. The two images look the same: no missing or ghost geometry, no black or garbage regions.
