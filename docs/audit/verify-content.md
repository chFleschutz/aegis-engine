# Verification: content and platform

## 1. Summary counts (bug and design-problem findings; polish only skimmed)

| File | Checked | Verified | Unverified | Refuted | Reclassified | Polish kept |
|---|---|---|---|---|---|---|
| findings-a-content.md | 11 | 11 | 0 | 0 | 2 (A-03, A-11) | 2 (A-05, A-12) |
| findings-b-content.md | 11 (+B-12 promoted) | 12 | 0 | 0 | 1 (B-12) | 3 (B-13, B-14, B-15) |
| findings-e-content.md | 12 | 11 | 1 (E-05) | 0 | 3 (E-04, E-09, E-11) | 2 (E-07, E-12) |

No finding was refuted. The common corrections: several "bugs" have no in-tree trigger and become latent design problems, and the GLFW failure consequences are milder than filed: GLFW guards calls after terminate, so the result is a hang or a Vulkan error, not a use-after-free or a crash (see §5).

## 2. Table

| ID | Short title | Severity (filed → verified) | Verdict | Evidence | Canonical if merged |
|---|---|---|---|---|---|
| A-CONTENT-01 | assets builds ECS entities (upward edge to aegis.scene) | design → design | VERIFIED | loader.cppm:19-28 `Desc::scene`; gltf_loader.cpp:54-57, 205-254; obj_loader.cpp:66-69; assets/CMakeLists.txt:7 `aegis-scene` PUBLIC | — |
| A-CONTENT-02 | GltfImport monolith, no CPU intermediate | design → design | VERIFIED | gltf_loader.cpp:40-61: loadMeshes→StaticMesh::create(:113), loadTextures→Texture::load*(:146,:154), MaterialInstance::create(:174), buildScene | A-CONTENT-02 |
| A-CONTENT-03 | Loaders share the engine-wide default material | bug → design (latent bug) | VERIFIED | obj_loader.cpp:68, gltf_loader.cpp:202, engine.cppm:117. No current scene combines a mutated default with another user: Helmets mutates it only on plane.obj; Eval-Scene DynamicObjects only on cube.obj; no bundled glTF has a primitive without a material | A-CONTENT-03 |
| A-CONTENT-04 | shared_ptr + Device&, no identity/cache/dedupe | design → design | VERIFIED (fact corrected) | gltf_loader.cpp:136-161 decodes per texture, not per image. The six HDR loads are in six scene classes, and one run loads one scene (eval-scene/main.cpp:351) | — |
| A-CONTENT-06 | Per-texture private sampler; glTF samplers ignored | design → design | VERIFIED | texture.cpp:84-86; bindless_heap.cppm:72 `maxSamplers{2048}`; device.cpp:134-136 returns InitializationFailed; MetalRoughSpheres.gltf asks for wrap 33071 | — (glTF-sampler sub-point overlaps B-CONTENT-04) |
| A-CONTENT-07 | glTF import drops/misreads data (4 items) | bug(1,2)/design(3,4) → same | VERIFIED | items 1-2: gltf_loader.cpp:64-117, 52-54, 209-254. Item 4: only point_light.slang:45 has `discard`. Sponza MASK confirmed in the submodule | split: B-CONTENT-05 (1), B-CONTENT-06 (2), B-CONTENT-04 (3, 4) |
| A-CONTENT-08 | Texture is a GPU type with decoders built in | design → design | VERIFIED | texture.cpp: 7 factories, stb decode inside loadFromFile:135, loadEquirectangular:157, loadFromMemory:189 | A-CONTENT-08 |
| A-CONTENT-09 | Material sync and validation pushed onto consumers | design → design | VERIFIED | material.cpp:132-139 (assert only); gpu_driven.cpp:107-115 `update(0)` once in sceneInitialized; static_mesh_render_system.cpp:50 per-frame update | validation half → B-CONTENT-01; sync half ↔ B-RENDER-04 item 3 |
| A-CONTENT-10 | Loading synchronous, graphics-queue bound | design → design | VERIFIED | upload.cppm:19-25 (16 tries, flush result ignored); device.cpp:238-241 `lastSubmitted()+1`; `transferQueue()` only declared (device.cppm:87), no callers | — |
| A-CONTENT-11 | Window has no error channel; GLFW terminated per window | bug → design (latent) | VERIFIED (facts corrected, §5) | window.cpp:18 unchecked `glfwInit()`, :28 assert, :47-54 `glfwTerminate()`, :56-69 swap. Nothing move-assigns or creates two windows | A-CONTENT-11 |
| A-CONTENT-13 | Input welded to Window/GLFW, logic untestable | design → design | VERIFIED | window.cppm:52 `friend class Input`, :61 single `m_keyHandler`; input.cpp:11-19, 62-73; only verifyInput (Win32, windowed) exercises it | A-CONTENT-13 |
| A-CONTENT-05, -12 | polish | polish | KEPT | gltf_loader.cpp:179 `setPath(material name)` confirmed; rhi/context.cpp:199,214 call GLFW | — |
| B-CONTENT-01 | Material validation assert-only; release corrupts memory | bug → bug | VERIFIED | PBR layout defaults.cpp:43-52 = 76 B, stride 80 (material.cpp:102). Release `setParameter("emissiveMap", vec4)` → material.cpp:68 copies 16 B at offset 68 into the 80-B `vector` (:157): 4-B heap overflow. Duplicate `addParameter` grows size (:85-86). Null texture → 0xFFFFFFFF sampled unguarded (cpu-driven/geometry.slang:57-63) | B-CONTENT-01 |
| B-CONTENT-02 | COLOR_0 VEC4 aborts debug, reads 3 of 4 in release | bug → bug | VERIFIED | gltf_loader.cpp:98-99 → `read3` uses `iterateAccessor<fvec3>`; fastgltf tools.hpp:689 asserts on the type; the stride comes from accessor.type (:753). No bundled model has COLOR_0 | — |
| B-CONTENT-03 | Data-URI images fail the whole load | bug → bug | VERIFIED | fastgltf.cpp:2460-2466 → decodeDataUri returns `sources::Array` (:718-722; no mapCallback set). gltf_loader.cpp:156-159 catch-all → UnsupportedFormat, :162-163 aborts the load | — |
| B-CONTENT-04 | glTF material semantics dropped; Sponza renders wrong | bug → bug | VERIFIED (fact corrected) | gltf_loader.cpp:169-198 (no alphaMode/doubleSided; `make_vec3` drops alpha). rhi/pipeline.cppm:73 defaults to Back. No `discard` in geometry shaders. Sponza.gltf materials 0, 3, 20: MASK 0.5 + doubleSided | B-CONTENT-04 |
| B-CONTENT-05 | Primitive topology ignored | bug → bug | VERIFIED | `primitive.type` never read (gltf_loader.cpp:64-117); mesh_preprocessor.cpp:42-44 only checks `%3`. Bundled content is mode 4 only (content-dependent trigger) | B-CONTENT-05 |
| B-CONTENT-06 | Every node instantiated, not the selected scene | bug → bug | VERIFIED | gltf_loader.cpp:52-54 sceneIndex used for the name only; :209-254 loops all nodes and parents orphans to the root. Bundled files: 1 scene, all nodes reachable | B-CONTENT-06 |
| B-CONTENT-07 | Polled input ignores ImGui focus | bug → bug | VERIFIED | no `WantCapture*` in src/aegis; editor_layer.cppm:34-46 Q/W/E/R; default camera gets KinematicMovementController (scene_defaults.cppm:32, W=forward); EditorLayer always pushed (engine.cppm:60); scene_panel.cppm:173 InputText | B-APP-08 |
| B-CONTENT-08 | Mouse-look sensitivity depends on frame rate | bug → bug | VERIFIED | kinematic_movement_controller.cppm:92-99: the cursor delta is added to `rotate`, then `rotate *= deltaSeconds` | B-APP-09 |
| B-CONTENT-09 | Models without normals rejected | design → design | VERIFIED | gltf_loader.cpp:78-79; obj_loader.cpp:43-44; mesh_preprocessor.cpp:36-40 | — |
| B-CONTENT-10 | Window owns process-global GLFW state | design → design | VERIFIED (facts corrected, §5) | window.cpp:47-54, 56-69; GLFW window.c:1041, 1169, 504, 723 | A-CONTENT-11 |
| B-CONTENT-11 | Input::bind has no unbind; dispatch not reentrant | design → design | VERIFIED | input.cpp:57-60 `emplace_back`; :68-72 range-for by reference calls the callback. Binding the same key in a callback can reallocate the vector mid-loop. Only aegis-test calls `bind` | B-CONTENT-11 |
| B-CONTENT-12 | Texture::create(desc, mip0) does not check size | polish → design (latent bug) | VERIFIED | texture.cpp:97-117 forwards unchecked; upload_manager.cpp:156 assert only, then :164-168 memcpy per footprint, so release over-reads a short span. No in-tree caller passes a wrong size | B-CONTENT-12 |
| B-CONTENT-13, -14, -15 | polish | polish | KEPT | mesh_preprocessor.cpp:133-134 uint8 casts, geometry.slang:11-12; sampler.cppm:36-37 linear default with RGBA32F texture.cpp:204; loader.cpp:16-20, texture.cpp:124 exact-case compare | B-CONTENT-15 ← E-CONTENT-12 |
| E-CONTENT-01 | No CPU intermediate for model import | design → design | VERIFIED | same as A-CONTENT-02 | A-CONTENT-02 |
| E-CONTENT-02 | Material mapping and PBR template coupled by string literals | design → design | VERIFIED | defaults.cpp:43-52; gltf_loader.cpp:180-193; common.slang:101-113 mirrors insertion order; unknown names stored then ignored (material.cpp:138) | — |
| E-CONTENT-03 | Material validation assert-only | bug → bug | VERIFIED | scratch compile: `MaterialValue v = 1` selects index 0 (int32_t), so `setParameter("roughness", 1)` writes integer bits. Other scenarios as in B-CONTENT-01 | B-CONTENT-01 |
| E-CONTENT-04 | Default material is one shared mutable instance | bug → design (latent) | VERIFIED | same as A-CONTENT-03; helmets/main.cpp:19-20, 62-65 | A-CONTENT-03 |
| E-CONTENT-05 | OBJ UVs not flipped | bug → bug | UNVERIFIED | No v flip in obj_loader.cpp:49-52, and no stbi flip anywhere. But plane.obj's UVs are rotated 180° (corner (5,5,0)→uv(0,0), (-5,-5,0)→(1,1)), so the predicted "painting upside down" can't be derived from code. Needs a Simple-Scene screenshot (examples not run) | — |
| E-CONTENT-06 | glTF instantiates every node | bug → bug | VERIFIED | as B-CONTENT-06 | B-CONTENT-06 |
| E-CONTENT-08 | Image decode reachable only through GPU creation | design → design | VERIFIED | texture.cpp:119-195 (decode then `create` in each) | A-CONTENT-08 |
| E-CONTENT-09 | Texture::create(desc, mip0) size unchecked | bug → design (latent) | VERIFIED | traced: upload_manager.cpp:156, 164-168; the OOB read needs a caller passing too few bytes, and no in-tree caller does | B-CONTENT-12 |
| E-CONTENT-10 | Key-binding dispatch only via live GLFW window | design → design | VERIFIED | input.cpp:11-19 ctor calls GLFW and writes `m_keyHandler`; `onKey` private | A-CONTENT-13 |
| E-CONTENT-11 | Bindings not removable; bind-in-callback UB | bug → design (latent) | VERIFIED | as B-CONTENT-11; no caller besides verifyInput | B-CONTENT-11 |
| E-CONTENT-13 | Window: no error channel, terminate per window | design → design | VERIFIED (facts corrected) | as A-CONTENT-11 | A-CONTENT-11 |
| E-CONTENT-14 | Import all-or-nothing; unsupported data dropped silently | design → design | VERIFIED | gltf_loader.cpp:162-163 (first bad texture fails the model), :180 alpha dropped; `Error` has no warning list | — (dropped-field list overlaps B-CONTENT-04) |
| E-CONTENT-07, -12 | polish | polish | KEPT | gltf_loader.cpp:125-132 (see §5: sRGB wins, not last writer) | E-CONTENT-12 → B-CONTENT-15 |

## 3. Refuted findings
None. No cited fact is contradicted by the code. Several failure consequences are corrected (§5), and four "bugs" are reclassified as latent design problems because no in-tree code path triggers them:
- A-CONTENT-03 and E-CONTENT-04: the shared default material.
- A-CONTENT-11: GLFW terminated per window.
- E-CONTENT-09: unchecked mip0 size.
- E-CONTENT-11: binding inside a callback.

## 4. Merged duplicates
- **B-CONTENT-01** ← E-CONTENT-03, A-CONTENT-09 (validation half).
- **B-CONTENT-04** ← A-CONTENT-07 items 3-4. Partial overlaps: E-CONTENT-14 (dropped-field list; its warning-channel policy stays separate), E-CONTENT-07 (sRGB conflict sub-point), A-CONTENT-06 (glTF-sampler sub-point; per-texture sampler ownership stays separate).
- **B-CONTENT-05** ← A-CONTENT-07 item 1.
- **B-CONTENT-06** ← A-CONTENT-07 item 2, E-CONTENT-06.
- **B-APP-08** (app group) ← B-CONTENT-07, A-APP-06.
- **B-APP-09** (app group) ← B-CONTENT-08, E-APP-07.
- **B-CONTENT-11** ← E-CONTENT-11.
- **B-CONTENT-12** ← E-CONTENT-09, E-RENDER-07 (the same release over-read at its RHI site), A-CONTENT-08 (mip0 sub-fact).
- **B-CONTENT-15** ← E-CONTENT-12.
- **A-CONTENT-02** ← E-CONTENT-01.
- **A-CONTENT-03** ← E-CONTENT-04.
- **A-CONTENT-08** ← E-CONTENT-08.
- **A-CONTENT-11** ← B-CONTENT-10, E-CONTENT-13.
- **A-CONTENT-13** ← E-CONTENT-10. Partial: E-APP-06 (GLFW-polled Input state).
- **B-RENDER-04** (render group; A-RENDER-01 likewise) ← A-CONTENT-09 sync half: static-entity material edits never reach the GPU-driven path. B-RENDER-04 also covers the static+dynamic shared-material race.

## 5. Corrections to facts
1. **Move-assigning a Window: a hang, not a use-after-free** (B-CONTENT-10(a), A-CONTENT-11). After `glfwTerminate`, GLFW guards every call with `_GLFW_REQUIRE_INIT` (window.c:1169 `glfwPollEvents`, :723 `glfwGetFramebufferSize`, :504 `glfwWindowShouldClose`). So `window = Window{...}` is not a use-after-free. The new window is destroyed, and the size reads 0, so `isMinimized()` is true. `shouldClose()` returns false forever, and `Engine::run` (engine.cppm:152-165) spins in the minimized/`waitEvents` branch. It becomes a real use-after-free only if GLFW is initialized again later, for example by another `Window` constructor. The lost key handler (`m_keyHandler` swapped to the temporary) is confirmed.
2. **No display: no crash at `glfwSetWindowUserPointer`** (B-CONTENT-10(c), A-CONTENT-11, E-CONTENT-13). When `glfwInit` fails, `glfwSetWindowUserPointer(nullptr, …)` returns early, because `REQUIRE_INIT` precedes the null assert (window.c:1041-1044). In release, `glfwGetRequiredInstanceExtensions` returns 0 extensions, and `glfwCreateWindowSurface` returns `VK_ERROR_INITIALIZATION_FAILED` (vulkan.c:306). The failure surfaces in `rhi::Context` surface creation, and GLFW's own reason is never printed (no error callback). A crash happens only if `glfwInit` succeeds and `glfwCreateWindow` fails.
3. **B-CONTENT-01:** `parameter(name)` on an unknown name dereferences null only if no override was stored under that name. After `setParameter(typo, …)`, the override is returned (material.cpp:144-145).
4. **B-CONTENT-04:**
   - The color-space choice is "any sRGB use wins", not last-writer-wins (gltf_loader.cpp:125-132). No bundled glTF has a color/data conflict.
   - `aegis-test` also sets a `cullMode` (test/main.cpp:211).
   - Sponza's 3 MASK materials (indices 0, 3, 20, cutoff 0.5) are the same 3 that are doubleSided.
5. **A-CONTENT-04:** Eval-Scene's six `KloppenheimSky.hdr` loads are in six separate scene classes. `main()` loads one scene, so a run loads the HDR once. The dedupe case rests on images shared by several textures within one glTF, and on repeated `loadModel`/`loadEnvironment` calls.
6. **A-CONTENT-07:** the Sponza MASK usage is confirmed in the bundled submodule, not only inferred.
7. **A-CONTENT-13:** after a `Window` move, the stale reference is `Input::m_window`, which then points at a moved-from window with a null handle. The handler's captured `Input*` stays valid. `Input`'s doc already forbids moving the window.
8. **B-CONTENT-12 / E-CONTENT-09:** only a *short* `mip0` over-reads. Longer data is silently truncated and gives a wrong texture, with no out-of-bounds read.
9. **E-CONTENT-05:** `plane.obj` maps corner (5,5,0)→uv(0,0) and (-5,-5,0)→uv(1,1). The asset's UVs are therefore rotated against the usual layout, and a v-flip alone may not make Simple-Scene's painting correct. The convention gap is real; the visual claim is unconfirmed.

## 6. Run log
- `clang++ -std=c++23 -I external/glm build/audit/verify/content/variant.cpp` → exit 0. Output: `int literal -> index 0, float -> 2, vec4 -> 5 (size 16)`. This confirms the int-literal path in E-CONTENT-03 (MSVC STL, clang).
- GPU lock taken (`mkdir build/audit/verify/gpu.lock`). `build/audit/src/aegis/test/aegis-test.exe --headless --scene --frames 60` → exit 0, stderr empty (run-scene.err, 0 lines). Output: "Offscreen scene frame verified: 800x600 target read back, not uniform", plus the upload and sampler checks. Lock released. This is a baseline: the current loaders (DamagedHelmet, cube.obj) run clean. No reported scenario is reachable through `aegis-test`, which can't load Sponza or arbitrary glTF.
- Python (`-I`) JSON inspection of `modules/aegis-assets/*/*.gltf`:
  - Sponza: 25 materials, 3 MASK + doubleSided, 1 Repeat sampler.
  - MetalRoughSpheres: sampler wrap 33071 (ClampToEdge).
  - All four files: primitive mode 4 only, no COLOR_0, no primitive without a material, 1 scene, all nodes reachable from it, no data-URI images.
- Source reads of external fastgltf (tools.hpp:689, fastgltf.cpp:672-722, 2440-2470) and GLFW 3.5 (window.c, vulkan.c) to trace the library behaviour. No project builds, no git commands.
