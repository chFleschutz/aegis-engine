# Findings b (bugs): FOUNDATION

Scope: `src/aegis/math`, `src/aegis/utils`, `core/assert.h`, `core/logging.cppm`, `core/profiler.cppm` and their consumers. Lens: bugs, math worked through with concrete values. Read-only analysis, no builds.

**Correction to `map/foundation.md`** (re-checked): `aegis::Transform::rotation` is `glm::quat` (`scene/components.cppm` l.23). So `editor_layer.cppm` l.106 resolves to `decomposeTRS(const mat4&, vec3&, quat&, vec3&)`, not the vec3 overload. Every vec3-Euler overload in `math.cppm` (`tranformationMatrix`, `normalMatrix`, `decomposeTRS`) has no caller, and `forward/right/up(vec3)` are called only from `tests/math`.

Order: by importance.

### B-FOUND-01: decomposeTRS drops mirroring (negative scale) and returns a non-unit quaternion
- **Where:** `src/aegis/math/math.cppm` `decomposeTRS(const glm::mat4&, glm::vec3&, glm::quat&, glm::vec3&)`; caller `src/aegis/editor/editor_layer.cppm` `EditorLayer::drawGizmo` l.106
- **Observed:** `scale.x = glm::length(glm::vec3(matrix[0]))` (always ≥ 0); `rotationMat[i] /= scale[i]`; `rotation = glm::quat_cast(rotationMat)`. There is no determinant check, no zero-scale check and no normalize.
- **Inference:** Take `Transform{ loc 0, rot identity, scale (−1,1,1) }`, for example a mirrored glTF node (`assets/gltf_loader.cpp` l.213 copies node TRS as-is).
  - `matrix()` has columns (−1,0,0), (0,1,0), (0,0,1).
  - Decompose gives scale (1,1,1) and rotationMat = diag(−1,1,1), which is improper. In `quat_cast` the trace terms are fourW = 1, fourX = −3, fourY = fourZ = 1, so the W branch wins: w = √2·0.5 = 0.707 and x = y = z = 0. That is a quaternion of length 0.707.
  - Recomposing gives `mat3_cast` = identity, so the mirror is lost.
  - The editor runs this every frame `ImGuizmo::IsUsing()` is true. Merely *translating* a mirrored entity un-mirrors it and writes a non-unit quat into `Transform::rotation`.
  - A scale axis dragged to 0 divides by 0, which puts NaN into the rotation.
- **Severity:** bug
- **Confidence:** medium. The math is certain. I did not verify whether the shipped assets carry negative scale, or whether ImGuizmo lets scale reach 0.
- **Affects:** editor gizmo, and any future serialization, animation or "apply transform" feature that decomposes a matrix.
- **Verify by:** doctest: `glm::vec3 t, s; glm::quat q; math::decomposeTRS(math::tranformationMatrix({}, glm::quat{1,0,0,0}, {-1,1,1}), t, q, s); CHECK(math::tranformationMatrix(t, q, s)[0].x == Approx(-1.0f)); CHECK(glm::length(q) == Approx(1.0f));`. Both checks fail (1.0 and 0.707).
- **Fix sketch:** If `determinant(mat3(matrix)) < 0`, negate one scale axis and its column before `quat_cast`, then normalize the quat. On zero scale, keep the previous rotation. Pin the behavior with the round-trip test.

### B-FOUND-02: AGX_ASSERT failure diagnostics are lost when output is piped; AGX_DEBUG_BREAK is unreachable
- **Where:** `src/aegis/core/assert.h` `aegis::assertFailed`, macros `AGX_ASSERT`, `AGX_ASSERT_X`, `AGX_UNREACHABLE`
- **Observed:** `std::cout << std::format("Assertion failed: '{}'\n\tFile: {}, ...", ...); assert(false && "Assertion failed!");`. The macro then evaluates `AGX_DEBUG_BREAK()` after `assertFailed` returns. `AGX_ASSERTS_ENABLED` is tied to `!NDEBUG`, so the standard `assert` is always active whenever AGX asserts are.
- **Inference:**
  - `std::cout` is synced with C `stdout`, which is fully buffered when it is a pipe or file (ctest, CI, the capture script in agent-notes). `abort()`, called from `assert`, does not flush stdio: glibc ≥ 2.27 and the UCRT both skip the flush.
  - Example: a debug `aegis-test --scene` run under ctest hits `Registry::get` with a missing component. The captured log shows only `Assertion failed: false && "Assertion failed!", file …/core/assert.h, line 44`. The expression, the call site's file/line/function and the message are all lost.
  - Because `assert` aborts inside `assertFailed`, `AGX_DEBUG_BREAK()` (`__debugbreak`/`__builtin_trap`) never runs, so the debugger stops in the CRT instead of at the call site.
- **Severity:** bug (diagnosability of every legacy-layer failure)
- **Confidence:** medium. Buffering and abort semantics are standard. I did not run it.
- **Affects:** every AGX_ASSERT user (engine, scene registry, editor, scripting, math/utils); CI logs; the agent-notes capture workflow.
- **Verify by:** code reading, or a manual run with a forced assert piped through `| cat`.
- **Fix sketch:** Write to `std::cerr` (unbuffered) or `std::fflush`. Drop the inner `assert(false)` and let the macro's `AGX_DEBUG_BREAK()` (or `std::abort()`) end the process at the call site.

### B-FOUND-03: The vec3 Euler overloads use two different rotation orders
- **Where:** `src/aegis/math/math.cppm` `tranformationMatrix(vec3, vec3, vec3)` and `normalMatrix(vec3, vec3)`, versus `forward/right/up(const glm::vec3&)` and `decomposeTRS(..., glm::vec3& rotation, ...)`
- **Observed:**
  - `tranformationMatrix(vec3)` uses `c1/s1 = rotation.y`, `c2/s2 = rotation.x`, `c3/s3 = rotation.z`, with column 0 `{c1c3+s1s2s3, c2s3, c1s2s3−c3s1}`. Multiplied out, that is **R = Ry·Rx·Rz** (YXZ). `normalMatrix(vec3)` uses the same order.
  - `forward(vec3)` = `{sx·sy·cz − cx·sz, sx·sy·sz + cx·cz, sx·cy}` = column 1 of **Rz·Ry·Rx** (ZYX, glm's `quat(vec3)` convention). `right`/`up` are columns 0 and 2 of the same matrix.
  - `decomposeTRS(vec3)` returns `glm::eulerAngles(quat_cast(R))`, which inverts Rz·Ry·Rx.
- **Inference:** The same Euler vector means two different rotations. With r = radians(30°, 60°, 10°):
  - `decomposeTRS(tranformationMatrix(0, r, 1))` gives (42.49°, 54.04°, 14.84°), not (30°, 60°, 10°).
  - `forward(r)` = (0.276, 0.928, 0.250), but column 1 of `tranformationMatrix(0, r, 1)` = (0.340, 0.853, 0.397).
  - `tranformationMatrix(vec3 r)` ≠ `tranformationMatrix(glm::quat(r))`; the quat path is self-consistent.
  - Nothing live is broken, because all engine transforms are quats. But the tests (`tests/math` basis cases) certify the vec3 functions, and the first Euler feature (inspector fields, scene files, animation) that reaches for them gets a silent mismatch. Numbers come from a script that reproduces both formulas.
- **Severity:** polish (dead code; it turns into a bug at the first caller)
- **Confidence:** high
- **Affects:** future Euler UI and serialization; `tests/math` covers the unused convention while the live quat transforms have no test.
- **Verify by:** `glm::vec3 r = glm::radians(glm::vec3{30,60,10}); glm::vec3 t, rr, s; math::decomposeTRS(math::tranformationMatrix({}, r, glm::vec3{1}), t, rr, s); CHECK(rr.x == Approx(r.x));` fails (0.742 vs 0.524). Also `CHECK(glm::vec3(math::tranformationMatrix({}, r, glm::vec3{1})[1]).x == Approx(math::forward(r).x));` fails.
- **Fix sketch:** Delete the vec3 overloads of `tranformationMatrix`, `normalMatrix` and `decomposeTRS`. Either drop `forward/right/up(vec3)` or define them as `forward(glm::quat(r))`, so only glm's ZYX convention remains (the kinematic controller and the glTF loader already use it). Point the tests at the quat overloads and add the decompose round-trip test.

### B-FOUND-04: Logging config macros apply to one TU only, and `ALOG::*` before `Logging` dereferences null
- **Where:** `src/aegis/core/logging.cppm` (global module fragment), `aegis::Logging::Logging`; `modules/aegis-log/include/aegis-log/helper/singleton.h` `Singleton::instance`; `src/aegis/engine.cppm` (`#include <aegis-log/log.h>` with no defines)
- **Observed:**
  - `logging.cppm` defines `AEGIS_LOG_DISABLE_DEBUG/TRACE` (under NDEBUG) and `AEGIS_LOG_FATAL_NO_ASYNC` before including `log.h`. `config.h` turns these into per-TU `constexpr bool`s.
  - `engine.cppm`, the only other `ALOG` user, includes `log.h` with none of them.
  - `Singleton::instance()` is `return *s_instance;`, and `s_instance` is set only when `Logging()` runs `ALOG::init`. That happens in `Engine::m_logging`.
- **Inference:**
  - The intended release config ("debug/trace compiled out, fatal synchronous") governs only `logging.cppm`'s own `ALOG::info`. An `ALOG::debug` added in any other module still formats and prints in release. If two TUs instantiate the same `Logger<0>::debug<Args...>` with different `DEBUG_ENABLED` values, that is an ODR violation.
  - Any logging call outside a constructed `Engine` dereferences null. A doctest case exercising a pure function that does `ALOG::warn(...)` segfaults. The same goes for `aegis-test` and the new-RHI modules. So adding logging to unit-testable code is unsafe today.
- **Severity:** design problem
- **Confidence:** high (mechanism); there is no failing call today.
- **Affects:** every future module that wants logging; unit testability of anything that logs; release log volume.
- **Verify by:** a doctest in any test exe calling `ALOG::warn("x")` crashes. Code reading for the macro split.
- **Fix sketch:** Move the defines to `target_compile_definitions(Aegis::Log INTERFACE/PUBLIC ...)`. Make logging safe without init: fall back to a default stderr sink when `s_instance` is null, or have `aegis.core.logging` export wrappers that check.

### B-FOUND-05: RollingAverage::average divides by N before N samples; Profiler panel shows wrong FPS and divides by 0
- **Where:** `src/aegis/utils/rolling_average.cppm` `RollingAverage::average`; consumers `core/profiler.cppm` `Profiler::time`, `editor/panels/profiler_panel.cppm` `ProfilerPanel::draw`; producer `engine.cppm` `Engine::run` (`ScopeProfiler frameTime("Frame Time")`)
- **Observed:** `return m_sum / N;` (doc: "Average is dirty for the first N-1 values"). The panel does `ImGui::Text("FPS: %.1f", 1000.0 / frameTime)` and `rolling.average() / frameTime * 100.0`. `Profiler::time` returns `0.0` for an unknown name.
- **Inference:**
  - Frame 1: the panel draws inside the frame, before the first "Frame Time" sample exists. So frameTime = 0 and the panel shows FPS inf and inf/NaN percentages.
  - After 1 sample of 16.7 ms: average = 16.7/50 = 0.334 ms, so FPS shows 2994. The value only becomes correct after 50 frames.
  - The minimized path (`continue` after `waitEvents`) also records the whole wait. 10 s minimized gives average (49·16.7 + 10000)/50 ≈ 216 ms, so FPS shows 4.6 for the next 50 frames.
- **Severity:** polish (display only)
- **Confidence:** high
- **Affects:** profiler panel; any future consumer of `RollingAverage` (frame pacing, adaptive quality).
- **Verify by:** `utils::RollingAverage<50> a; a.add(16.7); CHECK(a.average() == Approx(16.7));` fails (0.334). There is no `tests/utils` yet; this would be its first case.
- **Fix sketch:** Track `m_count = min(m_count+1, N)` and divide by it, returning 0 when empty. Have the panel guard `frameTime > 0`.

### B-FOUND-06: CameraSystem's aspect assert can never fire for aspect 0
- **Where:** `src/aegis/scene/systems/camera_system.cppm` `CameraSystem::calcPerspectiveProjection` (AGX_ASSERT consumer; scene group)
- **Observed:** `AGX_ASSERT(std::abs(camera.aspect - std::numeric_limits<float>::epsilon()) > 0.0f);`
- **Inference:** For aspect = 0: |0 − 1.19e−7| = 1.19e−7 > 0, so the assert passes. Then `perspective[0][0] = 1/(0·tan)` = +inf and the projection is inf/NaN. The assert fires only for aspect exactly equal to `FLT_EPSILON`. Aspect comes from `scene_update_pass.cpp` l.30 (`extent.x / extent.y`). A 0-width or 0-height extent (and 0/0 = NaN, which compares false and so does fire) is the case the guard was meant to catch.
- **Severity:** bug (guard is ineffective; low practical impact because minimized frames are skipped)
- **Confidence:** high
- **Affects:** camera projection robustness; a later extraction of projection math into a testable function.
- **Verify by:** `CHECK(std::abs(0.0f - std::numeric_limits<float>::epsilon()) > 0.0f);` holds, which shows the guard accepts 0.
- **Fix sketch:** `AGX_ASSERT(std::abs(camera.aspect) > std::numeric_limits<float>::epsilon());`. Better, move the projection into `aegis::math` as a pure function with a test.

### B-FOUND-07: Timer uses high_resolution_clock, which is the wall clock on libstdc++
- **Where:** `src/aegis/utils/timer.cppm` `Timer` (used by `core/profiler.cppm` `ScopeProfiler`)
- **Observed:** `std::chrono::high_resolution_clock::now()` everywhere. `engine.cppm` and `render_graph/frame_graph.cpp` use `steady_clock` directly.
- **Inference:** On Linux (libstdc++), `high_resolution_clock` is `system_clock`. An NTP step of −0.5 s during a frame produces a −500 ms "Frame Time" sample, which pulls the 50-frame average down by 10 ms (FPS shows nonsense for 50 frames). A forward step does the opposite. On MSVC STL it is `steady_clock`, so the behavior differs per platform.
- **Severity:** polish
- **Confidence:** medium (needs a clock step to show)
- **Affects:** profiler; any future timing-based feature reusing `Timer`.
- **Verify by:** `static_assert(decltype(...)::is_steady)` would fail on libstdc++: `CHECK(std::chrono::high_resolution_clock::is_steady);` fails on Linux CI.
- **Fix sketch:** Switch to `steady_clock` and make `Timer` the one clock helper that `engine.cppm` and `frame_graph.cpp` use.

### B-FOUND-08: inFOV is wrong for non-normalized inputs
- **Where:** `src/aegis/math/math.cppm` `inFOV`
- **Observed:** `return glm::angle(viewDirection, targetDirection) < 0.5f * fov;`. `glm::angle` is `acos(clamp(dot(x, y), -1, 1))` (`external/glm/glm/gtx/vector_angle.inl` l.20) and assumes unit vectors.
- **Inference:** view (0,1,0), target (10,1,0) (true angle 84.3°), fov 10°: dot = 1, clamp gives 1, acos gives 0, so it returns true. Natural callers pass `targetPos − eyePos`, which is unnormalized. The only non-test caller is in unbuilt `ai/`. The tests use unit vectors only.
- **Severity:** polish (no live caller)
- **Confidence:** high
- **Affects:** future AI, culling or visibility features.
- **Verify by:** `CHECK_FALSE(math::inFOV({0,1,0}, {10,1,0}, glm::radians(10.0f)));` fails.
- **Fix sketch:** Normalize both inputs inside, or compare `dot(normalize(v), normalize(t)) > cos(fov/2)`.

### B-FOUND-09: Random: documented bounds are wrong, normalFloatRange breaks for min == max, and the global state defeats deterministic tests
- **Where:** `src/aegis/math/random.cppm` `Random::uniformFloat`, `normalFloatRange`, `normalIntRange`, `s_generator`
- **Observed:**
  - The doc says `uniformFloat` returns "[min, max]", but `std::uniform_real_distribution` is [min, max).
  - `normalFloatRange` uses stddev `(max - min) / 6.0f`.
  - `normalIntRange` computes `(min + max) / 2.0f` in `int`.
  - `inline static std::mt19937 s_generator{ std::random_device{}() };`, and `generator()` returns a mutable reference.
- **Inference:**
  - `normalFloatRange(1, 1)` builds `normal_distribution(1, 0)`, which violates the stddev > 0 precondition (UB; libstdc++ asserts with `_GLIBCXX_ASSERTIONS`).
  - `normalIntRange(INT_MAX-1, INT_MAX)` overflows `min + max` (signed overflow UB).
  - The process-global, randomly seeded state makes the examples (eval-scene, simple-scene) different every run. That blocks the reference-image idea in agent-notes. Two test cases that each `seed()` share state across the doctest run order.
- **Severity:** polish (no engine callers; examples only)
- **Confidence:** high
- **Affects:** procedural content, reference-image tests, any test touching `Random`.
- **Verify by:** code reading. For determinism: `Random::seed(1); auto a = Random::uniformFloat(); Random::seed(1); CHECK(Random::uniformFloat() == a);` passes, so seeding itself works; the problem is the shared global.
- **Fix sketch:** Make `Random` a value type owning its `mt19937` (seed in the constructor), passed to callers. Fix the doc to [min, max). Guard `min == max`. Compute the midpoint in `double` or `int64`.

### B-FOUND-10: PerlinNoise1D: unbounded loop for negative x in release, NaN for rank ≤ 0, an extra octave, and seed-order dependence
- **Where:** `src/aegis/math/perlin_noise.cppm` `PerlinNoise1D::noise`, `Octave::Octave`. The class sits in a non-exported namespace, so it is dead code.
- **Observed:**
  - `int xIntervalIndex = static_cast<int>(x / m_bandwidth);` then `while (m_intervals.size() - 1 < xIntervalIndex) addInterval();`
  - `while (xInterval.octaves.size() <= rank) addOctave(...)`
  - `return noiseValue / maxAmplitude;`
- **Inference:**
  - Release, x = −2.5: index −2 converts to `size_t` 2^64−2, so the loop calls `addInterval` until `bad_alloc`. In debug, the AGX_ASSERT catches it.
  - rank = 0: no octave has rank ≤ 0, so maxAmplitude = 0 and the result is 0/0 = NaN.
  - rank r builds r+1 octaves. Octave r+1 holds 2^r+1 values (the largest allocation, never read), and the insert loop is O(n²).
  - The doc promises "seed BEFORE creating" gives the same result, but octaves are drawn lazily per query. After `seed(1)`, querying `noise(0.5,2)` then `noise(1.5,1)` draws in a different order than the reverse, so the same x gives different values.
- **Severity:** polish (dead)
- **Confidence:** high
- **Affects:** nothing today; it is a trap if exported.
- **Verify by:** code reading (unreachable from tests because the namespace is not exported).
- **Fix sketch:** Delete it until needed. If kept, use a hash-based gradient noise (stateless, seed parameter), which is pure and testable.

### B-FOUND-11: Interpolation curves labelled "[0, 1]" do not reach 0 and 1
- **Where:** `src/aegis/math/interpolation.cppm` `sigmoid01`, `fastSigmoid01`, `tanh01`, `percentage`
- **Observed:** `sigmoid01 = 1/(1+exp(-12x+6))`, `fastSigmoid01: x=12x-6; x/(1+|x|)/2+0.5`, `tanh01 = tanh(6x-3)/2+0.5`.
- **Inference:**
  - sigmoid01(0) = 0.00247 and sigmoid01(1) = 0.99753.
  - tanh01(0) = 0.00247.
  - fastSigmoid01(0) = 0.0714 and fastSigmoid01(1) = 0.9286, so the "fast approximation" is off by 7% at the ends.
  - Used as easing, an animation never reaches its target. In Perlin, `tanh01` leaves a seam jump of up to 0.25% of each neighbour difference at every chunk boundary.
  - `percentage(v, a, a)` = 0/0 = NaN.
  - None of these has a caller outside the dead Perlin code.
- **Severity:** polish
- **Confidence:** high
- **Affects:** future easing and animation.
- **Verify by:** `CHECK(math::fastSigmoid01(0.0f) == Approx(0.0f));` fails (0.0714).
- **Fix sketch:** Delete them, or renormalize: `(f(x) − f(0)) / (f(1) − f(0))`. Use `std::lerp` and drop `math::lerp`.

### B-FOUND-12: Color::fromHex accepts malformed input silently and rejects valid short and alpha forms
- **Where:** `src/aegis/utils/color.cppm` `Color::fromHex` (non-exported namespace, no callers)
- **Observed:** It checks only `size() == 7 && [0] == '#'`, then `stream >> std::hex >> rgbValue;` with no failure check.
- **Inference:**
  - "#12GGGG" passes the check and parses "12", so rgb = 0x000012 and the result is (0, 0, 0.071, 1): no assert, no white fallback.
  - "#GGGGGG" fails extraction, sets 0 (C++11), and returns black.
  - "#FFF" and "#FF000080" assert in debug and return white in release. That is the legacy "assert + fallback" mix.
- **Severity:** polish (dead)
- **Confidence:** high
- **Affects:** future theme/UI color config.
- **Verify by:** (after exporting) `CHECK(utils::Color::fromHex("#12GGGG") == glm::vec4{1.0f});` fails.
- **Fix sketch:** Parse with `std::from_chars`, require full consumption, support 3/6/8 digits, and return `std::optional<glm::vec4>`.

### B-FOUND-13: file::readBinary(path, size, offset) reports a short or failed read as success
- **Where:** `src/aegis/utils/file.cppm` `readBinary(const path&, size_t, size_t)` (no callers)
- **Observed:** `file.seekg(offset); std::vector<char> buffer(size); file.read(buffer.data(), size); return buffer;` with no `gcount()` or state check. The one-argument overload returns `{}` for both "missing" and "empty".
- **Inference:**
  - 10-byte file, `readBinary(p, 16)`: returns 16 bytes, the last 6 zero, which looks like success.
  - `offset = 20`: `seekg` fails, `read` does nothing, and it returns 16 zero bytes.
  - A shader or asset loader built on it would upload truncated or zero data instead of failing. That is probably why `render_graph/shader.cpp` and `test/main.cpp` have their own readers.
- **Severity:** polish (dead)
- **Confidence:** high
- **Affects:** shared file I/O for shaders and assets (currently duplicated).
- **Verify by:** write a 10-byte temp file; `CHECK(utils::file::readBinary(p, 16).size() == 10);` fails (16).
- **Fix sketch:** Return `std::expected<std::vector<std::byte>, Error>`, resize to `gcount()` and fail on a short read. Then replace the two ad-hoc readers.

### B-FOUND-14: AGX_PROFILE_SCOPE/FUNCTION cannot be used and would collide if they could
- **Where:** `src/aegis/core/profiler.cppm` l.12-13
- **Observed:** `#define AGX_PROFILE_SCOPE(name) aegis::ScopeProfiler profiler##__LINE__(name)` is defined inside a module interface unit.
- **Inference:** Macros never cross a module boundary, so no importer can use them. Even inside the unit, `##` suppresses expansion of `__LINE__`, so two uses in one scope both declare `profiler__LINE__` and redeclaration fails to compile. The map notes them as unused; this records that they are also broken.
- **Severity:** polish
- **Confidence:** high
- **Affects:** adding profiling scopes (callers write `ScopeProfiler x("...")` by hand today).
- **Verify by:** code reading.
- **Fix sketch:** Delete the macros, or move them to a small header with a two-level `AGX_CONCAT` helper.

## Rejected
- **normalMatrix with non-uniform scale:** correct. Column i scaled by 1/sᵢ is R·S⁻¹ = (R·S)⁻ᵀ. render_graph's `inverse(mat3(model))` uploaded un-transposed into a row-major slot is the same matrix. It is unused.
- **forward/right/up(quat) vs tranformationMatrix(quat):** consistent (both `mat3_cast` / `q*axis`). `forward(vec3 r)` equals `forward(glm::quat(r))` (both ZYX); only the YXZ matrix pair is the odd one out (B-FOUND-03).
- **Handedness / camera / projection:** RIGHT×FORWARD = X×Y = Z = UP is right-handed. CameraSystem's view rows (s, u, −f) and RH projection map z = −near to depth 0 and z = −far to 1, with the Y flip for Vulkan. Correct. (`inverseViewMatrix` has +f in column 2 where −f belongs, but it has no reader. That belongs to the scene group.)
- **decomposeTRS(quat) round trip for positive scale:** exact.
- **Kinematic controller `eulerAngles` → `quat` round trip** with yaw locked at 0 and |pitch| ≤ 1.5: unique, stable.
- **AGX_ASSERT side effects dropped in release:** none of the ~40 uses in `src/` (excluding ai) has a side-effecting argument. The unused-variable-in-release issue is a known gotcha (agent-notes).
- **Logging format misuse / thread safety:** all 7 `ALOG` calls go through `std::format_string` (compile-checked). Sinks are mutex-guarded, NO_THREAD mode.
- **Profiler nesting / singleton lifetime:** independent timers keyed by name. The Meyers singleton outlives `Engine::run`.
- **hashCombine fold:** `(hashCombine(seed, rest), ...)` combines sequentially and correctly. It is unexported and unused.
- **RollingAverage precision drift:** the add/subtract error is ~1e−13 relative per step, which is negligible for ms timings. N = 0 is rejected by `requires(N > 0)`.
- **`std::size_t` without `<cstddef>` in rolling_average.cppm:** `<array>` must declare it (`array<T, size_t N>`), so it is not a Linux break.
- **Timer units / ScopeTimer format:** `duration<double, milli>` is correct. `"{:>8}"` is a valid chrono spec.
- **GLM_FORCE_DEPTH_ZERO_TO_ONE only in math's GMF:** no other TU includes glm, and no glm projection helper is used (the camera builds its own).
- **readBinary(path) on a Linux directory** (possible huge `tellg` → `length_error`): low confidence, and the function is unused. Folded into B-FOUND-13's fix.
