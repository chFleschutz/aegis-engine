# Verification: shaders and build

Scope: `findings-a-build.md`, `findings-b-build.md` and `findings-e-build.md`. I read every cited symbol, ran the SDK 1.4.328.1 tools (spirv-dis, spirv-val, slangc 2025.17.2), ran the existing unit-test exe, and compiled two standalone probes plus one scratch CMake probe under `build/audit/verify/build/`. I did not build or configure the project and did not run a GPU test.

## 1. Summary counts

Only bug and design-problem findings are counted. Polish findings are listed under "kept".

| File | verified | unverified | refuted | reclassified | polish kept |
|---|---|---|---|---|---|
| findings-a-build.md | 7 (01, 02, 04, 05, 06, 07, 10) | 1 (03) | 0 | 1 (08: design problem → polish; its failure inference is refuted) | 09, 11 |
| findings-b-build.md | 8 (01, 03, 04, 05, 06, 08, 09, 11) | 1 (02) | 0 | 0 | 07, 10, 12 |
| findings-e-build.md | 5 (01, 02, 03, 05, 07; one sub-claim of 07 is refuted) | 0 | 0 | 0 | 04, 06 |

## 2. Table

| ID | Short title | Severity (filed → verified) | Verdict | Evidence | Canonical |
|---|---|---|---|---|---|
| B-BUILD-01 | Task shader emits mesh tasks from one invocation | bug → bug | VERIFIED | `task.spvasm:1438-1455`: `OpIEqual …%uint_0` → `OpBranchConditional` → `OpEmitMeshTasksEXT` (same in the `-O3` build). validusage.json **VUID-RuntimeSpirv-TaskEXT-07117**: "OpEmitMeshTasksEXT must be called exactly once under dynamically uniform conditions". spirv-val passes (exit 0), so no tool catches it. | B-BUILD-01 |
| B-BUILD-02 | Wave compaction assumes subgroup ≥ 32 | bug → bug | UNVERIFIED | `task.spvasm:1411-1418`: `OpGroupNonUniformBallotBitCount Reduce` / `ExclusiveScan`. Grep finds no "subgroup" in `src/aegis`; `device.cpp:581` checks only `meshShader && taskShader`. **VUID-RuntimeSpirv-None-06343** requires the stage to be in `subgroupSupportedStages`. No device with fewer than 32 lanes was available. | B-BUILD-02 |
| A-BUILD-03 | Same defect, architecture lens | bug → bug | UNVERIFIED | Same evidence as B-BUILD-02. | → B-BUILD-02 |
| B-BUILD-03 | `BoundingSphere::transform` takes scale from rows | bug → bug | VERIFIED (reproduced numerically) | Slang probe: with `-matrix-layout-row-major`, `mat[0]` = floats 0..3 of the stored `float3x4`. The upload path is `glm::mat3x4{rowMajor4(model)}` (`gpu_driven.cpp:54`). glm probe, T·R_y(45°)·S(1,1,4): shader rows 2.9155 / 1 / 2.9155, columns 1 / 1 / 4, radius ratio **0.729**. | B-BUILD-03 |
| B-BUILD-04 | `ambientOcclusionFactor` is never read | bug → bug (minor) | VERIFIED | The only shader hit is the declaration at `lighting.slang:33`. `ao = arm.x` (`:75`) is applied at `:102`. C++ writes the field at `lighting_pass.cpp:96`; the UI slider is at `:148`. | B-BUILD-04 |
| B-BUILD-05 | Shader and asset dirs wrong in a subproject | bug (latent) → bug (latent) | VERIFIED (by CMake semantics, not configured) | Writer: `${CMAKE_CURRENT_BINARY_DIR}/${REL_PATH}.spv` (`shaders/CMakeLists.txt`). Reader: `AEGIS_SHADER_DIR="${CMAKE_BINARY_DIR}/shaders"` (`render_graph/CMakeLists.txt:17`). `PROJECT_DIR` comes from `CMAKE_SOURCE_DIR` (root `CMakeLists.txt:26,34`). | B-BUILD-05 (related to A-BUILD-05) |
| B-BUILD-06 | Module-scope push constant in `indirect_draw` | design problem → design problem | VERIFIED (reproduced) | A probe swapped culling to `indirectDraw::getInstance`. slangc exits 0 and emits two `OpVariable … PushConstant`; spirv-val then fails with **VUID-StandaloneSpirv-OpEntryPoint-06674** ("uses more than one PushConstant interface"). The shipped culling module has one PushConstant variable (`:1066`). | B-BUILD-06 |
| B-BUILD-08 | Meshlet limits are free inputs; shader limits are hard-coded | design problem → design problem | VERIFIED | `geometry.slang:11-12,30-31,38`; `mesh_preprocessor.cppm:27-28`; `mesh_preprocessor.cpp:99-107` passes the limits unchecked. meshoptimizer 0.25 asserts only `max_vertices ≤ 256` and `max_triangles ≤ 512` (`clusterizer.cpp:1131-1132`). | → A-BUILD-02 |
| B-BUILD-09 | Non-ASCII checkout path breaks run-time loads on Windows | bug → bug | VERIFIED (run-time step reproduced) | Probe with an existing `dir_ü/x.txt` and ACP 1252: a narrow UTF-8 literal gives `exists=0 open=0`; a `char8_t` path gives `exists=1`. A full configure and build in such a directory was not run. | B-BUILD-09 (related to A-BUILD-05) |
| B-BUILD-11 | CPU/GPU agreement has no automated check; `MAX_REFLECTION_LOD` | design problem → design problem | VERIFIED | `lighting.slang:54` `MAX_REFLECTION_LOD = 4.0` vs. `ibl.cppm:22` `prefilteredMipLevels{ 5 }`, with no link between them. No spirv-val step in the CMake files or `build.yml`. | → A-BUILD-01 |
| A-BUILD-01 | CPU/GPU data interface has no home or guard | design problem → design problem | VERIFIED | Three `struct TextureDescriptor` (`ibl.cpp:26`, `lighting_pass.cpp:22`, `post_effects.cpp:106`). `descriptorOf` ×2, `storageIndex` ×2. The only checks are `sizeof <= 128` (`command_buffer.cppm:76`, `lighting_pass.cpp:43`, `static_mesh_render_system.cppm:33`). Grep finds no `offsetof`. | A-BUILD-01 |
| E-BUILD-02 | Struct layouts matched by hand | design problem → design problem | VERIFIED | Same evidence as A-BUILD-01. | → A-BUILD-01 |
| A-BUILD-02 | Cross-boundary constants duplicated as literals | design problem → design problem | VERIFIED | `numthreads(16,16,1)` in 9 shaders vs. `groupCount(…,16)` at `ibl.cpp:22`, `lighting_pass.cpp:143`, `post_effects.cpp:117`, `screen_passes.cpp:151`. `culling.slang:45` (64) vs. `gpu_driven.cpp:228`. `MAX_POINT_LIGHTS` 128 vs. `lighting_pass.cppm:32`. | A-BUILD-02 |
| A-BUILD-04 | Shaders identified by path strings; passes own pipelines | design problem → design problem | VERIFIED | 16 `"…slang.spv"` literals across 6 files plus one default argument. 10 `std::optional<rhi::Pipeline>` members in `render_graph`. | A-BUILD-04 |
| A-BUILD-05 | Three path mechanisms, leaked as PUBLIC macros | design problem → design problem | VERIFIED | `core/CMakeLists.txt` PUBLIC `PROJECT_DIR`/`BUILD_DIR`. `core::SHADER_DIR` has no reader (only the definition at `globals.cppm:22`). `aegis-test` PUBLIC `SHADER_PATH`/`ASSETS_DIR`. The gltf test uses `add_definitions(-DPROJECT_DIR=…)`. | A-BUILD-05 |
| A-BUILD-06 | Shader compilation is a copied recipe with no validation | design problem → design problem | VERIFIED | The test recipe uses only `-target spirv -fvk-use-entrypoint-name` (`src/aegis/test/CMakeLists.txt`). No spirv-val anywhere. With `COMPILE_SHADERS=OFF` even the triangle device test fails: `verifyIbl()` runs in every mode (`test/main.cpp:248`) and loads `ibl/*.spv`. | A-BUILD-06 |
| E-BUILD-05 | Two compile rules with different flags | design problem (small) → design problem (small) | VERIFIED | Same evidence as A-BUILD-06. | → A-BUILD-06 |
| A-BUILD-07 | `aegis-render-graph` is one target for framework and passes | design problem → design problem | VERIFIED (count corrected) | `render_graph.cppm` has 21 `export import` lines. `render_pass.cppm:13` `import aegis.scene` and `:21` `Scene&`. In `build.ninja:5678`, `cmake_object_order_depends_target_aegis-render-graph` includes `shaders/compile_shaders`. | A-BUILD-07 |
| E-BUILD-07 | Unit-test exes link whole subsystems | design problem → design problem | VERIFIED; the "tests/scene may not see `aegis.math`" sub-claim is REFUTED | Same evidence as A-BUILD-07, plus the link probe (§3). | → A-BUILD-07 |
| A-BUILD-08 | Link visibility does not follow imports | design problem → **polish** | RECLASSIFIED (facts hold; failure inference refuted) | `scene/CMakeLists.txt` PRIVATE core/math/EnTT, while `components.cppm:7` and `registry.cppm:11` have `export import aegis.math`. Probe: an exe linking only a STATIC lib that re-exports a PRIVATE-linked module builds and runs (exit 0). | A-BUILD-08 |
| A-BUILD-10 | CI proves compilation only | design problem → design problem | VERIFIED | `build.yml` runs `ctest --preset … -LE device` in both jobs and has no spirv-val step. `gpu_driven.cpp:265` returns an error without mesh shaders; it does not skip. | A-BUILD-10 |
| E-BUILD-01 | `;` in a TEST_CASE name splits it, and both halves run zero cases | bug → bug | VERIFIED (reproduced) | `aegis-tests-render_graph_tests-b858cb2.cmake:35,37` register the two halves. Running the exe with either filter prints "test cases: 0 \| 0 passed \| … 21 skipped" and exits **0**. With the full name: 1 case, 3 assertions. `LastTest.log:830-832`: ctest #38 " a layout change does" "passed". 108 `TEST_CASE`s in `.cpp` files → 109 entries. | E-BUILD-01 |
| E-BUILD-03 | `pbrTemplate` built inside the device-owning factory | design problem → design problem | VERIFIED | `defaults.cpp:15` `DefaultAssets::create(rhi::Device&, …)` holds the `addParameter` calls at `:43-52`. | E-BUILD-03 (related to E-CONTENT-02) |
| Polish, a-build | A-BUILD-09, A-BUILD-11 | polish | KEPT | A-09: `BUILD_TEST:BOOL=ON` (`CMakeCache.txt:42`), `aegis-gltf-testing.exe` is built, no `Aegis::GLTF` consumer. A-11: identical 5-line `project()` example files. | A-BUILD-09 ← D-15 |
| Polish, b-build | B-BUILD-07, B-BUILD-10, B-BUILD-12 | polish | KEPT | B-12: `(float2(dispatchID.xy)) / float2(size)` in all four IBL shaders. | B-07, B-10 → A-BUILD-06 |
| Polish, e-build | E-BUILD-04, E-BUILD-06 | polish | KEPT | — | E-04 → A-BUILD-02 (its `TextureDescriptor` part → A-BUILD-01); E-06 → A-BUILD-04 |

## 3. Refuted findings

No bug or design-problem finding is refuted outright. One inference is refuted, and it is the reason A-BUILD-08 is reclassified.

**The A-BUILD-08 inference, and the same sub-claim in E-BUILD-07 ("a `tests/scene` linking only `aegis-scene` may not see `aegis.math`").**
- I built a scratch CMake 4.3.2 + Ninja + clang project:
  - `pmath`: a STATIC library exporting module `pmath`;
  - `pscene`: a STATIC library whose module does `export import pmath`, linking `pmath` PRIVATE;
  - `ptest`: an exe linking only `pscene`, calling `twice()` from `pmath`.
- It configures, builds (12/12 steps) and runs with exit 0.
- Why: for static libraries, PRIVATE dependencies stay in the consumer's link closure (`$<LINK_ONLY:>`), and CMake collates the module map from that closure. E-APP-13 reasons the same way.
- What remains true:
  - The declarations don't match the import graph. That is a readability and hygiene issue (polish).
  - It would become real only if the libraries became SHARED.
- So the "trap" in extensibility trace B (expect to add `aegis-math` to LIBS) does not occur.

## 4. Merged duplicates

Each line gives the canonical ID, then the IDs merged into it.

- **B-BUILD-01** (bug): no duplicates.
- **B-BUILD-02** (bug) ← A-BUILD-03.
- **A-BUILD-01** (design) ← E-BUILD-02, B-BUILD-11, the `TextureDescriptor` part of E-BUILD-04, and D-03 (`findings-d.md`). A-FOUND-11 (polish, the repeated `rowMajor4`/normal-row conversion) is related: its helper belongs next to the `:gpu_types` partition A-BUILD-01 proposes.
- **A-BUILD-02** (design) ← B-BUILD-08, B-CONTENT-13 (`findings-b-content.md`, meshlet limits), and E-BUILD-04 (group sizes, enums, `MAX_POINT_LIGHTS`). B-BUILD-11's `MAX_REFLECTION_LOD`/`prefilteredMipLevels` pair is one more instance; the finding itself is filed under A-BUILD-01.
- **A-BUILD-06** (design) ← E-BUILD-05, B-BUILD-10, B-BUILD-07, and the test-shader flag part of C-11 (`findings-c.md`). The spirv-val part of A-BUILD-10 and B-BUILD-11 is fixed here too.
- **A-BUILD-07** (design) ← E-BUILD-07.
- **A-BUILD-08** (now polish) ← E-APP-13 (`findings-e-app.md`) and the imguizmo-PUBLIC part of D-20 (`findings-d.md`).
- **A-BUILD-05** (design) ← D-16 (`findings-d.md`, the unused `core::SHADER_DIR`). B-BUILD-05 and B-BUILD-09 are related bugs that the same fix would remove; they are not merged because they are distinct defects.
- **A-BUILD-04** (design) ← E-BUILD-06. A-RENDER-08 (pipeline pooling) is related, not the same defect.
- **A-BUILD-10** (design) ← the CI part of E-RENDER-13 (`findings-e-render.md`).
- **A-BUILD-09** (polish) ← D-15 (`findings-d.md`).
- **E-BUILD-03** (design): E-CONTENT-02 is related (name literals vs. the device-bound factory). Both block the same PBR-layout test.

## 5. Corrections to facts

1. **B-BUILD-01: the spec rule and the barrier.**
   - The rule exists in the SDK text as VUID-RuntimeSpirv-TaskEXT-07117 ("exactly once under dynamically uniform conditions"). Confidence goes from medium to high for the rule.
   - Slang's `DispatchMesh` lowers straight to `OpEmitMeshTasksEXT`; the slang.dll core module has no uniformization or doc caveat.
   - Recalled from SPV_EXT_mesh_shader, not in the SDK: `OpEmitMeshTasksEXT` also acts as a workgroup `OpControlBarrier`. So the missing barrier is not an independent defect: once every invocation calls `DispatchMesh`, the explicit `GroupMemoryBarrierWithGroupSync()` in the fix sketch is redundant but harmless.
   - The payload is in `TaskPayloadWorkgroupEXT` storage. Invocations 1..31 write it and terminate without emitting.
   - The shader's early `return` for `drawID >= batchSize` is unreachable, because `maxDrawCount = batch.instanceCount` (`gpu_driven.cpp:339-340`).
2. **B-BUILD-02:** `task.slang.spv` is SPIR-V 1.6 (header). Vulkan 1.3 lets SPIR-V 1.6 modules run with a varying subgroup size (recalled, not found in the SDK files), which supports the scenario. lavapipe (8-wide on AVX2 hosts) is the likeliest concrete trigger, and A-BUILD-10 proposes exactly such a CI job.
3. **B-BUILD-06:** the reflection JSON lists the two `pc` blocks as `pushConstantBuffer` index 0 and index 1, not "both at offset 0".
4. **B-BUILD-09:** confidence goes from low to medium; the narrow-to-`path` conversion failure is reproduced.
5. **A-BUILD-07:** the target has 22 `.cppm` files (the primary plus 21 partitions) and 15 `.cpp` files, not 25 `.cppm`.
6. **A-BUILD-06, plus the comment in `shaders/CMakeLists.txt` and AGENTS.md ("slangc reports no per-file dependencies"):**
   - slangc 2025.17.2 supports `-depfile`.
   - For `task.slang` it lists exactly the five imported modules.
   - Paths are Make-escaped (`C\:\\…`); whether Ninja accepts that format was not checked.
   - `-reflection-json` also exists. This answers A-BUILD-06's open question.
7. **A-BUILD-05:**
   - `loadShader`'s open-failure path is unit-testable today with a nonexistent relative path. Only its content checks (size 0, size % 4) need a file inside the macro directory.
   - "Collide, as the gltf test already shows": the gltf test does not link `aegis-core`, so there is no actual collision today. It only reuses the name with a different meaning.
8. **A-BUILD-01:** `rhi::Descriptor` is at `bindless_heap.cppm:16`, not `:15` (trivial).

## 6. Run log

| # | Command (cwd: repo root unless noted) | Exit | Key output |
|---|---|---|---|
| 1 | `spirv-dis build/audit/shaders/gpu-driven/task.slang.spv -o …/verify/build/task.spvasm` | 0 | `Version: 1.6`. `OpEmitMeshTasksEXT %1097` at `:1455`, inside `OpBranchConditional %1139` on `OpIEqual … %uint_0`. Ballot `Reduce`/`ExclusiveScan` at `:1411-1418`. No `OpControlBarrier`. |
| 2 | `spirv-dis build/audit/reflect/gpu-driven_task.slang.O3.spv` | 0 | Same conditional emit; 0 `OpControlBarrier`. |
| 3 | `spirv-val --target-env vulkan1.3 --scalar-block-layout …/task.slang.spv` | 0 | Passes: the rule is not checkable statically. |
| 4 | Python scan of `C:/VulkanSDK/1.4.328.1/share/vulkan/registry/validusage.json` | 0 | Found VUID-RuntimeSpirv-TaskEXT-07117 and VUID-RuntimeSpirv-None-06343. |
| 5 | `strings slang.dll \| grep DispatchMesh` | 0 | `DispatchMesh` lowers directly to `OpEmitMeshTasksEXT …; %_ = OpLabel`. |
| 6 | `slangc rowcheck.slang -matrix-layout-row-major -fvk-use-scalar-layout …`, then `spirv-dis` | 0 | `mat[0]` = `OpCompositeExtract` element 0 of the stored `_MatrixStorage_float3x4` (bytes 0..15). |
| 7 | `clang++ -std=c++23 -I external/glm scale_check.cpp && ./scale_check.exe` | 0 | Rows 2.9155 / 1 / 2.9155; columns 1 / 1 / 4; ratio 0.729. |
| 8 | `slangc culling_probe.slang …` (calls `indirectDraw::getInstance`), then `spirv-val` | 0 / **1** | Two `OpVariable … PushConstant`; spirv-val reports VUID-StandaloneSpirv-OpEntryPoint-06674. |
| 9 | `aegis-tests-render_graph.exe "--test-case=reads in the same layout need no barrier"` | 0 | `test cases: 0 \| 0 passed \| 0 failed \| 21 skipped`, "Status: SUCCESS!" |
| 10 | `aegis-tests-render_graph.exe "--test-case= a layout change does"` | 0 | `test cases: 0 … 21 skipped`. |
| 11 | `aegis-tests-render_graph.exe "--test-case=reads in the same layout need no barrier; a layout change does"` | 0 | `test cases: 1 \| 1 passed`, 3 assertions. |
| 12 | Scratch `linkprobe` (`cmake -G Ninja`, then `cmake --build`, then run `ptest.exe`), CMake 4.3.2 | 0 / 0 / 0 | Exe linking only `pscene` uses `pmath` re-exported through a PRIVATE link. |
| 13 | `clang++ nonascii.cpp && ./nonascii.exe` (ACP 1252) | 0 | narrow path `exists=0 open=0`; `char8_t` path `exists=1`. |
| 14 | `slangc task.slang … -depfile task_probe.d` | 0 | depfile lists `task.slang` plus `common`, `bindless`, `indirect_draw`, `meshlet_cull`, `visibility`. |
| 15 | `slangc -h`, `slangc -version` | 0 | `-depfile` and `-reflection-json` present; 2025.17.2. |
| 16 | `grep` in `build/audit/build.ninja:5678` | 0 | `aegis-render-graph` object order includes `shaders/compile_shaders`. |
| 17 | `grep` in `build/audit/Testing/Temporary/LastTest.log` | 0 | #38/112 " a layout change does" run with `--test-case= a layout change does`. |

No GPU run (`aegis-test`) was needed, so the GPU lock was not taken. All scratch files are under `build/audit/verify/build/` (`task.spvasm`, `rowcheck.*`, `scale_check.*`, `culling_probe.*`, `nonascii.*`, `dir_ü/`, `task_probe.*`, `linkprobe/`).
