# An own generic, row-major math library replaces glm

Decided 2026-11-21, see [`docs/plans/math-library-plan.md`](../plans/math-library-plan.md).

`aegis.math` becomes the engine's own math library, and glm stays only as the test oracle. It is built
from three class templates, `Vector<T, N>`, `Matrix<T, R, C>` and `Quaternion<T>`, constrained by
concepts and used through PascalCase aliases (`Float3`, `Float4x4`, `Quat`, `Int2`, `Double3`).
Matrices are stored row-major, so the memory matches the shaders' and uploads need no conversion.
Vectors are column vectors, so `M * v` and `projection * view * model` keep today's order, and
`operator*` is the linear-algebra product, with `mul` as its Slang name. A matrix element is
`m[row, column]`, and there is no single-index `[]`. Matrices and quaternions have no default
constructor: their state is always named (`Float4x4::identity()`, `Float4x4::zero()`,
`Quat::identity()`, `Quat::fromXyzw(…)`). Vectors keep a zero default.

Why:
- **Layout:** glm's column-major layout forces a `rowMajor4` conversion on every upload.
- **Clip space:** two projection matrices are built by hand because glm's don't fit Vulkan's clip
  space.
- **Modules:** glm only reaches the modules through a wrapper with macros.
- **A generic core over one struct per type:** an integer, `double` or SIMD-specialised type later
  costs an alias or an overload, not a rewrite. The price is slower `-O0` code, answered by a Debug
  preset with `-O1` instead of by the design.
- **Glm forms fail to compile:** without a single-index subscript or positional constructors, every
  glm form whose meaning would change (`m[i]`, `m[c][r]`, `mat4{ 1.0f }`, a column list,
  `quat{ w, x, y, z }`) is a compile error rather than a silent transpose. The migration can then be
  a rename.

**Considered options**
- *Keep glm:* no work, but the row-major conversions, the hand-built projections and the macro
  wrapper stay, and Debug runs through glm's template layers.
- *One hand-written struct per type:* the fastest code at `-O0` and the simplest to read. But a
  second scalar type duplicates everything, which conflicts with integer extents, `double` world
  positions and SIMD for one hot shape later.
- *Slang's lowercase names (`float3`) and its per-element matrix `*`:* identical spelling to the
  shaders. But the names break the engine's PascalCase types, and the per-element `*` would change
  the meaning of 75+ call sites during a mechanical migration.
- *Zero-initialising default constructors on matrices and quaternions:* convenient, but the default
  state stays implicit, and a zero quaternion is not a rotation.

**Consequences**
- Code mirrored from a shader writes `mul`, because Slang's `*` on matrices is per element.
- A struct with a matrix or quaternion member names its state in the initialiser, or it is not
  default-constructible.
- glm's `quat{ w, x, y, z }` constructor order is gone. Scene files already store `x y z w`, so they
  don't change.

→ the dev preset, the library, the prep and the swap PRs of the plan (issues to be filed in the
`render` lane).
