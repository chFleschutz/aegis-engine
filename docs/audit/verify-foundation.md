# Verification: foundation

Scope: findings-a-foundation.md, findings-b-foundation.md, findings-e-foundation.md. I read every cited symbol in the code. The math, assert, logging and clock claims were reproduced with standalone programs in `build/audit/verify/foundation/` (glm from `external/glm`, formulas copied verbatim). The source tree was not modified, and nothing in the project was built.

## 1. Summary counts

| File | Bug/design findings | Verified | Unverified | Refuted | Reclassified | Polish |
|---|---|---|---|---|---|---|
| findings-a-foundation.md | 8 (A-01..05, 07, 08, 09) | 8 | 0 | 0 | 2 (A-07 bug→design, A-09 design→polish) | A-06, A-10, A-11 kept |
| findings-b-foundation.md | 4 (B-01, 02, 04, 06) | 4 | 0 | 0 | 1 (B-06 bug→polish) | B-03, 05, 07–14 kept |
| findings-e-foundation.md | 4 (E-01..04) | 4 | 0 | 0 | 2 (E-03, E-04 design→polish) | E-05, E-06 kept |

## 2. Table

| ID | Short title | Severity (filed → verified) | Verdict | Evidence | Canonical |
|---|---|---|---|---|---|
| A-FOUND-01 | Error types have no shared way to describe them; the cause is dropped | design → design | VERIFIED | Four shapes (`rhi/error.cppm:11`, `renderer.cppm:20`, `render_graph/error.cppm:47`, `assets/error.cppm:83`). Grep finds no reader of `.rhi` or `.location`. `ibl.cpp:84,137,168,198,260` re-wrap `.message` only. `QueueSubmitFailed` and `SwapchainPresentFailed` are never produced. | A-FOUND-01 |
| A-FOUND-02 | Foundation layer has the wrong shape; live helpers sit in rhi | design → design | VERIFIED | `aegis.utils.*` is imported only by `core/profiler.cppm:9-10`. `alignTo`/`roundUpTo` are used in assets, render_graph and rhi (`material.cpp:83`). math/utils link `Aegis::Log`, but nothing in them uses it. | A-FOUND-02 |
| A-FOUND-03 | Logging is an engine-owned singleton; the render stack prints to stdout | design → design | VERIFIED | `singleton.h:26` `return *s_instance;` (nullptr until init). Repro: `ALOG::warn` with no init → segfault (exit 139). 8 `std::println` sites confirmed. | A-FOUND-03 |
| A-FOUND-04 | AGX_ASSERT is in the wrong layer, reached through an include hack, and used for input validation | design → design | VERIFIED (facts corrected, §5) | `assert.h:32-34`: release macros are `static_cast<void>(0)`. `color.cppm:35-38` asserts and then returns white. The camera assert accepts 0 (computed). | A-FOUND-04 |
| A-FOUND-05 | Three flag-enum implementations; directive-only operators apply to every enum | design → design | VERIFIED | `utility.cppm:73` `FlagEnum = std::is_enum_v<T> /*&& isFlagEnum<T>*/`. 13 `using namespace rhi::utility` (6 in src, 7 in test/main.cpp). `input.cppm:139-146` defines its own operators. | A-FOUND-05 |
| A-FOUND-07 | vec3 Euler overloads mix the YXZ and ZYX orders | bug (latent) → design | VERIFIED, reclassified | r=(0,.5,.5): `forward(r)`=(-0.4794,0.8776,0); column 1 of `tranformationMatrix(r)`=(-0.4207,0.8776,0.2298). `tranformationMatrix(vec3)` equals Ry·Rx·Rz exactly (diff 0). It has no caller, so nothing fails today. | A-FOUND-07 |
| A-FOUND-08 | `Camera::inverseViewMatrix` is wrong | bug (latent) → bug (latent) | VERIFIED | `camera_system.cppm:64-66` stores +f. `inverseView*view` ≠ I (e.g. [0][0]=0.2703). View point (0,0,-5) maps to (4.02,-1.60,4.71) instead of (-2.02,5.60,1.29). The field has no reader today. | A-FOUND-08 |
| A-FOUND-09 | Process globals: RNG, profiler, logger | design → polish | VERIFIED, reclassified | `random.cppm:99`; `profiler.cppm:82-86`. Random is used only by examples (eval-scene ~20 `generator()` calls, simple-scene 3). No threads exist today. The finding's own actionable part is deleting things. | A-FOUND-09 |
| B-FOUND-01 | decomposeTRS(quat) drops the mirror, returns a non-unit quat, NaN at zero scale | bug → bug | VERIFIED (stronger than filed) | scale(-1,1,1) → s=(1,1,1), q=(w .7071,0,0,0), recomposed [0].x=+1. Rot 30°Z with scale(-1,2,1) → q=(.7071,0,0,0), so the **rotation is lost too** (det -2 → +2). scale.x=0 → q=NaN. Positive scale round-trips (err 7e-9). | B-FOUND-01 |
| B-FOUND-02 | AGX_ASSERT message lost when stdout is redirected; AGX_DEBUG_BREAK never runs | bug → bug | VERIFIED (Windows reproduced; Linux by glibc ≥2.27 docs) | Forced `AGX_ASSERT_X` with stdout to a file (release and debug UCRT): stdout file is empty. stderr shows only `Assertion failed: false && "Assertion failed!", file …core/assert.h, line 44`. Exit 3 (abort), so the trap is never reached. | B-FOUND-02 |
| B-FOUND-04 | Log config macros apply per TU; ALOG before Logging dereferences null | design → design | VERIFIED | `logging.cppm:3-8` defines the config macros; `engine.cppm:5` includes `log.h` without them; the null deref is reproduced. No template is instantiated today under two configs (`info<>` is the same in both TUs; `fatal` is only in engine.cppm). | A-FOUND-03 |
| B-FOUND-06 | CameraSystem aspect assert never fires for 0 | bug → polish | VERIFIED (fact); scenario not reachable via the engine | aspect=0: condition =1 (passes), `[0][0]`=inf. The only aspect producer is `scene_update_pass.cpp:30`. Zero-extent frames are skipped (`window.cppm:37` `isMinimized` = w==0‖h==0), and swapchain extents are ≥1. Only user code could set 0. | B-FOUND-06 |
| E-FOUND-01 | Euler overloads mix two orders; all are dead | design → design | VERIFIED (small correction, §5) | Same numbers as A-FOUND-07. No vec3 overload has a caller outside tests and ai/. | A-FOUND-07 |
| E-FOUND-02 | decomposeTRS assumes positive, nonzero scale | bug → bug (confidence low → high) | VERIFIED | See B-FOUND-01. Reachable via the scene panel's unclamped `DragFloat3("Scale")` (`scene_panel.cppm:184`) or a mirrored glTF node (`gltf_loader.cpp:219-220`), followed by any gizmo drag. | B-FOUND-01 |
| E-FOUND-03 | About two thirds of the foundation is unreachable or uncalled | design → polish | VERIFIED, reclassified | Every listed symbol has 0 uses in src/tests/examples (excluding ai/), except `inFOV` and `forward/right/up(vec3)`, which are used by tests only. Deletion is the whole fix, so it is polish. | A-FOUND-06 |
| E-FOUND-04 | math::Random is global state with no engine consumer | design → polish | VERIFIED, reclassified | Consumers are only `examples/eval-scene` and `simple-scene` (plus dead `Color::random`). | A-FOUND-09 |
| a polish (A-06, A-10, A-11) | dead code; three file readers; repeated GPU-transform packing | polish | KEPT | Dead lists grep-verified (§6). | — |
| b polish (B-03, 05, 07–14) | — | polish | KEPT (B-03 → A-FOUND-07) | RollingAverage: 1 sample of 16.7 → 0.334 (FPS 2994); 10 s stall → 216.37 ms (FPS 4.62). `high_resolution_clock`: libstdc++ is_steady=0 / same as system_clock. `inFOV((0,1,0),(10,1,0),10°)`=true. sigmoid01(0)=.00247, fastSigmoid01(0)=.0714. libstdc++ `random.h:2138` asserts stddev>0. | — |
| e polish (E-05, E-06) | — | polish | KEPT | E-05 (once at 10 ms → 0.2) reproduced. `Profiler() = default;` is private (`profiler.cppm:117`). | — |

## 3. Refuted findings

None of the bug or design findings is refuted. Two failure paths are narrower than filed:
- **B-FOUND-06:** the cited route to aspect 0 (a zero extent from `SceneUpdatePass`) cannot happen. Hence the reclassification to polish.
- **B-FOUND-01:** "a scale axis dragged to 0" cannot happen through the gizmo, because ImGuizmo clamps its scale to ≥0.001 (`ImGuizmo.cpp:2333,2338,2350`). It can happen through the inspector field.

## 4. Merged duplicates (canonical ← others)

- **B-FOUND-01** ← E-FOUND-02. Related but distinct: A-APP-05, B-APP-04, E-APP-12 (the gizmo works in local vs world space).
- **A-FOUND-07** ← B-FOUND-03, E-FOUND-01 (the test idea is E-APP test #20).
- **A-FOUND-08**: no duplicate (B-FOUND "Rejected" mentions it).
- **B-FOUND-02**: standalone.
- **A-FOUND-01** ← C-02, D-07. Partial overlap: D-22. The silent `renderFrame`/device-lost bug is canonical in **B-RENDER-03**.
- **A-FOUND-03** ← B-FOUND-04, D-23, C convention-table row 32.
- **A-FOUND-04** ← C-10. Related, not merged: C-01 / D-27 (release builds dereference a failed `expected` after a compiled-out assert).
- **A-FOUND-02** ← D-20 (in part).
- **A-FOUND-05**: standalone.
- **A-FOUND-06** ← E-FOUND-03, D-12, D-13, D-21 (the commented ALOG block).
- **A-FOUND-09** ← E-FOUND-04. Related: B-FOUND-09 (defects in the Random API).
- **B-FOUND-05** ← E-FOUND-05, E-APP-11 (its RollingAverage bullet).
- **B-FOUND-06** ← E-APP-08, B-APP-15 (its aspect part).
- **B-FOUND-07** ← E-FOUND-06 (its clock half), D-10.
- **E-FOUND-06** (Profiler private constructor) ← E-APP-11 (its singleton part), A-FOUND-09 (its profiler part).
- **B-FOUND-14** ← C-09, E-APP-11 (its macro part), the macro bullet of E-FOUND-03.
- **A-FOUND-10** ← D-04, B-FOUND-13 (the readBinary defects).
- **A-FOUND-11** ← D-06.

## 5. Corrections to facts

1. **B-FOUND-01:** quat_cast of an improper matrix can also discard the rotation. Rot 30° about Z with scale (-1,2,1) recomposes to diag(1,2,1).
2. **E-FOUND-02:** confidence should be high. ImGuizmo cannot produce scale ≤0, but the inspector and glTF can. Then any gizmo drag (`IsUsing`) writes the mirror-less or NaN result into `Transform`.
3. **A-FOUND-07 / B-FOUND-03 / E-FOUND-01:** only `tranformationMatrix(vec3)` and `normalMatrix(vec3)` are YXZ. `forward/right/up(vec3)` match `glm::quat(r)` exactly, and `decomposeTRS(vec3)`'s `eulerAngles` inverts `quat(r)` (30,60,10 → 30,60,10). E-FOUND-01's "agree only when z == 0" is incomplete: the two orders also agree when x = y = 0.
4. **A-FOUND-04:**
   - `AGX_UNREACHABLE` has **zero uses** (excluding ai/), so the `std::unreachable()` proposal changes nothing today.
   - There are **seven** PRIVATE `${CMAKE_CURRENT_SOURCE_DIR}/..` include dirs (core, editor, math, scene, scripting, ui, utils), plus aegis-engine's PUBLIC one, not three.
   - `registry.cppm` has 18 assert lines, not 19.
5. **B-FOUND-06:** aspect cannot be 0 from the engine; frames with w==0 or h==0 are skipped (`window.cppm:37`, `engine.cppm` run loop).
6. **E-FOUND §3:** "`assertFailed` calls `assert(false)` and then `__builtin_trap()`" is wrong. The trap is never reached: the process exits 3 via abort.
7. **B-FOUND-13:** `seekg` past EOF on an `ifstream` normally succeeds; the following `read` is what fails. The outcome (16 zero bytes returned as success) is the same.
8. **D-06 (other group):** `math::normalMatrix(q,s)` is the same matrix the shaders build from `normalRow0..2` (`float3x3(row0,row1,row2)` in `indirect_draw.slang:27`; max diff 1.2e-7). It is not "transposed relative to the shader". The duplication point stands.
9. **A-FOUND-03:** the ODR hazard is latent. No `Logger` member template is instantiated today under two different configs.

## 6. Run log

- `math_check.cpp` (decompose, Euler, camera, RollingAverage, inFOV, interpolation):
  - mirror: `scale (1,1,1)`, `rotation w=0.7071 |q|=0.7071`, `recomposed [0].x = 1.0000 (original -1)`
  - mirrored and rotated: `det(original)=-2 det(recomposed)=2`; zero scale gives `rotation … nan`; positive round trip `7.45e-09`
  - Euler: r=(0,.5,.5) gives `forward(vec3) (-0.4794,0.8776,0)` vs `col1 (-0.4207,0.8776,0.2298)`. r=(30,60,10)° gives `decomposeTRS(vec3)(…) deg (42.49,54.04,14.84)`, `max|Ry*Rx*Rz - tranformationMatrix(vec3)| = 0`
  - camera: `inverseView*view` is not the identity; engine inverse gives `(4.02,-1.60,4.71)` vs correct `(-2.02,5.60,1.29)`; projection depth near/far = `0 / 1`; `aspect=0: assert condition = 1; [0][0]=inf`
  - RollingAverage<50>: `0.334 (FPS 2994)`, `216.366 (FPS 4.62)`, `once at 10 ms: 0.2`
  - inFOV: `true angle 84.29°, glm::angle 0 → inFOV=1`. Short identical vectors (len 0.5) → `inFOV=0` (angle 1.318), so the function also fails for short vectors.
  - interpolation: `sigmoid01(0)=0.00247, tanh01(0)=0.00247, fastSigmoid01(0)=0.07143`
- `math_check2.cpp`: `eulerAngles(quat(r)) = (30,60,10)`; `max|normalMatrix(q,s) - inverse(M)^T| = 1.19e-07` (the same matrix the shader builds from the uploaded rows)
- `assert_check.cpp`: includes the repo's `core/assert.h` and forces `AGX_ASSERT_X(argc>5, "forced failure message")`. Release CRT and `-fms-runtime-lib=dll_dbg -D_DEBUG` both behave the same: the stdout file is empty, and stderr holds only the CRT assert line from `assert.h` line 44. dll_dbg exits with 3 (abort). No dialog appears.
- `alog_check.cpp`: `instance ptr before init: 0000000000000000`, then `Segmentation fault` (exit 139) on `ALOG::warn`.
- `clock_check.cpp`:
  - MSVC STL: `is_steady=1 same_as_steady=1`
  - libstdc++ (WinLibs g++ 15.2): `is_steady=0 same_as_system=1`
- Grep (src, tests, examples; excluding src/aegis/ai):
  - 0 uses: `math::lerp`, `percentage`, `sigmoid01`, `tanh01`, `PerlinNoise1D`, `normalMatrix` (both overloads), `decomposeTRS(vec3)`, `tranformationMatrix(vec3)`, `Color`, `hashCombine`, `readBinary`, `ScopeTimer`, `elapsedNanos/Micros/Seconds`, `AGX_PROFILE_*`, `AGX_UNREACHABLE`, `Random::seed/normal*/uniformInt`, `inverseViewMatrix` readers.
  - Tests only: `inFOV`, `forward/right/up(vec3)`.
- Not done: no Linux run (WSL2 is unavailable on this machine). The glibc abort-no-flush behaviour rests on the glibc 2.27 release notes.
