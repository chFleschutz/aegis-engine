# Findings b: shaders and build (lens: bugs)

Phase 2 of the foundation audit, group SHADERS AND BUILD. This is a read-only analysis with no project builds. Evidence comes from:
- the source tree;
- `build/audit/` (Clang debug, SDK 1.4.328.1) and its `build.ninja`, `CMakeCache.txt` and `CTestTestfile.cmake`;
- `spirv-val` and `spirv-dis` from SDK 1.4.328.1;
- `slangc -O3 -reflection-json` output written only to `build/audit/reflect/`.

## Method and baseline

- **spirv-val, Debug:** I ran `spirv-val --target-env vulkan1.3` and `vulkan1.4` with `--scalar-block-layout` on all 16 built `.spv` files. All pass. Without `--scalar-block-layout`, `cpu-driven/geometry`, `gpu-driven/{culling,geometry,task}` fail, which is expected because they rely on scalar layout and the device enables `scalarBlockLayout`.
- **spirv-val, Release flags:** I recompiled all 15 shaders under `shaders/` with the Release flags (`-O3`) into `build/audit/reflect/`. All pass `spirv-val --target-env vulkan1.3 --scalar-block-layout`.
- **Push-constant offsets (reflection JSON):** I compared all 15 push-constant blocks against their C++ twins. Every member offset and every total size matches: cpu geometry 112, culling 64, indirectDraw 48, lighting 56, point light 48 with the pointer at 40, sky box 72, post 28, bloom 16/20/16, IBL 12/12/20/4.
- **Pointer strides (`ArrayStride` in the SPIR-V):** `Instance` 112, `Meshlet` 32, `Mesh` 72, `Camera` 300, `Vertex` 44, `PbrMaterial` 76, `BoundingSphere` 16, `DrawBatch` 8, `DrawMeshTasksIndirectCommand` 12. Each equals the `sizeof` of its C++ twin (`InstanceData`, `assets::Meshlet`, `StaticMesh::GpuData`, `CameraData`, `assets::Vertex`, `addParameter` order, `DrawBatchData`, `indirectCommandSize`).
- **Enum values:** `LightingViewMode` 0..5 matches `lighting.slang`'s `switch` 1..5. `PostProcessingPass::ToneMapping {Reinhard, Aces}` matches `post_process.slang` cases 0 and 1.
- **SPIR-V capabilities vs. enabled features:** every capability the shaders declare has its feature enabled in `Device::createFeatureChain`. The one gap is subgroup-stage support for the task shader, covered in B-BUILD-02.
- **Asset path case:** all 10 hard-coded asset paths in `src/` and `examples/`, and all 86 URIs inside the `.gltf`, `.obj` and `.mtl` files, match the on-disk names exactly (checked case-sensitively).

So the layout mirrors and the SPIR-V are correct today. The bugs below are in shader logic, in the GPU-driven task stage, and in CMake path wiring.

## Findings

### B-BUILD-01: Task shader emits mesh tasks from one invocation, in non-uniform control flow, without a barrier
- **Where:** `shaders/gpu-driven/task.slang` : `taskMain`
- **Observed:**
  - The shader calls `DispatchMesh` from one invocation only: `if (groupThreadID.x == 0) { ...; DispatchMesh(visibleCount, 1, 1, sharedPayload); }`.
  - The other 31 invocations write `sharedPayload.meshletIDs[visibleIndex]` and then return without calling it.
  - `spirv-dis` of `build/audit/shaders/gpu-driven/task.slang.spv` shows `OpEmitMeshTasksEXT` inside the `OpBranchConditional` block guarded by the `== 0` compare.
  - The module contains zero `OpControlBarrier` instructions.
- **Inference:**
  - SPIR-V requires every invocation of a task workgroup to execute `OpEmitMeshTasksEXT` exactly once, under uniform control flow. Running it from invocation 0 alone is undefined behaviour. It works on drivers that only read the first invocation's arguments.
  - Independently, invocation 0 hands `sharedPayload` to the mesh stage with no `GroupMemoryBarrierWithGroupSync()` after the other invocations' writes.
  - Failure scenario: on a driver or GPU where the 32 invocations are not one lock-stepped subgroup (see B-BUILD-02), the mesh groups read stale `meshletIDs`. Meshlets then go missing or are duplicated, or the driver behaves arbitrarily.
  - Neither `spirv-val` nor the validation layers flag this, so the `aegis-test-scene-gpu-driven` device test passes on the developer's GPU.
- **Severity:** bug
- **Confidence:** medium (the code shape is confirmed in the disassembly; the spec rule is recalled, not re-read)
- **Affects:**
  - The GPU-driven path on AMD (RADV) and Intel.
  - Any future task shader copied from this one.
  - The `--gpu-driven` device test, which cannot catch it.
- **Verify by:** `spirv-dis` as above; `aegis-test --headless --gpu-driven --screenshot` on an AMD or Intel device, compared with NVIDIA.
- **Fix sketch:** have invocation 0 write the payload header, then call `GroupMemoryBarrierWithGroupSync()`, then call `DispatchMesh(visibleCount, 1, 1, sharedPayload)` unconditionally from every invocation.

### B-BUILD-02: Task-shader meshlet compaction assumes subgroup size == TASK_GROUP_SIZE (32)
- **Where:**
  - `shaders/gpu-driven/task.slang` : `taskMain` (`WaveActiveCountBits` / `WavePrefixCountBits`)
  - `shaders/modules/meshlet_cull.slang` : `TASK_GROUP_SIZE = 32`
  - `src/aegis/rhi/device.cpp` : `queryCapabilities`
- **Observed:**
  - `visibleCount = WaveActiveCountBits(meshletVisible)` and `visibleIndex = WavePrefixCountBits(meshletVisible)` are computed over the wave, not the workgroup.
  - Invocation 0 dispatches `visibleCount` mesh groups.
  - `queryCapabilities` checks only `meshShader && taskShader`. Nothing in `src/aegis/rhi` queries `subgroupSize`, `minSubgroupSize`, or `subgroupSupportedStages` / `subgroupSupportedOperations` for the task stage (Grep for "subgroup" finds nothing).
  - No `requiredSubgroupSize` and no full-subgroups flag is set on the pipeline.
- **Inference:**
  - Failure scenario: a device that runs the 32-wide task group as two subgroups of 16, or four of 8 (Intel allows 8 to 32 and varies it for SPIR-V 1.6 modules).
  - `visibleIndex` restarts at 0 in each subgroup, so the subgroups overwrite each other's `meshletIDs` slots.
  - `visibleCount` seen by invocation 0 covers only its own subgroup.
  - Result: visible meshlets are silently dropped, or the wrong meshlet IDs are drawn.
  - A device without ballot support in the task stage would also violate the SPIR-V environment rules (`GroupNonUniformBallot` is declared in `task.slang.spv`).
- **Severity:** bug
- **Confidence:** medium (correct on NVIDIA's fixed 32 and on AMD, where 32 threads fit in one wave)
- **Affects:**
  - GPU-driven rendering on Intel or other non-32-wide hardware.
  - Any new wave-intrinsic shader.
  - The capability check is the obvious place to gate such features.
- **Verify by:** code reading; `aegis-test --headless --gpu-driven` on an Intel Arc GPU.
- **Fix sketch:** compact through `groupshared` atomics (`InterlockedAdd` on a shared counter, plus the barrier from B-BUILD-01) instead of wave ops. Alternatively, require full subgroups of size 32 and check `subgroupSupportedStages` in `queryCapabilities`.

### B-BUILD-03: `BoundingSphere::transform` takes scale from matrix rows, not columns, so culling under-sizes rotated, non-uniformly scaled instances
- **Where:**
  - `shaders/modules/common.slang` : `common::BoundingSphere::transform`
  - Used by `gpu-driven/culling.slang` (instance culling) and `gpu-driven/task.slang` (meshlet culling).
- **Observed:**
  - The shader computes `maxScale = max(length(mat[0].xyz), max(length(mat[1].xyz), length(mat[2].xyz)))`. In Slang, `mat[i]` is row *i*.
  - `mat` is `Instance::modelMatrix`, built from the top three rows of the model matrix (`float4x4(model, float4(0,0,0,1))`).
  - The model is `T*R*S` (`math::tranformationMatrix` scales column 0 by `scale.x`, `math.cppm:134`), so the per-axis scale is the length of the **columns**.
  - The C++ side gets this right: `decomposeTRS` uses `glm::length(glm::vec3(matrix[0]))`, a column (`math.cppm:213`).
- **Inference:**
  - Row *i* of `R*S` has length `sqrt(Σ_j R_ij² s_j²)`, which is ≤ max *s* and smaller than it whenever rotation mixes the axes.
  - Example: scale (1, 1, 4) rotated 45° about Y. Row lengths are 2.92, 1 and 2.92, so `maxScale` is 2.92 instead of 4 and the radius is 27% too small.
  - Wrong outcome: instances and meshlets that still overlap the frustum are culled near the screen edges and visibly pop in and out.
  - Unrotated or uniformly scaled objects are unaffected, which is why the test scenes look right.
- **Severity:** bug
- **Confidence:** high
- **Affects:** GPU-driven culling, any future CPU or GPU culling that reuses this helper, and LOD selection.
- **Verify by:** code reading (the math above); a scene with a rotated, non-uniformly scaled mesh at the frustum edge rendered `--gpu-driven` vs. `--scene`.
- **Fix sketch:** use column lengths, `length(float3(mat[0][j], mat[1][j], mat[2][j]))` for j = 0..2, or compute `maxScale` on the CPU and store it in `Instance`. The latter would also make it unit-testable next to `math::decomposeTRS`.

### B-BUILD-04: Lighting's `ambientOcclusionFactor` is uploaded but never read, so the "AO Factor" slider does nothing
- **Where:** `shaders/lighting.slang` : `Lighting::ambientOcclusionFactor`, `main`; `src/aegis/render_graph/passes/lighting_pass.cpp` : `LightingPass::execute`, `drawUI`
- **Observed:**
  - C++ fills `.ambientOcclusionFactor = m_ambientOcclusionFactor`, and `drawUI` exposes it as `ImGui::DragFloat("AO Factor", ...)`.
  - `lighting.slang` declares the field in `Lighting` but `main` never references it: AO is `arm.x`, applied as `(kD * diffuse + specular) * ao`.
- **Inference:** the user drags "AO Factor" from 1 to 0 and the image does not change. The field also silently consumes a slot in a hand-mirrored layout.
- **Severity:** bug (minor)
- **Confidence:** high
- **Affects:** Editor UI and lighting tuning.
- **Verify by:** code reading; Grep for `ambientOcclusionFactor` in `shaders/` finds only the declaration.
- **Fix sketch:** apply it, e.g. `ao = lerp(1.0, arm.x, lighting->ambientOcclusionFactor)`, or delete the field from both twins and the UI.

### B-BUILD-05: Shader and asset directories are wrong when Aegis is built as a CMake subproject
- **Where:**
  - `src/aegis/render_graph/CMakeLists.txt` : `AEGIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"`
  - `shaders/CMakeLists.txt` : output `${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}.spv`
  - Root `CMakeLists.txt` : `set(PROJECT_DIR "${CMAKE_SOURCE_DIR}")` and `PROJECT_DIR="${CMAKE_SOURCE_DIR}"` on `aegis-engine`
  - `src/aegis/core/CMakeLists.txt` : `PROJECT_DIR="${PROJECT_DIR}"`
- **Observed:**
  - The shader writer uses `CMAKE_CURRENT_BINARY_DIR` (Aegis's own `<bin>/shaders`), while the reader uses `CMAKE_BINARY_DIR` (the top-level build root).
  - `PROJECT_DIR` uses `CMAKE_SOURCE_DIR` (the top-level source root).
  - `aegis-test` alone uses `PROJECT_SOURCE_DIR` (`src/aegis/test/CMakeLists.txt:33`).
  - The project prepares for subproject use: `AEGIS_BUILD_TESTS` defaults to `PROJECT_IS_TOP_LEVEL`, and there is the `Aegis::Engine` alias.
- **Inference:**
  - Failure scenario: a game repo does `add_subdirectory(aegis-engine)` or uses FetchContent.
  - The `.spv` files land in `<game-build>/aegis-engine/shaders/`, but `shaderDirectory()` looks in `<game-build>/shaders/`. Every `loadShader` fails, so no pipeline is created.
  - `core::ASSETS_DIR` resolves to `<game-src>/modules/aegis-assets/`, which does not exist.
  - The build succeeds; the failure appears only at run time.
- **Severity:** bug (latent until the first external consumer)
- **Confidence:** high (mechanism); medium (whether subproject use is planned)
- **Affects:** Any external game or app consuming the engine, packaging, and the three independent path mechanisms the map lists (M5).
- **Verify by:** a configure sequence with a two-line wrapper project that calls `add_subdirectory`; compare `AEGIS_SHADER_DIR` in `compile_commands.json` with the custom-command outputs in `build.ninja`.
- **Fix sketch:** derive every path from one place: `PROJECT_SOURCE_DIR` / `PROJECT_BINARY_DIR`, or a single `AEGIS_SHADER_OUTPUT_DIR` variable used by both the writer and the reader.

### B-BUILD-06: `indirect_draw` declares a module-scope push-constant variable that leaks into every importer
- **Where:** `shaders/modules/indirect_draw.slang` : `indirectDraw::pc`, `getInstance`; `shaders/gpu-driven/culling.slang` : `PushConstants`, local `getInstance`
- **Observed:**
  - `indirect_draw.slang` has `public [[vk::push_constant]] ConstantBuffer<PushConstants> pc;` at module scope.
  - `culling.slang` imports the module and declares its own, different `[[vk::push_constant]] pc` (64 B vs. 48 B).
  - The reflection JSON (`build/audit/reflect/gpu-driven_culling.slang.json`) lists both blocks at offset 0.
  - Only one reaches the SPIR-V, because the unused one is dead-stripped.
  - `culling.slang` re-implements `getInstance` line for line, because `indirectDraw::getInstance` is bound to the module's block.
- **Inference:**
  - Today this is harmless. It is a trap for the obvious change, though: calling `indirectDraw::getInstance` from culling, or from any new compute pass over instances.
  - That call would read the module's 48-byte layout from the 64-byte block culling actually pushes: `visibility` would read `drawBatches`, and the counts would be wrong.
  - Or the entry point would statically use two push-constant variables, which Vulkan forbids. Only the validation layers at pipeline creation would catch that, since there is no build-time `spirv-val`.
  - Every new instance-reading shader must duplicate `getInstance`.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** Adding GPU-driven passes (occlusion culling, shadow culling); shader-module reuse in general.
- **Verify by:** the reflection diff above; `spirv-dis` shows a single `OpVariable %_ptr_PushConstant` in `culling.slang.spv`.
- **Fix sketch:** keep push-constant declarations in entry-point files only. Make `getInstance(InstanceSource src, uint id)` take the two instance pointers and `staticCount` as parameters, and delete culling's copy.

### B-BUILD-07: `COMPILE_SHADERS=OFF` produces a build that configures and links but cannot render
- **Where:** Root `CMakeLists.txt` : `option(COMPILE_SHADERS ...)`, `if(COMPILE_SHADERS) add_subdirectory(shaders)`; `render_graph/shader.cpp` : `shaderDirectory`
- **Observed:**
  - With the option OFF, nothing produces `<bin>/shaders/*.spv`, and nothing adds `compile_shaders` as a dependency.
  - `AEGIS_SHADER_DIR` still points at `<bin>/shaders`. There is no option for a prebuilt shader directory and no configure-time message.
- **Inference:**
  - In a fresh build dir, every pipeline creation fails at run time with "Failed to open shader".
  - The `--scene` and `--gpu-driven` device tests fail; examples assert through `Engine::check`.
  - In an old build dir, stale `.spv` files from an earlier ON configure load silently even after `.slang` edits. This is a new route to the known stale-binary gotcha.
- **Severity:** polish
- **Confidence:** high
- **Affects:** Build configuration and CI variants; anyone trying to speed up a build.
- **Verify by:** configure sequence `-DCOMPILE_SHADERS=OFF --fresh`, then build and run `aegis-test --headless --scene`.
- **Fix sketch:** remove the option, or make OFF require an `AEGIS_PREBUILT_SHADER_DIR` that feeds `AEGIS_SHADER_DIR`.

### B-BUILD-08: Mesh-shader output limits are hard-coded while the meshlet builder's limits are free inputs
- **Where:**
  - `shaders/gpu-driven/geometry.slang` : `MAX_VERTICES = 64`, `MAX_PRIMITIVES = 126`
  - `src/aegis/assets/mesh_preprocessor.cppm` : `Input::maxVerticesPerMeshlet{ 64 }`, `maxTrianglesPerMeshlet{ 126 }`
  - `assets/mesh_data.cppm` : `Meshlet::vertexCount` / `primitiveCount` (`uint8_t`)
- **Observed:**
  - The defaults agree today.
  - `MeshPreprocessor` passes the `Input` values straight to `meshopt_buildMeshlets` without checking them against the shader's limits. No C++ constant mirrors 64 or 126.
- **Inference:**
  - Failure scenario: a caller tunes meshlets with `maxVerticesPerMeshlet = 128`.
  - `SetMeshOutputCounts(128, ...)` exceeds the declared `out vertices MSOut meshVertices[64]`, and `meshVertices[i]` writes out of bounds: undefined behaviour or a device-lost error.
  - Above 255 the count would also truncate in the `uint8_t` fields.
- **Severity:** design problem
- **Confidence:** medium
- **Affects:** Asset import tuning; MeshPreprocessor unit tests (which can't currently assert the shader contract).
- **Verify by:** code reading.
- **Fix sketch:** define `assets::maxMeshletVertices` / `maxMeshletTriangles` constants, have `MeshPreprocessor` clamp or reject larger inputs (unit-testable), and put the shader constant's comment next to the C++ constant.

### B-BUILD-09: Non-ASCII checkout paths break run-time shader and asset loading on Windows
- **Where:**
  - `render_graph/shader.cpp` : `shaderDirectory()` (`std::filesystem::path{ AEGIS_SHADER_DIR }`)
  - `core/globals.cppm` : `ENGINE_DIR`, `ASSETS_DIR`
  - `test/main.cpp` : `std::filesystem::path{ ASSETS_DIR }`
- **Observed:**
  - All baked-in paths are narrow `const char*` literals.
  - Clang emits them as UTF-8, while the MSVC STL converts narrow strings to `path` through the active code page. There is no UTF-8 `activeCodePage` manifest (Grep).
- **Inference:** a repo cloned under, for example, `C:\Users\Jürgen\src\aegis-engine` gets mangled paths (UTF-8 bytes read as CP1252), so every shader and asset load fails at run time. Linux is unaffected. The Ninja and CMake toolchain may already struggle with such paths, which lowers the practical impact.
- **Severity:** bug
- **Confidence:** low
- **Affects:** Windows users with non-ASCII profile or checkout paths.
- **Verify by:** a configure, build and `aegis-test --headless --scene` sequence from a copy of the tree in a directory with "ü".
- **Fix sketch:** bake the paths as `u8"..."` literals into `std::filesystem::path`, or add a UTF-8 application manifest to the executables.

### B-BUILD-10: The test shader compiles with a different flag set from every other shader
- **Where:** `src/aegis/test/CMakeLists.txt` : the `slangc` custom command; `shaders/CMakeLists.txt`; `shaders/modules/README.md`
- **Observed:**
  - The test shader gets `-target spirv -fvk-use-entrypoint-name` only: no `-profile`, `-fvk-use-scalar-layout`, `-matrix-layout-row-major`, `-I shaders`, or `-O`/`-g`.
  - The README names its `computeMain` as a working example of the convention.
- **Inference:**
  - Nothing breaks today, because `RoundTripConstants` has only pointers and a `uint`.
  - If someone extends the device harness with a `float3`, a matrix, or `import modules.bindless`, the harness pushes std430 layout or column-major matrices, or fails to compile. It would then test different rules from the ones the engine uses.
  - That weakens the harness's job of proving the shader convention. (The doc mismatch itself is in the map as M7; this entry records the bug risk.)
- **Severity:** polish
- **Confidence:** high
- **Affects:** Extending `aegis-test`'s shader checks.
- **Verify by:** code reading; `spirv-dis` of `shader.spv` shows no scalar-layout dependence.
- **Fix sketch:** factor an `aegis_compile_slang(<src> <out>)` CMake function in `shaders/CMakeLists.txt` and use it from both places.

### B-BUILD-11: CPU/GPU agreement has no automated check; one more unlinked duplicate found
- **Where:**
  - `shaders/lighting.slang` : `MAX_REFLECTION_LOD = 4.0` vs. `render_graph/ibl.cppm` : `prefilteredMipLevels{ 5 }`
  - Plus the duplicates already listed in map/build.md §2: group sizes, `MAX_POINT_LIGHTS`, enum values, `TASK_GROUP_SIZE`, `pbrTemplate` order.
  - CI (`.github/workflows/build.yml`).
- **Observed:**
  - All about 20 struct twins and both enums match today: I checked them by reflection offsets and `ArrayStride`, which I believe no one had done before.
  - `MAX_REFLECTION_LOD` must equal `prefilteredMipLevels - 1` and has no comment linking the two.
  - No build step or CI job runs `spirv-val`, even though agent-notes say slangc compiles some shaders into SPIR-V that validation rejects.
- **Inference:**
  - Changing `prefilteredMipLevels` to 6 makes rough reflections sample mip 4 instead of 5 (too sharp at roughness 1), with no error anywhere.
  - Any reorder in a twin struct likewise shows up only as wrong pixels.
  - A slangc upgrade that emits invalid SPIR-V is caught only on a GPU machine, never in CI.
- **Severity:** design problem
- **Confidence:** high
- **Affects:**
  - Every new pass or shader, which adds another hand-mirrored struct.
  - Test goal (3): layouts are pure data and could be checked without a GPU.
- **Verify by:** code reading; the reflection JSONs in `build/audit/reflect/`.
- **Fix sketch:**
  - Build-time: emit `-reflection-json` next to each `.spv` and run `spirv-val --scalar-block-layout` as part of `compile_shaders`.
  - Add one doctest (`tests/render_graph/shader_layout.test.cpp`) that reads the JSON and compares offsets and sizes against `offsetof`/`sizeof` of the C++ twins (move the local `PushConstants` structs to namespace scope so the test can name them), plus constants such as group sizes. This replaces hand-checking with a CI-run, GPU-free test.

### B-BUILD-12: IBL bakes sample at texel corners, not centers
- **Where:**
  - `shaders/ibl/brdf_lut.slang` : `main` (`uv = float2(dispatchID.xy) / float2(imageSize)`)
  - `equirect_to_cube.slang`, `irradiance_convolution.slang`, `prefilter_environment.slang` : `st = float2(dispatchID.xy) / float2(imageSize.xy)`
- **Observed:**
  - Texel *i* stores the value for coordinate *i/N*.
  - `lighting.slang` samples the LUT with `sampleLevel(float2(NdotV, roughness), 0)`, where texel *i* is centered at *(i+0.5)/N*. Cube faces are sampled by direction.
- **Inference:**
  - Every baked value is shifted by half a texel. The BRDF LUT is biased toward lower NdotV and roughness; at 512² this is small, but it is largest at grazing angles.
  - Cube-face directions are asymmetric: u = -1 is covered but u = +1 is not, which gives faint seams at face edges, most visible in the 32² irradiance map.
- **Severity:** polish
- **Confidence:** medium
- **Affects:** IBL quality; it is not a crash or layout issue.
- **Verify by:** code reading; `aegis-test --screenshot` before and after the fix.
- **Fix sketch:** use `(float2(dispatchID.xy) + 0.5) / float2(size)` in all four shaders.

## Rejected

- **spirv-val failures:** none beyond the expected scalar-layout dependency (Debug and `-O3` builds, vulkan1.3 and 1.4).
- **Push-constant and pointer-layout mismatches:** none. Reflection offsets and `ArrayStride` values equal the C++ twins, including `Meshlet` tail padding (32) and `Instance` (112).
- **Enum mismatches** (`LightingViewMode`, `ToneMapping`): values agree.
- **Compute dispatch off-by-one:** every C++ dispatch uses ceil-division `groupCount`/`groups`, and every compute shader bounds-checks: images via `GetDimensions`, culling against `staticCount + dynamicCount`, the test shader against `count`. IBL dispatches z = 6, matching faces 0..5.
- **Culling grid vs. task group:** `groupCountX = ceil(meshletCount/32)` matches `TASK_GROUP_SIZE`. The task stage bounds-checks `dispatchThreadID.x < meshletCount`.
- **Meshlet limits:** 64/126 agree today (the latent risk is B-BUILD-08).
- **Stale `.spv` for `aegis-test` or unit tests after a shader edit:** `build.ninja` gives `aegis-render-graph` and `aegis-test` an order-only dependency on `compile_shaders` and `test-shaders`, and Ninja rebuilds dirty order-only inputs.
- **Module edits not recompiling shaders:** every shader DEPENDS on all `modules/*.slang`. This is coarse but correct.
- **SPIR-V capabilities without enabled features:** all covered except subgroup stages (B-BUILD-02).
- **Asset path case on Linux:** all code paths and internal URIs match. The `mtllib untitled.mtl` in `Misc/cube.obj` and `Misc/colored_cube.obj` is missing on every platform, not a case issue, and the loader tolerates it (`aegis-test` loads `cube.obj`).
- **Paths with spaces:** VERBATIM custom commands and CMake-escaped compile definitions handle them.
- **Build types other than Debug or Release** get no `-O` or `-g`: slangc defaults apply, which is harmless.
- **Wrong-SDK configure:** it already fails fast (known; AGENTS.md).
- **`PROJECT_DIR`/`BUILD_DIR` defined on both `aegis-core` and `aegis-engine`:** identical values, no redefinition diagnostic.
- **Device tests and the assets submodule on CI:** CI skips `device` and checks out submodules recursively. The registered tests use absolute exe paths and baked asset paths, so the working dir is irrelevant.
- **Presets:** consistent between Linux and Windows. Release gets `NDEBUG` from CMake defaults; `AGX_ASSERT` compiling out is a known note.
- **Two push-constant blocks in culling at run time:** only one reaches the SPIR-V (the risk is B-BUILD-06).
- **Out of group, passed on:** `SceneUpdatePass` and `SkyBoxPass` use `camera.projectionMatrix` computed from the previous frame's aspect, and `CameraData` is left unwritten when there is no main camera (render lens).
