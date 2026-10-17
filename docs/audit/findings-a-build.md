# Findings A: shaders and build (architecture lens)

Phase 2 of the foundation audit, group "shaders and build", lens a (architecture). Read-only: nothing was built or run. Every fact was rechecked against the code; facts in `docs/agent-notes.md` are used as evidence and not reported as new. "Observed" is what the code says. "Inference" is my reading of it.

Judged against three questions: does it make adding features cheaper or safer, does it make the code easier to understand and change, does it make logic unit-testable without a GPU.

## 1. Findings

### A-BUILD-01: The CPU/GPU data interface has no single home and no guard
- **Where:**
  - Push-constant structs local to functions:
    - `render_graph/passes/gpu_driven.cpp`: `CullingPass::execute`, `GpuDrivenGeometryPass::execute`.
    - `post_effects.cpp`: `BloomPass::execute` (`ThresholdConstants`, `DownsampleConstants`, `UpsampleConstants`) and `PointLightRenderSystem::render`.
    - `screen_passes.cpp`: `SkyBoxPass::execute`, `PostProcessingPass::execute`.
    - `ibl.cpp`: three `PushConstants`, plus a bare `uint32_t` in `Ibl::brdfLut`.
    - `lighting_pass.cpp`: an anonymous-namespace `PushConstants`.
  - Exported mirrors:
    - `CameraData` (`scene_update_pass.cppm`).
    - `InstanceData`, `DrawBatchData` (`gpu_driven.cppm`).
    - `StaticMeshRenderSystem::PushConstants`, `LightingPass::Uniforms`.
    - `assets::Vertex`, `BoundingSphere`, `Meshlet` (`mesh_data.cppm`) and `StaticMesh::GpuData`.
- **Observed:**
  - About 20 structs copy Slang structs by hand. The only checks are `sizeof(T) <= 128`: in `CommandBuffer::pushConstants`, `lighting_pass.cpp:43` and `static_mesh_render_system.cppm:33`. There are no `offsetof`/`sizeof` equality checks, no reflection and no test.
  - `bindless::TextureDescriptor` has 5 C++ spellings:
    - local structs in `ibl.cpp:26`, `lighting_pass.cpp:22` and `post_effects.cpp:106`;
    - two loose `uint32_t` in `screen_passes.cpp:73` (`skyboxImage`, `skyboxSampler`);
    - two indices written by `assets/material.cpp:61-62`.
  - Helpers are duplicated too: `descriptorOf` in `ibl.cpp:32` and `lighting_pass.cpp:28`, `storageIndex` in `lighting_pass.cpp:45` and `screen_passes.cpp:20`.
  - Both ends are typed: the RHI has `rhi::Descriptor<T>` (`bindless_heap.cppm:15`) and Slang has `bindless::StorageImageDescriptor`. In between, every C++ copy stores a raw `uint32_t` taken from `.index`. For example, the lighting push constants declare `std::uint32_t sceneColor; std::array<std::uint32_t, 5> gBuffer;`.
  - `common::PbrMaterial` has no C++ struct. It matches only by the call order of `addParameter` in `assets/defaults.cpp:43-52`. `material.test.cpp` tests the packing rule, not this template against the Slang struct.
- **Inference:**
  - A struct local to a function cannot be named from a test TU, so a layout test is impossible until the structs move.
  - These changes would all render wrong data with no validation error: a reordered member, a `vec3` that becomes a `vec4`, or swapped storage and sampled indices.
  - `aegis-test` checks validation, not pixels (agent-notes: "Look at the screenshot after any render change").
  - Nothing is broken today. The cost and risk grow with every material or pass.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every new pass or shader, material parameter blocks, the GPU-driven `Instance` layout, and unit tests (there is nothing to test today).
- **Proposal:**
  1. (S) Add one shared descriptor type and use typed indices.
     - Add `rhi::TextureDescriptor{SampledImageDescriptor image; SamplerDescriptor sampler;}` in `:bindless_heap`, and `assets::Texture::descriptor()` returning it. This replaces the 3 local structs, both `descriptorOf` helpers and the inline unpacking.
     - Use the typed `rhi::*Descriptor` types in mirror members. They are the same 4 bytes, and a storage/sampled mix-up becomes a compile error.
     - Add `ResourcePool::storageDescriptor(handle)` to replace both `storageIndex` helpers.
  2. (S) Move every mirror into one exported partition, e.g. `aegis.render_graph:gpu_types`, each named after its Slang twin.
     - Add `tests/render_graph/gpu_types.test.cpp`, which pins `sizeof` and the `offsetof` of each member that follows padding.
     - Those are the pointer after `vec3`s, the `int8` cone fields, and the 112-byte `InstanceData` stride.
     - The expected numbers are written once, as a comment next to the Slang struct.
  3. (M, with the material system) Generate the expected offsets from Slang reflection (see trace A).

### A-BUILD-02: Constants that cross the CPU/GPU boundary are duplicated as literals
- **Where:**
  - **Workgroup sizes:** the Slang `numthreads` (16,16,1) and (64,1,1) are repeated as `groupCount(...,16)` in `ibl.cpp:22`, `lighting_pass.cpp:143`, `post_effects.cpp:117` and `screen_passes.cpp:151`, and as `groupCount(...,64)` in `gpu_driven.cpp:228`.
  - **View mode:** `lighting.slang` has `switch case 1..5`, matching `enum class LightingViewMode`.
  - **Tone mapping:** `post_process.slang` has `case 0/1`, matching `ToneMapping`.
  - **Point lights:** `MAX_POINT_LIGHTS` in Slang and `maxPointLights` in C++.
  - **Meshlet limits:** `gpu-driven/geometry.slang` has `MAX_VERTICES=64` and `MAX_PRIMITIVES=126`. The C++ side has `MeshPreprocessor::Input::maxVerticesPerMeshlet{64}` and `maxTrianglesPerMeshlet{126}` (`mesh_preprocessor.cppm:27-28`).
  - **G-buffer count 5:** in `common::GBuffer`, `GeometryPass::gBufferFormats` and lighting's `gBuffer` array.
- **Observed:**
  - Nothing links these values: no shared constant, assert or test.
  - The meshlet limits are fields of each call's `Input` with defaults. The loaders (`gltf_loader.cpp:75`, `obj_loader.cpp:30`) keep the defaults today.
- **Inference:**
  - **Concrete hazard:** a loader that sets `maxTrianglesPerMeshlet = 128`, a common meshoptimizer value, makes `SetMeshOutputCounts` exceed the 126 primitives the mesh shader declares. That is GPU undefined behaviour, and validation cannot see it.
  - **Second example:** changing `lighting.slang` to `numthreads(8,8,1)` while C++ still divides by 16 leaves three quarters of the pixels unwritten.
- **Severity:** design problem (a latent bug for the meshlet limits)
- **Confidence:** high
- **Affects:** new compute passes, meshlet and LOD work, debug view modes, tests.
- **Proposal:**
  1. (S) Read these values from the SPIR-V itself.
     - Add a pure helper `rhi::detail::reflectEntryPoint(std::span<const std::uint32_t> spirv, std::string_view entry)`.
     - It reads `OpEntryPoint` and `OpExecutionMode` (`LocalSize`, `OutputVertices`, `OutputPrimitivesEXT`). That is about 40 lines with no new dependency.
     - `rhi::Pipeline` stores `workgroupSize()`, and a new `CommandBuffer::dispatchThreads(Extent3D)` uses it. This replaces the five `groupCount` literal sites.
     - The helper can be unit-tested with hand-built word arrays, which follows the "extract, don't mock" rule.
     - The same reader lets the GPU-driven pass reject meshlet limits above `OutputPrimitivesEXT`. Alternatively, make the limit one named constant.
  2. (S) Name the enums on the Slang side with explicit values, and keep the C++ enums in `:gpu_types` next to their twins.

### A-BUILD-03: The task shader assumes a subgroup size of at least 32
- **Where:** `shaders/gpu-driven/task.slang` `taskMain` and `modules/meshlet_cull.slang` `TASK_GROUP_SIZE`. Grep finds no "subgroup" anywhere in `src/aegis`.
- **Observed:**
  - The shader runs `[numthreads(TASK_GROUP_SIZE,1,1)]` (32 threads). It computes `visibleCount = WaveActiveCountBits(...)` and `visibleIndex = WavePrefixCountBits(...)`, then calls `if (groupThreadID.x == 0) DispatchMesh(visibleCount, ...)`.
  - The RHI never queries or requires a subgroup size. `GpuDrivenGeometryPass::createResources` checks only `meshShaders`.
- **Inference:** this breaks on a device whose task shaders run with fewer than 32 lanes (some Intel configurations; lavapipe depends on the host's SIMD width).
  - Each subgroup computes `visibleIndex` from 0, so the subgroups overwrite each other's `meshletIDs` slots.
  - Thread 0 dispatches only its own subgroup's count, so meshlets go missing.
  - There is no validation error, and `--gpu-driven` still exits 0.
  - It is correct on NVIDIA (32 lanes) and AMD (32 or 64), which is why it has not shown up.
- **Severity:** bug (device-dependent)
- **Confidence:** medium. The wave semantics are certain; which devices run task shaders below 32 lanes was not verified.
- **Affects:** portability of the GPU-driven path. A lavapipe CI job could pass while rendering wrong output.
- **Proposal:** (S) Replace the wave prefix with a `groupshared uint` counter, `InterlockedAdd` and a group barrier before the dispatch. The alternative is to gate the path on `subgroupSize >= 32` in `Device::Capabilities`. Prefer the shader fix.

### A-BUILD-04: Shaders are identified by path strings resolved at run time, and each pass owns its pipelines
- **Where:**
  - `render_graph/shader.cppm`: `loadShader` and `create{Compute,Graphics}Pipeline`, with default entry names `"main"`, `"vertexMain"` and `"fragmentMain"`.
  - Call sites:
    - `ibl.cpp:54-63`
    - `lighting_pass.cpp:73`
    - `post_effects.cpp:45-47,252`
    - `screen_passes.cpp:42,115`
    - `gpu_driven.cpp:180,275-277`
    - the default argument at `static_mesh_render_system.cppm:36`
- **Observed:**
  - Each pass loads its shader with a literal such as `"bloom/threshold.slang.spv"` in its own `createResources`. A typo is a run-time `Error`.
  - Stale `.spv` files still load (agent-notes).
  - Each pass holds its pipeline as `std::optional<rhi::Pipeline>`, so nothing maps a source file to its pipelines.
- **Inference:**
  - The naming convention is consistent and costs little today.
  - The real limit is ownership: hot reload, permutations and a pipeline cache all need a source-to-pipeline map. With 9 creation sites across 6 files, each of those features must touch every pass.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** hot reload, the material and pipeline systems, testing `loadShader` (see A-BUILD-05).
- **Proposal:**
  - **NOW:** pass the source name (`"bloom/threshold.slang"`) and let `loadShader` append `.spv`, so the identity is the source file.
  - **WITH THE FEATURE:** add a `render_graph::ShaderLibrary`/`PipelineCache`. It owns pipelines built from `{source, entries, Desc}`, and passes hold handles. It replaces the free functions and the `optional<Pipeline>` members.
  - Don't add a registry before there is a second consumer.

### A-BUILD-05: Three path mechanisms, all absolute paths baked at configure time and leaked as PUBLIC macros
- **Where:**
  - root `CMakeLists.txt:33-36` and `core/CMakeLists.txt:24-27`: PUBLIC `PROJECT_DIR` and `BUILD_DIR`;
  - `core/globals.cppm:21-23`: `ENGINE_DIR`, `SHADER_DIR`, `ASSETS_DIR`;
  - `render_graph/CMakeLists.txt:17`: PRIVATE `AEGIS_SHADER_DIR`;
  - `src/aegis/test/CMakeLists.txt:31-34`: PUBLIC `SHADER_PATH` and `ASSETS_DIR` on an executable;
  - `modules/aegis-gltf/test/CMakeLists.txt:8`: `-DPROJECT_DIR` with a different value under the same name.
- **Observed:**
  - Nothing reads `core::SHADER_DIR`; shaders come from `AEGIS_SHADER_DIR` instead.
  - Assets come from `core::ASSETS_DIR` in the examples and from a separate `ASSETS_DIR` macro in `aegis-test`.
  - `PROJECT_DIR`/`BUILD_DIR` sit PUBLIC on `aegis-core`, which `assets` and `render_graph` link PUBLIC. The macros therefore reach every TU of `assets`, `render_graph`, `editor`, `engine` and the examples, although only `globals.cppm` reads them.
  - There are no `install()` rules.
- **Inference:**
  - Moving the build directory changes these definitions on most targets and recompiles them.
  - Generic macro names leaking PUBLIC can collide, as the gltf test already shows.
  - A relocated build cannot find its shaders.
  - `loadShader`'s error paths are pure file I/O but cannot be unit-tested, because the directory is a macro.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** shader and asset hot reload, packaging, tests of `loadShader`, every new test executable that loads data.
- **Proposal:** (S)
  - Add one `core::Paths{shaders, assets}`, or a `ShaderLoader{directory}` value, resolved at startup: an environment override (`AEGIS_SHADER_DIR`, `AEGIS_ASSETS_DIR`), else the configure-time default.
  - Make that default PRIVATE, prefixed `AEGIS_`, and read in one TU.
  - Pass the shader directory to `render_graph` through a Desc.
  - Delete `core::SHADER_DIR` and `aegis-test`'s duplicate `ASSETS_DIR`.
  - Exe-relative lookup and install rules: WITH THE FEATURE (packaging).

### A-BUILD-06: Shader compilation is a copied recipe with no validation step
- **Where:** `shaders/CMakeLists.txt` (`foreach` plus `add_custom_command`) and `src/aegis/test/CMakeLists.txt:19-27` (a second recipe).
- **Observed:**
  - The engine recipe uses `-profile <12 caps> -I shaders -fvk-use-entrypoint-name -matrix-layout-row-major -fvk-use-scalar-layout -warnings-disable 39001`, plus `-O0 -g` in Debug and `-O3` in Release.
  - The test recipe uses only `-target spirv -fvk-use-entrypoint-name`. The doc mismatch is map M7; the point here is that the flags can't be reused.
  - There is no `spirv-val` step, although agent-notes says to run it by hand and AGENTS.md documents a slangc release that emits invalid SPIR-V.
  - `RelWithDebInfo` and `MinSizeRel` builds get no `-O` flag.
  - With `COMPILE_SHADERS=OFF`, the three `device` tests (`tests/CMakeLists.txt:28-35`) stay registered and fail for lack of `.spv` files.
- **Inference:**
  - A second shader directory (an example's shader, a test shader, an editor shader) means copying the recipe, and a flag already got lost that way once.
  - Invalid SPIR-V is caught only when a device test happens to create that pipeline.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** shaders outside `shaders/`, SDK and slangc upgrades, CI.
- **Proposal:** (S)
  - Add `cmake/AegisShaders.cmake` with `aegis_add_shaders(<target> SOURCES … INCLUDE_DIRS … OUTPUT_DIR …)` that carries the flags once.
  - When `spirv-val` is found next to slangc, add a second COMMAND: `spirv-val --target-env vulkan1.3 --scalar-block-layout`.
  - Use it for both `shaders/` and `src/aegis/test/`.
  - Register the device tests only `if(TARGET compile_shaders)`.
  - Check whether slangc's `-depfile` works (unverified here). If it does, it can replace "every shader depends on every module".

### A-BUILD-07: `aegis-render-graph` is one target for the framework, every pass, IBL and the ImGui backend
- **Where:** `render_graph/CMakeLists.txt` (25 `.cppm`, 15 `.cpp`; PUBLIC `assets`, `scene`, `EnTT`; PRIVATE `imgui`, `vulkan-module`), `tests/render_graph/CMakeLists.txt`, and `shaders/CMakeLists.txt:63-64`.
- **Observed:**
  - The framework (`frame_graph`, `schedule`, `resource_pool`, `frustum`, `draw_batch_registry`) shares a target with `passes/*`, `ibl` and `ui_pass`. `render_graph.cppm` re-exports all 21 partitions.
  - `aegis-tests-render_graph` tests pure logic (`schedule`, `frustum`) but links the whole target, which pulls in the legacy scene/core/utils stack and imgui (map C13).
  - The target depends on `compile_shaders`, so the unit-test executable waits for all 15 shaders.
  - Adding a pass edits three central files: `CMakeLists.txt` (sources are listed, not globbed), `render_graph.cppm` and `default_graph`.
- **Inference:**
  - Every new pass touches the umbrella interface and recompiles all its importers (agent-notes: about 180 steps).
  - Framework tests stay coupled to legacy code.
  - The split is blocked on one fact: `render_pass.cppm` imports `aegis.scene`, because `FrameInfo` holds `Scene&`.
- **Severity:** design problem
- **Confidence:** high (structure), medium (the rebuild cost comes from agent-notes)
- **Affects:** every new pass, `render_graph` tests, build time, and a home for the material system.
- **Proposal:** (M)
  - Split into two targets:
    - `aegis-render-graph`: the framework only. No scene, no imgui, no shader dependency.
    - `aegis-render-passes`: the passes, `ibl`, `ui_pass`, `default_graph` and `:gpu_types`. It depends on `compile_shaders`.
  - `FrameInfo` must stop naming `Scene` first; that decoupling belongs to the render lens.

### A-BUILD-08: Link visibility does not follow the module import graph
- **Where:**
  - `scene/CMakeLists.txt`: core, math and EnTT are PRIVATE; nothing is PUBLIC.
  - `scripting/CMakeLists.txt`: math, scene, core and utils are PRIVATE.
  - `utils/CMakeLists.txt`: math is PRIVATE.
  - `assets/CMakeLists.txt`: core and scene are PUBLIC; EnTT is PRIVATE although no assets file uses it (map C6).
  - `core/CMakeLists.txt`: imguizmo is PUBLIC although core doesn't use it (map C7).
  - `src/aegis/CMakeLists.txt`: `aegis-engine` hosts three modules (`engine`, `defaults`, `scene_description`).
- **Observed:**
  - `scene/components.cppm` and `registry.cppm` do `export import aegis.math`, yet `aegis-scene` links math PRIVATE (map C8).
  - It works because every consumer gets math some other way.
- **Inference:**
  - The rule the build relies on is implicit: some other target already links it.
  - Following the documented recipe, `aegis_add_test(scene ... LIBS aegis-scene)` may not see the `aegis.math` module. Medium confidence; not built.
  - A test of `scene_description` would have to link the whole engine.
- **Severity:** design problem
- **Confidence:** medium
- **Affects:** new test modules (scene, scripting, core), new targets, splitting the engine target.
- **Proposal:** (S)
  - Write one rule into AGENTS.md: link PUBLIC what a `.cppm` imports, PRIVATE what only a `.cpp` imports, and nothing else.
  - Fix scene, scripting and utils accordingly; drop EnTT from assets and imguizmo from core.
  - Move `defaults` and `scene_description` into their own target when they get tests.

### A-BUILD-09: Dead and accidental build surface
- **Where:** `modules/CMakeLists.txt` (`add_subdirectory(aegis-gltf)`), `modules/aegis-gltf/CMakeLists.txt` (`option(BUILD_TEST ... ON)`, C++20), `external/CMakeLists.txt` (doctest), root `CMakeLists.txt:13-15`, `rhi/CMakeLists.txt:4`.
- **Observed:**
  - No target links `Aegis::GLTF`.
  - Its `BUILD_TEST` option defaults ON, so every engine build also compiles and links `aegis-gltf-testing.exe` (present in `build/audit/modules/aegis-gltf/test/`). It also leaves a generic `BUILD_TEST:BOOL=ON` in the cache (`build/audit/CMakeCache.txt:42`).
  - doctest is configured even when tests are off.
  - `find_package(Vulkan)` runs twice (root and rhi), and the root has a dead `if(NOT Vulkan_FOUND)` check.
- **Inference:** this inflates the target graph and every CI job with an unrelated executable.
- **Severity:** polish
- **Confidence:** high
- **Affects:** build time and readability.
- **Proposal:** (S) Remove the gltf `add_subdirectory`, or set `BUILD_TEST` OFF before it. Add doctest only `if(AEGIS_BUILD_TESTS)`. Drop the duplicate `find_package` and the dead check.

### A-BUILD-10: CI proves compilation only; nothing GPU-side runs, not even shader validation
- **Where:** `.github/workflows/build.yml` (jobs `linux` and `windows`, `ctest -LE device`).
- **Observed:**
  - Four jobs (Linux and Windows, debug and release) each configure, build and run `ctest -LE device`.
  - Examples build but never run.
  - There is no `spirv-val` step.
  - `aegis-test-scene-gpu-driven` fails rather than skips without mesh shaders: `createResources` returns "The GPU-driven path needs mesh shaders".
- **Inference:**
  - Everything in this group that breaks silently passes CI: A-BUILD-01, -02, -03 and -06.
  - `spirv-val` is the cheapest gain without a GPU. A lavapipe job comes next, but only once the mesh-shader test can be separated.
- **Severity:** design problem
- **Confidence:** high
- **Affects:** every shader and RHI change.
- **Proposal:**
  - **NOW (S):** validate shaders in the build (A-BUILD-06), which gives CI that check for free. Label the GPU-driven test `device;mesh-shaders`, and have `aegis-test` exit with a skip code (CTest `SKIP_RETURN_CODE`) when a capability is missing.
  - **WITH THE FEATURE (M):** a Linux lavapipe job running `ctest -L device -LE mesh-shaders`. Fix A-BUILD-03 first.

### A-BUILD-11: Device tests are registered by hand, and examples cannot be registered at all
- **Where:** `tests/CMakeLists.txt:26-35`, the identical 5-line `examples/*/CMakeLists.txt` files with `project(<Name>)`, and `examples/CMakeLists.txt`.
- **Observed:**
  - The three device tests are hand-written `add_test`/`set_tests_properties` pairs in `tests/`, while the `aegis-test` target lives in `src/aegis/test/`.
  - Examples have no tests, and each one calls `project()`.
- **Inference:**
  - Each new device scenario (IBL, screenshot comparison) is copied boilerplate.
  - The missing headless `Engine` mode is agent-notes idea 1 and not repeated here. On the build side, there is no helper to call once it exists.
- **Severity:** polish
- **Confidence:** high
- **Affects:** new device scenarios, examples as smoke tests, reference images.
- **Proposal:** (S)
  - Add `aegis_add_device_test(<name> COMMAND <target> <args> [LABELS])`, which sets the `device` label, timeout and skip code in one place.
  - Add `aegis_add_example(<Name>)` in `examples/CMakeLists.txt` in place of the per-example `project()`. It registers the example once `Engine` supports `--headless --frames N`.

## 2. Extensibility traces

### A. A new shader and its CPU/GPU data interface
Scenario: an SSAO compute pass that gets a camera pointer, two storage indices and one texture descriptor.

| Step | Where | Additive or modifying | Guard today |
|---|---|---|---|
| 1. `shaders/ssao.slang` with `numthreads(8,8,1)`, a `PushConstants` struct and `[[vk::push_constant]]` | new file | additive | none |
| 2. A shared struct: `import modules.common`; a new one goes into `modules/common.slang` | central module | modifying; recompiles all 15 shaders | none |
| 3. CMake | glob with `CONFIGURE_DEPENDS` | no edit | stale `.spv` after a rename |
| 4. Output | `build/<preset>/shaders/ssao.slang.spv` | – | – |
| 5. Load | `createComputePipeline(dev,"Ssao","ssao.slang.spv")` | additive | run-time `Error` |
| 6. C++ mirror | a local struct copied member by member, `uint32_t` indices, a 4th `TextureDescriptor` or 3rd `descriptorOf` | additive, duplicating | `sizeof <= 128` only |
| 7. Fill it | `pool.bufferAddress`, `get(view).storageDescriptor()->index`, a 3rd `storageIndex` | duplicating | none |
| 8. Dispatch | `groupCount(x, 8)`, the 8 copied from step 1 | duplicating | none |
| 9. Register the pass | `render_graph/CMakeLists.txt`, `render_graph.cppm`, `default_graph` | modifying 3 central files; rebuilds every importer | compile |
| 10. Test | not possible for the struct; runtime coverage only through `aegis-test --scene` | – | validation only |

- **Central files:** `render_graph/CMakeLists.txt`, `render_graph.cppm` and `default_graph` for every pass; `modules/common.slang` when data is shared.
- **Boilerplate:** about 25 duplicated lines per pass.
- **Checkable without a GPU, once fixed:**
  - the mirror layout (A-BUILD-01);
  - the workgroup size (A-BUILD-02);
  - SPIR-V validity (A-BUILD-06);
  - `loadShader`'s errors (A-BUILD-05).
- **Verdict:** the shape is right. Shader files are additive, sources are globbed, there is one pipeline layout and each pass owns its resources. The data contract, however, is unguarded and copied, and three central lists must be edited per pass. Adding is cheap; changing a shared struct is not safe.
- **Smallest changes that make it safer**, in order:
  1. Typed descriptors plus `rhi::TextureDescriptor` and `ResourcePool::storageDescriptor` (S).
  2. A `:gpu_types` partition plus a layout test (S).
  3. Workgroup size read from SPIR-V plus `dispatchThreads` (S).
  4. `spirv-val` in the recipe (S).
- **Not now:** a C++/Slang shared header with type macros. It fights the modules-only build and Slang's module system for little gain over a layout test.
- **The real fix, together with the material system (M):**
  - `slangc -reflection-json` (unverified for this slangc version).
  - A `cmake -P` script using `string(JSON …)` turns it into a header of `constexpr` offsets, which `:gpu_types` checks with `static_assert`. No Python and no runtime cost.
  - First verify that the reflection covers the structs behind pointers (`Lighting*`, `Instance*`), not only the push-constant block.

### B. A new test module (`aegis-tests-scene`)
- Steps:
  1. Add `tests/scene/CMakeLists.txt` containing `aegis_add_test(scene SOURCES … LIBS aegis-scene)`.
  2. Add one `add_subdirectory(scene)` line to `tests/CMakeLists.txt`.
  3. Import the dotted submodules directly (`aegis.scene.registry`), unlike the partition rule in the README.
- **Trap:** A-BUILD-08. Expect to add `aegis-math` to `LIBS`, or fix the link first.
- **Verdict:** good. Labels and test discovery are automatic. Globbing `tests/*/CMakeLists.txt` would save one line per module and isn't worth the indirection.

### C. A new example registered as a device CTest
- Adding the example: a directory, a copied 5-line CMake file and a line in `examples/CMakeLists.txt`.
- Registering it is impossible today: `Engine` has no headless or frame-limit mode (agent-notes idea 1).
- Once it has one, registration is a copied `add_test` pair, and it must sit inside `if(AEGIS_BUILD_TESTS)` because `enable_testing()` is only called there.
- Asset paths come from the configure-time `core::ASSETS_DIR`.
- **Verdict:** blocked on the engine; on the build side, only a helper is missing (A-BUILD-11).

## 3. Planned extensions: build-side needs
Tags: NOW = before features start; WITH = as part of that feature; NOT NEEDED.

**Shader hot reloading**
- Watch the `.slang` sources and rebuild through the existing `compile_shaders` target. This keeps one compiler path and one flag set. A Slang session inside the engine (linking libslang and duplicating the flags in C++) is NOT NEEDED.
- An injectable shader directory that knows the source directory (A-BUILD-05): NOW (S).
- The source name as the shader identity (A-BUILD-04): NOW (trivial).
- `ShaderLibrary`/`PipelineCache` with deferred destruction through the existing deletion queue: WITH.
- Stale `.spv` cleanup (agent-notes idea 5): WITH.

**Material and pipeline systems**
- The parameter block contract (`PbrMaterial` matched by call order): a reflection check, or a generator that emits the Slang struct from the C++ template: WITH (M). The `:gpu_types` and typed-descriptor groundwork: NOW.
- Permutations: `-D` defines and output names per variant, through `aegis_add_shaders(… VARIANTS …)`. The function: NOW; the variants: WITH.
- Specialization constants: no build change; RHI `Desc` work: WITH.
- Pipeline libraries and a cache file: only a writable path, from A-BUILD-05: WITH.
- Note for the render lens: the GPU-driven path binds one pipeline for all draw batches (`GpuDrivenGeometryPass::execute`). Per-material shaders change that pass, not the build.

**Asset hot reload and asset paths**
- Turn `core::ASSETS_DIR` and `aegis-test`'s `ASSETS_DIR` into one runtime value with an override: NOW (S).
- `install()` rules and exe-relative lookup: WITH (packaging). Hot reload itself needs neither.
- Case sensitivity: already an AGENTS.md rule; NOT NEEDED.

**Multithreaded and job-based work**
- No build-side need today. No `CMakeLists.txt` or workflow links `Threads::Threads` or mentions pthread (Grep).
- A job system should link `Threads::Threads` PUBLIC from its owning target: WITH.
- A TSan preset (`linux-clang-tsan`): WITH.

**CI GPU job (lavapipe)**
- Fix A-BUILD-03; add the `mesh-shaders` label and skip codes: NOW (S).
- The job itself: WITH (M).
- `spirv-val` in the build: NOW. It needs no GPU.

## 4. Target build structure (minimal)
```
cmake/AegisShaders.cmake   aegis_add_shaders(<target> SOURCES … [VARIANTS …])   slangc + spirv-val
cmake/AegisTesting.cmake   aegis_add_test(<module> …)   aegis_add_device_test(<name> COMMAND …)

aegis-platform       PUBLIC glfw
aegis-rhi            PUBLIC platform · PRIVATE vulkan-module, vma        (+ rhi::TextureDescriptor)
aegis-renderer       PUBLIC rhi
aegis-assets         PUBLIC rhi, math     (scene/core edges: render/app lens)
aegis-render-graph   PUBLIC rhi, renderer, math       framework only, no shaders
aegis-render-passes  PUBLIC render-graph, assets · PRIVATE imgui, vulkan-module
                     :gpu_types = every CPU/GPU mirror · depends on compile_shaders
legacy targets       same shape; links follow the visibility rule
aegis-engine         facade only
```
Rules:
1. Link PUBLIC what a `.cppm` imports, PRIVATE what only a `.cpp` imports, and nothing else.
2. Path definitions are PRIVATE, prefixed `AEGIS_`, and read in one TU.
3. CPU/GPU mirrors live in one `:gpu_types` partition per owning module (assets keeps `Vertex`, `Meshlet` and `GpuData`), each with a comment naming its Slang twin, pinned by a layout test.
4. Shaders compile only through `aegis_add_shaders`, which validates them.
5. Each library gets one `aegis-tests-<module>` through `aegis_add_test`. Device scenarios go through `aegis_add_device_test`, labelled `device` (plus `mesh-shaders` where needed).

## 5. What works well and should stay
- **One pipeline layout, bindless descriptors and buffer device addresses.** Push constants are the only interface per draw, so a new shader needs no descriptor-set code. This is why trace A is additive.
- **Scalar layout and row-major matrices for every engine shader.** C++ structs map to Slang member for member, with no std140 rules. The proposed layout test relies on it.
- **Globbed shader sources with every module as a dependency.** Coarse but never wrong, and adding a shader needs no CMake edit.
- **The fail-fast SDK check in `rhi/CMakeLists.txt`.** It turns a deep build failure into one configure message.
- **`aegis_add_test`, with one labelled executable per module.** A new module is two lines, and `-L` and `-LE device` work as documented.
- **Presets with warnings-as-errors in the base, and CI running the same presets.** Local and CI flags cannot drift.
- **`external/` added as SYSTEM.** Third-party warnings never break the `-Werror` build.
- **`vulkan-module` linked PRIVATE.** It keeps `vk::` types out of test TUs, which is what makes the "extract, don't mock" rule enforceable.
