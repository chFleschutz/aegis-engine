# Findings e: FOUNDATION, testability lens

Scope: `src/aegis/math`, `src/aegis/utils`, `core/assert.h`, `core/logging.cppm`, `core/profiler.cppm`; consumers `tests/math`, `tests/CMakeLists.txt`. Read-only; nothing built or run. I re-checked the map facts against the code. One was wrong (see E-FOUND-01).

## 1. What is tested today (`tests/math/math.test.cpp`, 5 cases)

| Function | Cases | Depth |
|---|---|---|
| `forward/right/up(vec3)` | zero rotation gives the world axes; orthonormal at (0.3,-0.7,1.1) | Shallow. Every Euler order is orthonormal, so this cannot catch a wrong convention (E-FOUND-01). |
| `forward/right/up(quat)` | identity quat gives the world axes | Trivial. Never checked against a non-identity rotation or against the matrix. |
| `inFOV` | target dead ahead is in; target behind is out | No boundary case. Its only other caller is in unbuilt `ai/`. |
| everything else in math, all of utils, `Profiler` | none | none |

## 2. Untested pure logic: test it or delete it

**Test (has real callers):**
- `tranformationMatrix(quat)` and `decomposeTRS(quat)`: `Transform::matrix()` and the editor gizmo (`editor_layer.cppm:106`).
- `forward/right/up(quat)`: `Transform`, `CameraSystem`.
- `utils::RollingAverage` and `utils::Timer`: reached only through `core::Profiler`.

**Delete instead of testing (no caller outside unbuilt `ai/`):**
- all `vec3`-Euler overloads: `forward/right/up`, `tranformationMatrix`, `normalMatrix`, `decomposeTRS`
- `normalMatrix(quat)`; render_graph computes its own normal matrix
- `inFOV` and its 2 tests
- all of `:interpolation`
- `:perlin_noise`: `PerlinNoise1D` sits in a non-exported namespace, so no importer can see it
- `Random::seed/normalFloat/normalFloatRange/uniformInt/normalInt/normalIntRange`
- `utils::Color` and `utils::hashCombine`: non-exported namespaces, so invisible
- the empty primary module `aegis.utils`
- `utils::file::readBinary` (both overloads)
- `ScopeTimer`, `Timer::elapsed<>` and `elapsedNanos/Micros/Seconds`
- `RollingAverage::operator double` and `operator+=`
- the `AGX_PROFILE_SCOPE/FUNCTION` macros (E-FOUND-03)

## 3. Blockers and 4. Smallest seam per blocker

| Blocker | Real? | Smallest seam |
|---|---|---|
| `math::Random`: a process-global `mt19937` seeded from `random_device` | Only for code that calls it: `PerlinNoise1D` and `Color::random` (both dead), plus the examples. | Delete it (E-FOUND-04). No seam is needed once its engine consumers are gone. |
| `Profiler::instance()` singleton with a private ctor | Yes. State leaks across test cases, and `ScopeProfiler` hard-wires `instance()`. | Make the ctor public and keep `instance()` for the engine. No interface, no injection (E-FOUND-06). |
| Assert-only preconditions abort | `assertFailed` calls `assert(false)` and then `__builtin_trap()`; release builds compile it out. CI (`build.yml`, config matrix debug and release) runs `ctest -LE device` in both. A test that reaches an assert-then-fallback such as `Color::fromHex` passes in release and dies in debug. | In foundation every such assert sits in dead code (`perlin_noise.cppm`, `color.cppm`). Deleting that code removes the blocker. |
| `aegis.utils` primary exports nothing | Not a blocker. The submodules are dotted named modules, each its own primary interface, so a test can `import aegis.utils.rolling_average;` directly. The README's re-export rule applies only to partitions. `Color` and `hashCombine` are invisible because their namespace lacks `export`, not because of re-export. | None. |
| math and utils `#include "core/assert.h"` without linking core | Not a link blocker. `assert.h` holds macros and one `inline` function, no core symbol, so `aegis-tests-utils` linking only `aegis-utils` would link. The include dir is PRIVATE, but only the module units themselves need it. Inference (medium confidence): a test importing `aegis.utils.color` could fail to find the `aegis.math` BMI, because `aegis-utils` links math PRIVATE. This is moot once `color` is deleted. | Delete the two includers (E-FOUND-03). Then drop the "TODO" include dirs and the unused `Aegis::Log` links from both targets. |
| File IO without an injectable path | `file::readBinary` takes a path, so a temp file would make it testable, but nothing calls it. | Delete it. |

## Findings

### E-FOUND-01: Euler overloads mix two rotation orders; all of them are dead
- **Where:** `math/math.cppm`: `forward/right/up(const vec3&)`, `tranformationMatrix(..., const vec3&, ...)`, `normalMatrix(const vec3&, ...)`, `decomposeTRS(..., vec3& rotation, ...)`
- **Observed:**
  - `right(vec3)` returns `{ cy * cz, cy * sz, -sy }`, which is column 0 of Rz·Ry·Rx (glm's `eulerAngles` order).
  - `tranformationMatrix(vec3)` builds column 2 as `scale.z*(c2*s1), scale.z*(-s2), scale.z*(c1*c2)`, with `c1/s1` from `rotation.y` and `c2/s2` from `rotation.x`. That is Ry·Rx·Rz.
  - `decomposeTRS(vec3)` returns `glm::eulerAngles(quat_cast(R))`, i.e. Rz·Ry·Rx.
  - `Transform::rotation` and `GlobalTransform::rotation` are `glm::quat`, so `components.cppm:26-42` and `editor_layer.cppm:106` resolve to the **quat** overloads. map/foundation.md §1 says the editor uses the vec3 `decomposeTRS`; it does not.
  - No vec3 overload has a caller outside `ai/`.
- **Inference:** Take rotation `(0, 0.5, 0.5)`. `forward(r)` differs from column 1 of `tranformationMatrix({}, r, {1,1,1})`, and `decomposeTRS(tranformationMatrix(..., r, ...))` does not return `r`, because Ry·Rz ≠ Rz·Ry. The two orders agree only when `rotation.z == 0`. The orthonormality test passes under both orders, so it cannot catch this. It is a latent bug waiting for the next caller, for example an Euler field in the inspector.
- **Severity:** design problem (latent bug) · **Confidence:** high (derived by hand, not executed)
- **Affects:** any feature that edits rotation as Euler angles; the round-trip test
- **Proposal:** Delete the four vec3 overloads; quaternions are the only representation in use. In the same commit, rename `tranformationMatrix` to `transformationMatrix` (2 call sites). A later Euler UI converts through glm's `quat(vec3)` and `eulerAngles`, which share one convention. Size S.

### E-FOUND-02: `decomposeTRS` assumes positive, nonzero scale
- **Where:** `math/math.cppm`: `decomposeTRS(..., quat&, ...)`
- **Observed:** Scale is the `glm::length` of each column, so it is always ≥ 0. The code then does `rotationMat[i] /= scale.i` with no check.
- **Inference:** A mirrored matrix (negative determinant) comes back as positive scale plus an improper rotation, and `quat_cast` turns that into a wrong quaternion. Zero scale divides by zero and writes NaN into `Transform` through the editor gizmo. I have not verified whether ImGuizmo can produce zero or negative scale.
- **Severity:** bug (edge case) · **Confidence:** low (no reproduced path)
- **Affects:** the editor gizmo; any future import or parenting path that decomposes matrices
- **Proposal:** Pin the contract with a test that positive scale round-trips. Add an axis flip when `determinant < 0` only if a caller needs mirroring. Size S.

### E-FOUND-03: About two thirds of the foundation is unreachable or uncalled
- **Where:** the list in section 2, plus `core/profiler.cppm`, where `#define AGX_PROFILE_SCOPE` comes after `export module`
- **Observed:**
  - `perlin_noise.cppm` and `color.cppm` declare `namespace aegis::...` without `export`.
  - `utils.cppm` exports nothing.
  - The profiler macros are defined inside a module interface, and macros never cross `import`. No file uses `AGX_PROFILE_*`; callers write `ScopeProfiler` directly (`engine.cppm:154,305`, `scene.cppm:66`).
  - These two files are the only `core/assert.h` includers in math and utils.
- **Inference:** Dead code costs reading time and invites tests that protect nothing. Deleting it also removes the U3 header edge (math/utils → core), both "TODO remove" include dirs and the unused `Aegis::Log` links.
- **Severity:** design problem · **Confidence:** high
- **Affects:** every test someone would otherwise write for this code; the layering of L0
- **Proposal:** One deletion commit covering section 2. What remains: math is the glm whitelist, `world` and the quat helpers; utils is `RollingAverage` and `Timer`. Size S.

### E-FOUND-04: `math::Random` is global, unseeded state with no engine consumer
- **Where:** `math/random.cppm`: `Random::s_generator` (`inline static std::mt19937{ std::random_device{}() }`)
- **Observed:** After E-FOUND-03, its only callers are `examples/simple-scene` and `examples/eval-scene`. eval-scene passes `Random::generator()` into its own distributions.
- **Inference:** A global RNG in L0 would make any future engine feature that uses it (particles, procedural placement) nondeterministic in tests and unsafe across threads.
- **Severity:** design problem · **Confidence:** medium
- **Affects:** future procedural or simulation features and their tests
- **Proposal:** Delete `Random` and give the examples a local `std::mt19937`. A future feature takes a `std::mt19937&`, or a seed in its `Desc`, which is also its test seam. Size S.

### E-FOUND-05: `RollingAverage` under-reports until the window fills
- **Where:** `utils/rolling_average.cppm`: `RollingAverage::average`
- **Observed:** `return m_sum / N;` over a zero-filled window. The doc comment says "Average is dirty for the first N-1 values".
- **Inference:** `Profiler` uses N = 50. A scope sampled once at 10 ms shows 0.2 ms in the profiler panel. A scope that runs fewer than 50 times never shows its real average.
- **Severity:** polish · **Confidence:** high
- **Affects:** the profiler panel; RollingAverage tests
- **Proposal:** Keep a sample count and divide by `min(count, N)`. Write the test first (it fails today). Size S.

### E-FOUND-06: `Timer` runs on a non-monotonic clock on Linux; `Profiler` cannot be instantiated
- **Where:** `utils/timer.cppm`: `Timer` (uses `std::chrono::high_resolution_clock`); `core/profiler.cppm`: `Profiler() = default;` under `private:`
- **Observed:**
  - libstdc++ aliases `high_resolution_clock` to `system_clock`.
  - `engine.cppm` and `frame_graph.cpp` use `steady_clock` directly.
  - `Profiler` can only be reached through `instance()`.
- **Inference:** On Linux, a wall-clock adjustment during a frame gives a negative or huge profiler sample. Tests of `Profiler` would share one instance across cases.
- **Severity:** polish · **Confidence:** medium (the clock alias is libstdc++ behaviour; not observed in a run)
- **Affects:** profiler correctness on Linux; `core::Profiler` tests
- **Proposal:** Switch `Timer` to `steady_clock` and make `Profiler`'s ctor public. These are two one-line changes and add no abstraction. Size S.

## 5. Candidate tests (ranked)

| # | TEST_SUITE / TEST_CASE | Unit | Seam | Value | Effort |
|---|---|---|---|---|---|
| 1 | `math::transformationMatrix` / "round-trips through decomposeTRS for a rotated, non-uniformly scaled transform" (compare translation, scale, and `abs(dot(q, q'))≈1`) | math | none | High: guards the editor gizmo path | S |
| 2 | `math::transformationMatrix` / "has forward, right and up of its rotation as scaled columns" (quat) | math | none | High: catches the bug class of E-FOUND-01 | S |
| 3 | `utils::RollingAverage` / "averages only the most recent N samples" (wrap-around) | utils (new exe) | none | Medium: Profiler's only logic | S |
| 4 | `utils::RollingAverage` / "reports the mean of the samples so far before the window fills" | utils | none | Medium: fails today (E-FOUND-05) | S |
| 5 | `utils::RollingAverage` / "returns the most recent sample as last" (including after wrap) | utils | none | Low-medium: backs `Profiler::lastTime` | S |
| 6 | `math::decomposeTRS` / "recovers translation and per-axis scale" | math | none | Medium | S |
| 7 | `math basis vectors` / "match the rotated world axes for a quarter turn about Z" (quat; replaces the trivial identity case) | math | none | Medium: the identity test cannot fail | S |
| 8 | `core::Profiler` / "reports zero for a name never recorded" and "averages recorded times per name" | core (new exe, links imguizmo) | public ctor | Low: thin map wrapper | S |
| 9 | Bug-report test before deletion: `math basis vectors` / "Euler forward matches the Euler matrix's second column" with `(0, 0.5, 0.5)` | math | none | Proves E-FOUND-01, then goes away with the code | S |

## 6. Strategy view (tests/README.md)

- **Working well:** `aegis_add_test` costs one line per module. A utils test target is `aegis_add_test(utils SOURCES rolling_average.test.cpp LIBS aegis-utils)` plus one `add_subdirectory`. `CHECK` with `doctest::Approx` suits math well.
- **Friction: the re-export rule is stated too narrowly.** It covers partitions, but dotted modules (`utils`, `core`, `scene`) are separate primaries that tests import directly. Neither case is the trap seen here: `export import :perlin_noise` compiles and exports *nothing*, because the partition's namespace lacks `export`, and no error appears anywhere. The README should say both.
- **Friction: suite naming has drifted from the README.**
  - README: "namespace-qualified type, one block per file".
  - Practice: per-function suites without `aegis::` (`rhi::calcMipLevels`, `render_graph::sortPasses`), often several per file (`image.test.cpp` has 5).
  - math uses `aegis::math …` and a topic name instead.
  - Codify the practice: `<module>::<Type or function>`, no `aegis::`, several suites per file allowed.
- **Friction: assert tests depend on the build type.** CI runs `ctest -LE device` in both debug and release, and anything that reaches an `AGX_ASSERT` traps in one and passes in the other. "Test around asserts" is the right rule. Add: never test an assert-then-fallback; either make the fallback the contract or remove it.
- **The scope statement is stale.** The README says tests cover "the new RHI stack", yet `aegis-tests-math` tests legacy code. The scope should read "pure logic of any module".
- **glm whitelist:** test TUs see only the glm names that `math.cppm` re-exports, because `aegis-math` links glm PRIVATE. Tests 1-7 need only `dot`, `length`, `quat` and `mat4` indexing, all of which are exported. A test that needs `quat_cast` or `epsilonEqual` must extend the whitelist first.
