# Findings E: shaders and build, testability lens

Scope: `shaders/`, the C++ mirrors of Slang structs, all CMake, presets, `tests/CMakeLists.txt`, CI.
Read-only, against `build/audit` (Clang debug, SDK 1.4.328.1). Map facts used here were re-checked
against the code; known gotchas from `docs/agent-notes.md` are cited, not re-reported.

## 1. What is checked today

- **CTest** (`tests/CMakeLists.txt`): `aegis_add_test` builds one doctest exe per module and runs
  `doctest_discover_tests(... PROPERTIES LABELS <module>)`, so each `TEST_CASE` becomes one CTest
  entry. Labels: `rhi`, `assets`, `render_graph`, `math`. The 108 `TEST_CASE`s become 109 entries (E-BUILD-01).
  Three `device` tests run `aegis-test --headless` (triangle, `--scene`, `--gpu-driven`), `TIMEOUT 300`.
- **Device pass signal is debug-only.** `Context::enableValidation` is false under `NDEBUG`
  (`rhi/context.cppm:28`), so a release device run proves only "no crash, no failed check". In
  debug, a missing layer fails `Context::findLayers`, so validation cannot silently go missing.
  Warnings print but do not fail (map M11).
- **CI** (`build.yml`): Linux (Clang 22, libstdc++ 15) and Windows Clang, debug and release;
  `checkout` with `submodules: recursive` (assets present); builds examples and shaders; runs
  `ctest -LE device`. Release CI does cover "breaks with asserts compiled out" for build and unit tests.
- **CPU/GPU interface:** only `sizeof(T) <= 128` (`CommandBuffer::pushConstants`,
  `command_buffer.cppm:76`; again in `lighting_pass.cpp:43`, `static_mesh_render_system.cppm:33`).
  No offset, order or stride check. `material.test.cpp` tests `MaterialTemplate`'s packing rules,
  not the real `pbrTemplate` against `common::PbrMaterial`.
- **Shaders:** a slangc error fails the build. No `spirv-val`, no reflection output, no stale cleanup.

## 2. Findings

### E-BUILD-01: A `;` in a TEST_CASE name silently drops the case from CTest
- **Where:** `tests/render_graph/schedule.test.cpp` `TEST_CASE("reads in the same layout need no barrier; a layout change does")`; `external/doctest/scripts/cmake/doctestAddTests.cmake` (`string(REPLACE "\n" ";" output ...)`)
- **Observed:** discovery splits the case list on `;` (a CMake list). The generated
  `build/audit/tests/render_graph/aegis-tests-render_graph_tests-b858cb2.cmake:35-37` registers
  `--test-case=reads in the same layout need no barrier` and `--test-case= a layout change does`;
  both "passed" (`ctest.log` #37, #38).
- **Inference:** neither filter matches the real name, so doctest selects zero cases and exits 0.
  The case runs only when the exe is run by hand; in ctest and CI it shows green twice and never runs.
  Commas are escaped by the script and are safe.
- **Severity:** bug
- **Confidence:** high for the split; medium that doctest exits 0 on an empty selection
- **Affects:** every `aegis_add_test` module; trust in "N tests passed"
- **Proposal:** split the case in two (the "one assertion focus" rule wants that anyway) and add
  "no `;` in TEST_CASE names" to tests/README.md. If per-case CTest entries aren't valued,
  register one `add_test` per executable instead (removes this bug class and the POST_BUILD
  discovery run). S.

### E-BUILD-02: CPU/GPU struct layouts are matched by hand and checked by nothing
- **Where:** about 20 twins (map/build.md §2). Test-visible (exported): `render_graph::CameraData`, `Frustum`, `InstanceData`, `DrawBatchData`, `LightingPass::Uniforms`, `StaticMeshRenderSystem::PushConstants`, `assets::Vertex`, `BoundingSphere`, `Meshlet`, `StaticMesh::GpuData`. Not test-visible: anonymous `PushConstants` in `lighting_pass.cpp`; function-local structs in `gpu_driven.cpp:189,294`, `screen_passes.cpp:70,124`, `post_effects.cpp:123,140,163,268`, `ibl.cpp:142,173,216`
- **Observed:** no `offsetof` or `sizeof ==` assert anywhere (Grep). Some layouts rest on subtle
  scalar rules: `InstanceData` (84 bytes of matrix and vec3s, a pointer padded to 88, stride 112);
  `Meshlet` (30 bytes, 32 only if Slang rounds struct size up to alignment); C++ flat
  `coneAxis[3]/coneCutoff` vs Slang's nested `NormalCone`.
- **Inference:** a field added or reordered on one side compiles, passes validation (which does not
  check push-constant contents or reads through a device address) and renders wrong. Not even the
  debug device tests catch it. Today "do these match?" is answered by reasoning about Slang's layout
  rules, which a machine should do.
- **Severity:** design problem · **Confidence:** high
- **Affects:** every new pass, shader field, GPU-driven or material extension
- **Proposal:** seam S1 (section 4). Replaces the prose "mirrors X" comments as source of truth. M.

### E-BUILD-03: `pbrTemplate`'s parameter order lives inside a device-owning factory
- **Where:** `assets/defaults.cpp` `DefaultAssets::create(rhi::Device&, ...)`; `common.slang` `PbrMaterial` ("in order")
- **Observed:** the ten `pbr.addParameter(...)` calls that must match `PbrMaterial` run in the same
  function as five `Texture::solidColor(device, ...)` calls.
- **Inference:** the one layout assembled at run time can't be unit-tested, because building it
  needs a device. `material.test.cpp` shows a null `shared_ptr<Texture>` is fine for layout, so the
  device dependency is incidental.
- **Severity:** design problem · **Confidence:** high
- **Affects:** new material parameters and templates; a no-GPU PBR-block test
- **Proposal:** extract `assets::detail::addPbrParameters(MaterialTemplate&, white, normal)` (the
  stateless-helper pattern in tests/README.md); `create` calls it. A test builds it with null
  textures and checks offsets against S1's header (or hand-pinned values). S.

### E-BUILD-04: Values shared by Slang and C++ are duplicated as literals
- **Where:** `numthreads(16,16,1)` vs `groupCount(..,16)` in `lighting_pass.cpp:143`, `screen_passes.cpp:151`, `post_effects.cpp:117`, `ibl.cpp:22`; `numthreads(64,1,1)` vs `groupCount(instanceCount, 64)` (`gpu_driven.cpp:228`); `MAX_POINT_LIGHTS` vs `LightingPass::maxPointLights`; `LightingViewMode`/`ToneMapping` vs `switch` literals; `TextureDescriptor` redefined in `ibl.cpp:26`, `lighting_pass.cpp:22`, `post_effects.cpp:106`
- **Observed:** nothing links the two sides; three identical C++ `TextureDescriptor` structs.
- **Inference:** a wrong group size under-dispatches (part of the image is never written) with no
  validation error. Each new compute pass copies the pattern, including a fourth `TextureDescriptor`.
- **Severity:** polish (no mismatch today) · **Confidence:** high
- **Affects:** every new compute pass
- **Proposal:** (a) one exported `TextureDescriptor` (e.g. returned by `assets::Texture`) replaces the
  three locals, S. (b) Take group sizes from reflection (`threadGroupSize` per entry point) into S1's
  header. Enums and `MAX_POINT_LIGHTS` stay hand-mirrored; a shared-header mechanism for 3 values
  isn't worth it.

### E-BUILD-05: Shader compile rules exist twice, with different flags
- **Where:** `shaders/CMakeLists.txt` (foreach); `src/aegis/test/CMakeLists.txt` (`test-shaders`)
- **Observed:** the test shader is compiled with only `-target spirv -fvk-use-entrypoint-name`: no
  `-profile`, scalar layout, row-major, `-I`, `-O`/`-g` (doc side: map M7).
- **Inference:** the harness's own shader doesn't use production flags, and every new shader site
  (example, test) copies a third custom command; any added check must be added in each copy.
- **Severity:** design problem (small) · **Confidence:** high
- **Affects:** new shader locations; spirv-val and reflection steps
- **Proposal:** one `aegis_compile_slang(<target> SOURCES .. OUTPUT_DIR ..)` CMake function holding
  flags, naming and later the C2/C3 commands; both sites call it. S.

### E-BUILD-06: Shader binaries are found by string, and stale ones still load
- **Where:** `render_graph/shader.cpp` `loadShader`; literals in 8 files (map/build.md §1.1)
- **Observed:** path and entry-point literals; outputs of deleted sources stay (agent-notes, Build).
- **Inference:** a typo or a removed shader surfaces only at run time in a device test, which CI
  skips; a stale `.spv` hides a removal entirely.
- **Severity:** polish · **Confidence:** high
- **Affects:** renaming or moving shaders
- **Proposal:** delete orphaned `.spv` files at configure time (agent-notes idea 5), S. With S1, a
  generated path constant per shader replaces the literals, so a removed shader is a compile error.

### E-BUILD-07: Unit-test executables link whole subsystems
- **Where:** `tests/render_graph/CMakeLists.txt` `LIBS aegis-render-graph`; `shaders/CMakeLists.txt` `add_dependencies(aegis-render-graph compile_shaders)`
- **Observed:** `aegis-tests-render_graph` gets `assets`, `scene`, `EnTT`, `core` and more through
  PUBLIC links, and building it waits for all 15 shaders, though `schedule`, `frustum` and
  `grownCapacity` are pure.
- **Inference:** the cost is build time and coupling, not correctness. The root cause is U2
  (render_graph imports scene). A future `tests/scene` may not see `aegis.math` through
  `aegis-scene`'s PRIVATE links (map/imports.md §5; unverified).
- **Severity:** design problem · **Confidence:** medium
- **Affects:** render_graph test iteration; adding scene or core tests
- **Proposal:** no new target now. When render_graph's framework is split from the passes for other
  reasons, tests link the framework only; before adding scene tests, make `aegis-scene` link PUBLIC
  what its interfaces `export import`. S each, when needed.

## 3. Blockers

| Blocker | Fact | Blocks |
|---|---|---|
| B1 no reflection | slangc runs without `-reflection-json`; no layout data leaves the shader build | any automated layout comparison |
| B2 hidden twins | 12 of about 20 mirrors are anonymous or function-local in `.cpp` | a test TU can't name them (and the re-export rule wouldn't help) |
| B3 device-bound factory | `pbrTemplate` is built in `DefaultAssets::create(Device&)` | PBR layout test |
| B4 heavy test links | E-BUILD-07 | cheap render_graph and scene tests |
| B5 no GPU in CI | hosted runners; validation off in release | device tests, golden images, examples in CI |
| B6 two compile rules | E-BUILD-05 | adding spirv-val or reflection once |

Assets are not a blocker: CI checks out submodules recursively (`aegis-assets` included).

## 4. Smallest seam per blocker

- **S1 (B1+B2): a generated layout header plus `static_assert` at each twin.** `aegis_compile_slang`
  also passes `-reflection-json <x>.json` (check the flag on slangc 2025.17). A CMake-script step
  (`string(JSON)`, no Python) writes `shader_layout.h`: per shader, each push-constant and
  pointer-reached struct's member offsets, sizes and the entry group sizes. Each twin, where it
  already lives, adds `static_assert(offsetof(PushConstants, gBuffer) == layout::lighting::PushConstants::gBuffer)`.
  This catches drift on either side at compile time and doesn't need the hidden structs moved or
  exported (B2 disappears). It is cheaper than a runtime CTest comparison, which would need every
  twin visible to a test. Not over-engineering: it replaces about 20 prose "mirrors X" comments with checks.
- **S2 (B3):** the `detail::addPbrParameters` extraction (E-BUILD-03).
- **S3 (B6):** `aegis_compile_slang` (E-BUILD-05); prerequisite for S1 and C3.
- **S4 (B4):** none now (E-BUILD-07); wait for the render_graph split.
- **S5 (B5):** lavapipe job (section 6), only after C1-C4 are in; they need no GPU.

## 5. Candidate checks, ranked

| # | Check | Catches | Runs | Effort |
|---|---|---|---|---|
| C1 | Fix `;` case name, add README rule (or per-exe `add_test`) | a test that never runs while showing green | CTest, CI | S |
| C2 | S1 layout header + `static_assert` at each twin | member order, type width and stride drift on either side | build (every preset, CI) | M |
| C3 | `spirv-val --target-env vulkan1.3 --scalar-block-layout` after each slangc call | slangc emitting invalid SPIR-V (the known 1.4.328 bug) | build step | S |
| C4 | `addPbrParameters` + offsets test against `PbrMaterial` | material block order drift | CTest `assets` | S |
| C5 | Orphaned `.spv` cleanup at configure | stale binaries masking removed shaders | configure | S |
| C6 | Group sizes from S1 replace literals | under-dispatch | build | S (after C2) |
| C7 | Missing-include scanner checked in (agent-notes idea 4) | libstdc++-only include breaks before CI | CTest `lint` or CI step | S |
| C8 | Headless examples as `device` tests (agent-notes idea 1) | example regressions, validation errors in examples | CTest `device` | M |
| C9 | Golden images, tolerance compare of `--screenshot` | wrong output that validation can't see | CTest `golden`, local GPU | M |
| C10 | lavapipe device job on Linux | uploads, bindless, sync, layouts at run time in CI | CI | M |

## 6. Strategy view

- **Test helper scaling.** `aegis_add_test` is the right size: a new module is one call. Adding
  `tests/scene` needs only link-visibility fixes (E-BUILD-07). Device tests are 3 raw `add_test`
  calls; a 6-line `aegis_add_device_test(name ARGS ..)` setting `LABELS device` and `TIMEOUT` is
  worth it only once examples (C8) or golden images (C9) add more entries. Use a separate `golden`
  label so a GPU-less but validation-capable run (lavapipe) can skip image compares, whose results
  differ across drivers.
- **Device test granularity.** Each device test runs the whole 1490-line `test/main.cpp`; a failure
  names the mode, not the check. Fine at 3 tests; revisit if C8/C9 grow it.
- **Release.** Release device runs have no validation; keep device tests debug-only in any CI job.
- **lavapipe on Linux (feasibility only).** Needs: Mesa's lavapipe ICD (`mesa-vulkan-drivers`, recent
  enough for Vulkan 1.3, buffer device address, descriptor indexing, sync2 and dynamic rendering;
  Ubuntu 24.04's stock Mesa may need a newer PPA); the SDK's validation layer found by the loader
  (CI sets only `VULKAN_SDK`/`LD_LIBRARY_PATH`, so `VK_ADD_LAYER_PATH=$VULKAN_SDK/share/vulkan/explicit_layer.d`,
  or source `setup-env.sh`), else debug `findLayers` fails hard; `VK_DRIVER_FILES` pointing at
  `lvp_icd.x86_64.json`. Headless mode needs no display. `--gpu-driven` needs mesh shaders
  (`gpu_driven.cpp:265` errors without them); lavapipe support depends on the Mesa version, so
  label that test `mesh` or let it skip. 600 software-rendered frames at a 300 s timeout may be
  tight; use `--frames 60` in CI. Confidence: medium on the overall approach, low on lavapipe's
  exact feature coverage.
