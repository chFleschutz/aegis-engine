# aegis.math: implementation plan

Plans the replacement of glm by an engine-owned `aegis.math`. The requirements (why, goals, conventions,
required surface, gates) are in `math-library-plan.md` from the stray docs, the *spec*; this plan takes
them as given, adds what the spec left open, and settles the architecture, the consumer API, the
implementation style and the PR sequence. Survey numbers are from `claude/math-library-plan` on
2026-11-21. The decision is recorded in
[`docs/adr/0013-own-generic-row-major-math-library.md`](adr/0013-own-generic-row-major-math-library.md).

**Deviations from the spec:**
- The spec lists integer and `double` types as a non-goal. This plan keeps them out of the
  *migration* but builds the core so that they cost one alias line each: the library must stay
  extensible (integer extents, `double` world positions, SIMD for one hot shape) without a rewrite.
- The spec asks for Slang names. Types follow the engine's PascalCase instead (`Float3`, `Float4x4`),
  keeping Slang's *shapes* (`Float3x4` is 3 rows × 4 columns); functions keep Slang's names, which
  are already camelCase (`dot`, `normalize`, `lerp`, `saturate`, `mul`).
- The spec wants `float4x4` default-constructed to zero. Here matrices and quaternions have no default
  constructor at all; their state is always named (`Float4x4::identity()`, `Quat::identity()`).

## What the survey adds to the spec

- **glm enters the engine through one door.** Only `src/aegis/math/math.cppm` includes glm, and only
  `aegis-math` links `glm::glm`. The 91 files that write `glm::vec3` get the names from its exported
  `namespace glm { using … }` block. So the swap needs no glue between subsystems: once `aegis.math`
  exports the new names, the rest is a rename, and nothing outside `aegis-math` links glm afterwards
  (this answers the spec's last open question).
- **The scene files already store quaternions as `x y z w`** (`reflect/value.cpp:115`, parsed at :176
  with a swapped constructor call). The new quaternion needs no data migration, only that one parser
  line changes.
- **Raw matrix indexing is a short list:** about 40 lines, in `camera_system.cpp`, `editor_camera.cpp`,
  `shadows.cpp`, `lighting_pass.cpp`, `picking.cpp`, `viewport.cpp`, `gpu_types.cppm`,
  `instance_store.cppm`, `math.cppm` itself and five test files. These are the only places where
  the column-major to row-major switch changes what an expression means.
- **Matrices and quaternions are already initialised explicitly** almost everywhere (`glm::mat4{ 1.0f }`,
  `glm::quat{ 1.0f, 0.0f, 0.0f, 0.0f }`). The exceptions are about eight members in
  `render_graph/passes/gpu_types.cppm` (`glm::mat4 view;`, `glm::mat3x4 model;`), two locals and the
  generic `as<T>()` in `editor/ui/panels/inspector_panel.cpp:40` (`T{}`). No container holds matrices
  or quaternions, so dropping their default constructors costs little.
- **Broadcast vector constructors are common:** `glm::vec3{ 1.0f }` (default scales, registry defaults).
  An aggregate `{ 1.0f }` would mean `{1, 0, 0}` and still compile, so vectors need real constructors.
- **Shader matrices are already row-major** (`-matrix-layout-row-major` in `cmake/AegisShaders.cmake`),
  so the new storage needs no shader change.
- **Validation follows `NDEBUG` only** (`rhi/context.cppm:21`), not the optimisation level, so a Debug
  build compiled with `-O1` keeps asserts and validation.

## Implementation style: a generic core, kept shallow

**Decision:** three class templates, constrained by concepts, with every type the engine uses as an
alias:

```cpp
template <Scalar T, std::size_t N>                 requires Dimension<N>                  struct Vector;
template <Scalar T, std::size_t R, std::size_t C>  requires Dimension<R> && Dimension<C>  struct Matrix;
template <std::floating_point T>                                                          struct Quaternion;

using Float3 = Vector<float, 3>;  using Float4x4 = Matrix<float, 4, 4>;  using Quat = Quaternion<float>;
```

Each operation is written **once**, for every scalar type and size. What keeps this from becoming glm:

- **One level between an operator and the arithmetic.** `operator+` on vectors calls `detail::zip`,
  an `if constexpr` over N that spells out the components; matrix kernels are plain loops over
  `m[r, c]`. No `compute_*` helper structs, no qualifier/precision parameter, no SFINAE, no CRTP, no
  expression templates.
- **Per-size code is limited to vector storage.** `Vector<T, 2>`, `Vector<T, 3>`, `Vector<T, 4>` are
  partial specialisations only because named members `x y z w` need them; they hold the members,
  the constructors, `operator[]` and the named accessors (`xy()`, `xyz()`). Matrices need no
  specialisation: their storage is a flat array.
- **Constraints are concepts** (see below). A wrong use is a one-line constraint failure, not a
  backtrace.
- **Hot shapes can be specialised without touching the rest.** If the benchmark shows a generic kernel
  missing the gate (say `Float4x4 * Float4x4`), a more constrained overload for that shape is added
  beside the generic one: hand-unrolled first, intrinsics only if that is not enough. Other scalar
  types and shapes keep the generic path.

**Debug speed** is not a design driver. Generic templates are slow at `-O0`, like most of the engine;
a **dev preset** (Debug with `-O1`, so asserts and validation stay on, see PR 0) is the answer for
day-to-day runs, and plain `-debug` stays for stepping through code.

### Concepts

| Concept | Meaning | Used by |
|---|---|---|
| `Scalar<T>` | `std::is_arithmetic_v<T> && !std::same_as<T, bool>` | storage and arithmetic of all three templates |
| `Dimension<N>` | `N >= 2 && N <= 4` (non-type, so it appears in `requires`) | `Vector`, `Matrix` |
| `VectorType<V>` | `V` is a `Vector<T, N>` | generic consumer code, the scalar helpers' overloads |
| `MatrixType<M>` | `M` is a `Matrix<T, R, C>` | generic consumer code, `nearlyEqual` |
| `FloatVector<V>` | `VectorType<V>` with a floating-point `ValueType` | `length`, `normalize`, `distance`, `lerp` |

Plus the standard ones where they say it best: `std::floating_point` for `inverse`, `determinant` and
`Quaternion`; `std::signed_integral<T> || std::floating_point<T>` for unary minus; `std::integral` for
the integer-only operations (`%`, bitwise, shifts) once a first integer user needs them.

### Rules for the code

- **Every function is `constexpr` or explicitly `inline`** and defined in the interface. A member
  function defined inside a class in a named module is *not* implicitly inline, so do not rely on
  that; function templates are fine as they are, non-template helpers need the keyword.
  `std::sqrt`/`sin`/`cos` are not `constexpr` in C++23 on both standard libraries, so `length`,
  `normalize` and rotations are not `constexpr`.
- **Vectors and quaternions by value, matrices by `const&`.**
- **Scalars mix in through `std::type_identity_t<T>`:** `operator*(Vector<T, N>, std::type_identity_t<T>)`
  so that `v * 2` deduces `T` from the vector only. Vectors of different scalar types never mix
  implicitly; `Float2{ someUint2 }` is an explicit conversion.
- **No `std::array` inside the types:** plain members in vectors, `T m_elements[R * C]` in matrices,
  plain members in quaternions. All types stay trivially copyable and standard layout.
- **Vector `operator[]` branches, never `(&x)[i]`** (which is undefined behaviour); it folds away
  when the index is a constant.
- **No SIMD, no alignment beyond `alignof(T)`:** `Float4` is 16 bytes with alignment 4, as scalar
  layout needs. glm runs scalar today (no `GLM_FORCE_INTRINSICS`), so the gate compares like with like.

## Architecture

Module `aegis.math`, namespace `aegis::math`, folder `src/aegis/math/`, new-style (moves from the
legacy list in `AGENTS.md`). `import std;`, plus `<cassert>` for `assert` in the partitions that check indices: no glm, no other macros.
`aegis-math` links nothing.

```
math.cppm            export module aegis.math; export import :scalar, :vector, :matrix, :quaternion, :transform, :aliases;
scalar.cppm          :scalar      Scalar, Dimension; pi<T>/twoPi<T>; radians, degrees, min/max/abs/clamp/lerp/
                                  saturate/floor/fract/sqrt/mod on scalars; halfToFloat; nearlyEqual
vector.cppm          :vector      Vector<T, N> and its 2/3/4 storage, VectorType, FloatVector, detail::map/zip/
                                  reduce, arithmetic, dot, cross (N = 3), length, normalize, distance,
                                  component-wise overloads of the :scalar helpers
matrix.cppm          :matrix      Matrix<T, R, C>, MatrixType; factories, [r, c], row/column access, product
                                  (R×K · K×C, matrix · vector), mul, transpose, determinant, inverse,
                                  affineInverse (square 2..4), shape and scalar conversion
quaternion.cppm      :quaternion  Quaternion<T>; factories, product, rotate a vector, conjugate, inverse,
                                  normalize, slerp, rotationMatrix, eulerAngles
transform.cppm       :transform   world axes, forward/right/up, transformMatrix, decomposeTRS, viewMatrix,
                                  inverseViewMatrix, perspective, orthographic: templates on the float type
aliases.cppm         :aliases     the exported aliases and a static_assert block on their layout
```

Partitions form a chain, `scalar ← vector ← matrix ← quaternion ← transform ← aliases`, so there are
no partition cycles (see `tests/README.md`). Importers write `import aegis.math;`. Everything is a
template, so there is no `.cpp`: the module's implementation is its interface.

**Exported aliases** (one line each, no code behind them):

| Scalar | Vectors | Matrices | Quaternion |
|---|---|---|---|
| `float` | `Float2` `Float3` `Float4` | `Float2x2` … `Float4x4` (all nine shapes) | `Quat` |
| `double` | `Double2` `Double3` `Double4` | `Double2x2` … `Double4x4` | none until used (`Quaternion<double>` works) |
| `std::int32_t` | `Int2` `Int3` `Int4` | none (no integer matrices until a use) | none |
| `std::uint32_t` | `Uint2` `Uint3` `Uint4` | none | none |

The migration uses only the `Float` names and `Quat`. The others exist so that the first integer or
`double` user finds the type already tested (see Testing).

Errors: nothing in the library fails as a value. `inverse` of a singular matrix is the caller's
precondition (as in glm and Slang); an out-of-range index is misuse and `assert`s; integer division
by zero is undefined, as for scalars. `normalize` of a zero vector returns the zero vector (the
spec's choice), not NaN.

## The types

```cpp
export namespace aegis::math
{
template <typename T>
concept Scalar = std::is_arithmetic_v<T> && !std::same_as<T, bool>;

template <std::size_t N>
concept Dimension = N >= 2 && N <= 4;

template <Scalar T, std::size_t N>
    requires Dimension<N>
struct Vector;

template <Scalar T>
struct Vector<T, 3>
{
    using ValueType = T;
    static constexpr std::size_t componentCount = 3;

    T x{};
    T y{};
    T z{};

    constexpr Vector() noexcept = default;                                         // zero
    explicit constexpr Vector(T s) noexcept : x{ s }, y{ s }, z{ s } {}            // broadcast, like glm and Slang
    constexpr Vector(T x, T y, T z) noexcept : x{ x }, y{ y }, z{ z } {}
    constexpr Vector(Vector<T, 2> xy, T z) noexcept;
    explicit constexpr Vector(Vector<T, 4> v) noexcept;                            // drops w
    template <Scalar U>
    explicit constexpr Vector(Vector<U, 3> v) noexcept;                            // Int3 -> Float3 and so on

    [[nodiscard]] constexpr auto xy() const noexcept -> Vector<T, 2>;
    [[nodiscard]] constexpr auto operator[](std::size_t i) noexcept -> T&;         // asserts i < 3
    [[nodiscard]] constexpr auto operator[](std::size_t i) const noexcept -> const T&;

    friend constexpr auto operator==(const Vector&, const Vector&) noexcept -> bool = default;
};

namespace detail
{
// The one place that knows the vector sizes; every component-wise operation goes through these.
template <Scalar T, std::size_t N, typename F>
constexpr auto zip(Vector<T, N> a, Vector<T, N> b, F f) noexcept
{
    using R = decltype(f(a.x, b.x));
    if constexpr (N == 2)
        return Vector<R, 2>{ f(a.x, b.x), f(a.y, b.y) };
    else if constexpr (N == 3)
        return Vector<R, 3>{ f(a.x, b.x), f(a.y, b.y), f(a.z, b.z) };
    else
        return Vector<R, 4>{ f(a.x, b.x), f(a.y, b.y), f(a.z, b.z), f(a.w, b.w) };
}
// map (unary), reduce (dot) and generate (a vector from f(i)) look the same
}

template <Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator+(Vector<T, N> a, Vector<T, N> b) noexcept -> Vector<T, N>
{
    return detail::zip(a, b, std::plus{});
}

template <Scalar T, std::size_t N>
[[nodiscard]] constexpr auto operator*(Vector<T, N> v, std::type_identity_t<T> s) noexcept -> Vector<T, N>;

template <FloatVector V>
[[nodiscard]] inline auto normalize(V v) noexcept -> V;                            // zero stays zero

template <Scalar T, std::size_t R, std::size_t C>
    requires Dimension<R> && Dimension<C>
struct Matrix
{
    using ValueType = T;
    static constexpr std::size_t rowCount = R;
    static constexpr std::size_t columnCount = C;

    // No default constructor: the state is always named.
    [[nodiscard]] static constexpr auto zero() noexcept -> Matrix;
    [[nodiscard]] static constexpr auto identity() noexcept -> Matrix
        requires(R == C);
    template <std::same_as<Vector<T, C>>... Rows>
        requires(sizeof...(Rows) == R)
    [[nodiscard]] static constexpr auto fromRows(Rows... rows) noexcept -> Matrix;
    template <std::same_as<Vector<T, R>>... Columns>
        requires(sizeof...(Columns) == C)
    [[nodiscard]] static constexpr auto fromColumns(Columns... columns) noexcept -> Matrix; // glTF, ImGuizmo

    template <Scalar U, std::size_t R2, std::size_t C2>
    explicit constexpr Matrix(const Matrix<U, R2, C2>& m) noexcept;                // keeps the overlap, rest from identity

    [[nodiscard]] constexpr auto operator[](std::size_t row, std::size_t column) noexcept -> T&;  // C++23, asserts bounds
    [[nodiscard]] constexpr auto operator[](std::size_t row, std::size_t column) const noexcept -> const T&;
    [[nodiscard]] constexpr auto row(std::size_t i) const noexcept -> Vector<T, C>;
    [[nodiscard]] constexpr auto column(std::size_t i) const noexcept -> Vector<T, R>;
    constexpr void setRow(std::size_t i, Vector<T, C> v) noexcept;
    constexpr void setColumn(std::size_t i, Vector<T, R> v) noexcept;

    friend constexpr auto operator==(const Matrix&, const Matrix&) noexcept -> bool = default;

private:
    constexpr Matrix() noexcept = default;                                         // for the factories only
    T m_elements[R * C]{};                                                         // row-major: row * C + column
};

template <Scalar T, std::size_t R, std::size_t K, std::size_t C>
[[nodiscard]] constexpr auto operator*(const Matrix<T, R, K>& a, const Matrix<T, K, C>& b) noexcept -> Matrix<T, R, C>
{
    auto result = Matrix<T, R, C>::zero();
    for (std::size_t r = 0; r < R; ++r)
        for (std::size_t c = 0; c < C; ++c)
            for (std::size_t k = 0; k < K; ++k)
                result[r, c] += a[r, k] * b[k, c];
    return result;
}
template <Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto operator*(const Matrix<T, R, C>& m, Vector<T, C> v) noexcept -> Vector<T, R>;
template <std::floating_point T, std::size_t N>
[[nodiscard]] constexpr auto inverse(const Matrix<T, N, N>& m) noexcept -> Matrix<T, N, N>; // if constexpr per N

template <std::floating_point T>
struct Quaternion
{
    using ValueType = T;

    T x;
    T y;
    T z;
    T w;

    // No default constructor and no four-scalar constructor: the state and the order are always named.
    [[nodiscard]] static constexpr auto identity() noexcept -> Quaternion;
    [[nodiscard]] static constexpr auto fromXyzw(T x, T y, T z, T w) noexcept -> Quaternion; // memory and scene-file order
    [[nodiscard]] static auto fromAxisAngle(Vector<T, 3> axis, T radians) noexcept -> Quaternion;
    [[nodiscard]] static auto fromEuler(Vector<T, 3> radians) noexcept -> Quaternion;  // glm's convention, pinned by test
    [[nodiscard]] static auto fromMatrix(const Matrix<T, 3, 3>& rotation) noexcept -> Quaternion;

    friend constexpr auto operator==(Quaternion, Quaternion) noexcept -> bool = default;

private:
    constexpr Quaternion(T x, T y, T z, T w) noexcept;                             // for the factories only
};
}
```

**Default state, per type:**

- **`Matrix` and `Quaternion` have no public default constructor.** Every value is made by a named
  factory (`Float4x4::identity()`, `Float4x4::zero()`, `Quat::identity()`), so a reader never has to
  know what an implicit default is, and no zero quaternion (an invalid rotation) appears by accident.
  A member is written `Float4x4 view = Float4x4::identity();`, which is what the engine already does
  with `{ 1.0f }`. Consequences: a struct with a matrix member and no initialiser is not
  default-constructible (wanted: the GPU structs then name their state), and `std::vector<Float4x4>`
  can't `resize(n)` without a value (no such container exists).
- **`Vector` keeps its default constructor (zero)**, plus broadcast and per-component constructors.
  Zero is the only sensible default, vertex data lives in `std::vector<Float3>` that gets resized,
  `reflect::Value` holds vectors, and glm and the new type agree on component order, so none of the
  vector constructors is ambiguous.

**Matrix access:**

- **Elements use the C++23 multidimensional subscript, `m[row, column]`, and there is no single-index
  `operator[]`.** Every glm-style `m[i]` and `m[c][r]` therefore fails to compile instead of silently
  reading a column as a row; rows and columns are explicit (`m.row(i)`, `m.column(i)`, `setRow`,
  `setColumn`). The flat storage makes `[r, c]` a direct array access, so the generic kernels are
  plain loops that the optimiser unrolls. Clang supports the syntax since 15; MSVC support is
  checked when its presets build again.
- `row(i)` returns by value although rows are contiguous: one rule for rows and columns, and no
  reference into the flat array that would need a cast.

**Conversions and layout:**

- The explicit converting constructor covers the spec's conversions in one template:
  `Float3x4{ m4 }` keeps the top three rows (the 48-byte instance transform), `Float4x4{ m34 }`
  appends `0 0 0 1`, `Float3x3{ m4 }` keeps the upper-left block, and `Float4x4{ someDouble4x4 }`
  converts the scalar type. It is the only constructor besides copy, and it can't be mistaken for a
  default state.
- Every exported alias: `static_assert` on size (`N * sizeof(T)`, `R * C * sizeof(T)`), alignment
  `alignof(T)`, `std::is_trivially_copyable_v`, `std::is_standard_layout_v`.

**Departures from Slang semantics,** recorded in the ADR and, after the swap, in AGENTS "Code style":

- `operator*` between matrices, and matrix × vector, is the **linear-algebra product** (glm's
  meaning, so 75+ call sites keep their logic); `mul(a, b)` is the same product under the Slang name.
  In Slang `*` on matrices is per element, so code mirrored from a shader writes `mul`. There is no
  per-element matrix `*` and no `vector * matrix` (row-vector) product; using one fails to compile.
- `operator==` compares whole values and returns `bool` (Slang returns a per-component `bool3`);
  per-component comparisons (`Vector<bool, N>`, `any`, `all`) come with their first use.
- `mod` keeps glm's floored semantics (`x - y * floor(x / y)`), unlike Slang's `fmod`, so its two
  uses don't change.

## Consumer usage

Types and functions are always qualified `math::`, as with `rhi::` and `log::`; this also keeps
`math::min` and `std::min` apart. Consumers name the aliases; the templates `math::Vector` /
`math::Matrix` and the concepts are only spelled by code that is itself generic.

```cpp
import aegis.math;

// Vectors: glm-like construction, zero by default
const math::Float3 offset{ 0.0f, 0.0f, 1.0f };
const math::Float3 scale{ 1.0f };                                  // broadcast
math::Float3 velocity;                                             // zero
const math::Float4 point{ location, 1.0f };
const float distance = math::length(target - location);
const auto direction = math::normalize(math::cross(forward, math::world::UP));
const math::Float2 uv = position.xy();

// Matrices: named state, column vectors, composition unchanged, row-major storage
math::Float4x4 model = math::Float4x4::identity();
const math::Float4x4 viewProjection = camera.projection * camera.view;
const math::Float4 clip = viewProjection * math::Float4{ position, 1.0f };
const math::Float4 plane = viewProjection.row(3) + viewProjection.row(0); // frustum planes are rows
const math::Float3 translation = math::Float3{ model.column(3) };  // was glm::vec3{ model[3] }
projection[1, 1] *= -1.0f;                                         // was projection[1][1]

// Hand-built matrices read as their rows (shadows.cpp's light view)
const auto view = math::Float4x4::fromRows(
    math::Float4{ side, -x },
    math::Float4{ up, -y },
    math::Float4{ toLight, -eye },
    math::Float4{ 0.0f, 0.0f, 0.0f, 1.0f });

// Builders replace hand-written projections (Vulkan clip space: depth 0..1, y down)
camera.projection = math::perspective(camera.fovY, camera.aspect, camera.near, camera.far);
shadow.projection = math::orthographic(halfWidth, halfWidth, 0.0f, depth);

// Quaternions: named state and component order
math::Quat rotation = math::Quat::identity();
rotation = math::Quat::fromEuler(math::radians(math::Float3{ 90.0f, 0.0f, 0.0f }));
const auto turn = math::Quat::fromAxisAngle(math::world::UP, math::radians(45.0f));
const auto parsed = math::Quat::fromXyzw(c[0], c[1], c[2], c[3]);  // reflect's "x y z w" text
const math::Float3 facing = turn * math::world::FORWARD;

// GPU upload: the bytes already match the shader, no rowMajor4
gpu.viewProjection = viewProjection;                               // Float4x4 member in the GPU struct
gpu.model = math::Float3x4{ model };                               // top three rows

// Other scalar types work the same, and convert explicitly
const math::Uint2 extent{ 1920u, 1080u };
const math::Float2 texel = math::Float2{ 1.0f } / math::Float2{ extent };
const math::Double3 worldPosition{ 1.0e6, 2.0e6, 0.0 };
const math::Float3 cameraRelative{ worldPosition - cameraPosition }; // Double3 -> Float3

// Generic code, where it is genuinely generic
template <math::FloatVector V>
auto projectOnto(V v, V axis) -> V { return axis * (math::dot(v, axis) / math::dot(axis, axis)); }

// Interop at the boundary, no value_ptr
auto gizmo = std::bit_cast<std::array<float, 16>>(math::transpose(matrix)); // ImGuizmo is column-major
matrix = math::transpose(std::bit_cast<math::Float4x4>(gizmo));             // and back
const ImVec2 imguiSize{ size.x, size.y };                          // ImGui/fastgltf types convert by component
```

## Migration hazards and how the plan removes them

The spec wants a mechanical migration. A rename is only safe if no expression silently changes meaning.
With no single-index `operator[]`, no default constructors and no positional quaternion constructor,
almost every hazard is now a compile error; the prep PR (under glm) moves those sites onto bridge
helpers first so that the mechanical rename commit builds.

| glm expression | New library | Handled by |
|---|---|---|
| `m[i]`, `m[c][r]` (column, then row) | compile error: no single-index `[]` | prep: `math::row`, `math::column`, `math::element(m, r, c)`, builders; script: `math::element(m, r, c)` → `m[r, c]` |
| `glm::mat4{ c0, c1, c2, c3 }` (columns) | compile error: no vector-list constructor | prep: `math::fromColumns` / `math::fromRows` bridges, renamed by the script |
| `glm::mat4{ 1.0f }`, `{ 0.0f }` | compile error: no scalar constructor | script maps the exact literals to `identity()` / `zero()` |
| `glm::mat4 view;` (uninitialised) | compile error: no default constructor | prep gives each an explicit `{ 1.0f }` or `{ 0.0f }`, which the script maps |
| `glm::quat{ w, x, y, z }` | compile error: no public four-scalar constructor | script maps the identity literal to `Quat::identity()`; prep moves the few others (reflect parser, tests) onto a `math::quatFromXyzw` bridge |
| `glm::quat{ eulerVec }` | compile error: no vector constructor | prep: `math::quatFromEuler` bridge → `Quat::fromEuler` |
| `T{}` with `T = glm::quat` (`inspector_panel.cpp:40`) | compile error | prep: the helper takes the fallback value explicitly |
| `glm::mat3x4` is 3 columns, `Float3x4` is 3 rows | memory identical where used (rows of `rowMajor4(model)`) | its indexed loops (`instance_store.cppm`, `gpu_types.cppm`) are in the `m[c][r]` row above |
| `vec3{ 1.0f }` broadcast | same meaning | nothing to do |
| `v * M` row-vector product, per-element matrix ops | compile error: not provided | rewrite where the compiler finds one |
| `vec3 * int` | same meaning (`std::type_identity_t<T>` scalar) | nothing to do; vector/vector of different types is a compile error |
| `normalize(0)` was NaN | now zero | behaviour change on purpose; property test pins it |

What still compiles with a different meaning: nothing in the table. The remaining risk is a wrong
rewrite by hand, which the full `ctest`, the golden images and the screenshots catch.

## Testing and benchmarks

- `tests/math` (`aegis-tests-math`) links `glm::glm` PRIVATE as the oracle, includes glm before any
  import (AGENTS' include order), and converts layouts in the test only. Split by partition:
  `vector.test.cpp`, `matrix.test.cpp`, `quaternion.test.cpp`, `transform.test.cpp`, `layout.test.cpp`.
- **Typed tests keep the generic core honest:** the vector and matrix suites are
  `TEST_CASE_TEMPLATE` over `float` and `double` (and the arithmetic suite also over `std::int32_t`
  and `std::uint32_t`), across sizes 2, 3, 4 and the square and `3x4` matrix shapes. An exported
  alias is a tested alias; this is what makes the integer and `double` types usable on day one rather
  than a promise.
- **Compile-only checks** with `requires` expressions (`static_assert(!requires { … })`): the
  constraints reject `normalize(Int3)`, `inverse(Float3x4)`, `-Uint3`, `Float3 + Int3`; the hazard
  forms don't compile (`m[0]`, `Float4x4{ 1.0f }`, `Float4x4{ v0, v1, v2, v3 }`, a default-constructed
  `Float4x4` or `Quat`, `Quat{ 1, 0, 0, 0 }`). This pins the migration safety net, not only the maths.
- **Differential (float, against glm):** fixed-seed random inputs through both for products,
  `inverse`, `determinant`, `transpose`, quat ↔ matrix, `fromEuler`/`eulerAngles` (glm's convention),
  `slerp`, `viewMatrix`, `perspective`, `orthographic`, `transformMatrix`, `decomposeTRS`,
  `fromColumns` against glm's constructor. Tolerance stated per function (relative 1e-5 for
  products, looser for `inverse`, whose inputs are filtered by determinant). The `double`
  instantiations run the same comparisons against glm's `dmat`/`dquat`.
- **Properties:** `inverse(M) * M ≈ I`; TRS round trip including the negative-x mirror; `normalize`
  of zero; quat round trip near 180°; `affineInverse` equals `inverse` on affine input;
  `fromRows(m.row(0), …) == m` and `fromColumns(m.column(0), …) == m`.
- **Layout:** the `static_assert`s, plus a test that `std::bit_cast<std::array<float, 12>>(Float3x4)`
  equals the rows the shader expects (replaces the `rowMajor4` checks in
  `tests/render_passes/frame_data.test.cpp`).
- **Benchmark:** `tools/bench-math/` (`aegis-bench-math`, built only with `-DAEGIS_BUILD_BENCHMARKS=ON`,
  never in `ctest`), own vs glm on the spec's six operations, for `float` (and `double` for
  information). Gates: **Release within 10% of glm** on Clang/Windows and Clang/Linux; **dev preset
  not slower than glm** on the same preset. `-O0` Debug numbers are reported, not gated. A shape that
  misses a gate gets its specialised overload; the results go in the library PR.

## PR sequence

Each PR follows AGENTS (worktree, `ctest -LE device` per commit, full `ctest` before pushing).

0. **`build/dev-presets`: Debug with optimisation.** `windows-clang-dev` and `linux-clang-dev`
   presets: `CMAKE_BUILD_TYPE=Debug` (so `NDEBUG` stays undefined: asserts and validation on) plus
   `-O1` added after CMake's Debug flags (check in the compile commands that it wins over `-O0`, and
   that the Windows Debug runtime defines survive; do not overwrite `CMAKE_CXX_FLAGS_DEBUG`). AGENTS
   "Build & run" names the preset for day-to-day runs and keeps `-debug` for stepping. Independent of
   the math work; can land first or in parallel.
1. **`feat/math-library`: the library beside glm.** New partitions in `aegis.math` exporting the
   templates, concepts and aliases, while the `namespace glm` wrapper and the glm-typed helpers stay
   (overloads by type, no clash: the new names are PascalCase). Tests, layout asserts, benchmark with
   its numbers in the PR body. Nothing in the engine uses the new types yet.
2. **`refactor/math-prep`: move the meaning-changing expressions onto bridges, still on glm.** Add
   bridge helpers implemented on glm (`math::fromRows`, `math::fromColumns`, `math::row`,
   `math::column`, `math::element`, `math::perspective`, `math::orthographic`, `math::quatFromEuler`,
   `math::quatFromXyzw`), give the uninitialised matrices an explicit literal, fix the inspector's
   `T{}`, and move every site from the hazard table onto them; the two hand-built perspectives and
   the shadow view/ortho collapse into builders. All tests and the golden images must be unchanged:
   this PR proves the bridges are behaviour-neutral before the backend moves. To find every site,
   build this branch once against PR 1's types: the compile errors are the list. Can be stacked on 1.
3. **`refactor/math-swap`: switch the backend.**
   - Commit A: bridges and temporary shims (`rowMajor4` as identity) re-pointed at the new types;
     the `transform` partition takes the new types.
   - Commit B, **mechanical**: a committed script (`tools/migrate-math.sh`, deleted in commit D)
     renames `glm::vec3` → `math::Float3`, `glm::mat4` → `math::Float4x4`, `glm::quat` →
     `math::Quat`, `glm::length` → `math::length`, the exact `glm::mat4{ 1.0f }` / `{ 0.0f }` and
     identity-quaternion literals, the bridges to their final names, and `math::element(m, r, c)` to
     `m[r, c]`. It must build. Report its hash for `.git-blame-ignore-revs`.
   - Commit C: delete the shims and `rowMajor4` calls, the transposing in `toGpuTransform`, the
     ImGuizmo `value_ptr` calls (→ `bit_cast` of the transpose), the `glm::make_vec3` and
     `unpackHalf1x16` sites.
   - Commit D: remove the `namespace glm` wrapper, the glm include and `glm::glm` from `aegis-math`;
     `AGENTS.md` (math leaves the legacy list), `docs/agent-notes.md` (the `rowMajor4` note becomes
     "upload matrices as they are"), and an `aegis.math` paragraph in AGENTS "Code style" (row-major
     storage, column vectors, `[r, c]`, named default state, `*` vs `mul`, citing
     [`docs/adr/0013-own-generic-row-major-math-library.md`](adr/0013-own-generic-row-major-math-library.md)).
     These are coding rules, so they stay out of `GLOSSARY.md`.
   - Full `ctest` with device and golden tests, and screenshots of `simple` and `sponza`: a wrong
     hand rewrite shows as a wrong image, not a failed build.
4. **Follow-ups, on demand only:** `decomposeTRS` returning a struct instead of out-parameters,
   reverse-Z (`perspective` gets a depth-mode parameter in the PR that introduces it), integer-only
   and per-component comparison operations with their first user, specialised or SIMD overloads for a
   shape that a profile shows hot.

## Spec open questions, resolved

- **Swizzles:** named accessors only, `xy()` and `xyz()` (the only ones used: 3 and 1 sites); add
  others on first use.
- **Quaternion order in saved scenes:** already `x y z w` in text; no migration.
- **Matrix default:** none; `zero()` and `identity()` name it.
- **Reverse-Z:** out of scope; one-place change in `perspective` later.
- **glm consumers left outside the engine:** none; after PR 3 only `aegis-tests-math` and
  `aegis-bench-math` link glm.

## Still open (needs the owner)

- Constants spelling: this plan uses `math::pi<T>` / `math::twoPi<T>` (variable templates, so
  `double` gets its own precision); the spec wrote `two_pi`.
- Whether to export the `Int`/`Uint`/`Double` aliases in PR 1 (this plan: yes, tested) or only the
  `Float` ones and `Quat` until a first use (smaller surface, same generic code).
