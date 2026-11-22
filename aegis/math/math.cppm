export module aegis.math;
import std;

// The math library (docs/plans/math-library-plan.md, docs/adr/0013-own-generic-row-major-math-library.md)
export import :scalar;
export import :vector;
export import :matrix;
export import :quaternion;
export import :transform;
export import :aliases;

// Temporary shims for the swap from glm (docs/plans/math-library-plan.md, "PR sequence" step 3): the glm names the
// engine still spells, re-pointed at the new types, until tools/migrate-math.sh renames them
export namespace glm
{
using vec2 = aegis::math::Float2;
using vec3 = aegis::math::Float3;
using vec4 = aegis::math::Float4;

using quat = aegis::math::Quat;

using mat3 = aegis::math::Float3x3;
using mat3x4 = aegis::math::Float3x4;
using mat4 = aegis::math::Float4x4;

using aegis::math::clamp;
using aegis::math::cross;
using aegis::math::degrees;
using aegis::math::determinant;
using aegis::math::dot;
using aegis::math::eulerAngles;
using aegis::math::inverse;
using aegis::math::length;
using aegis::math::mod;
using aegis::math::normalize;
using aegis::math::radians;

/// @brief The rotation by `angle` radians around `axis`: Quat::fromAxisAngle with the arguments swapped
[[nodiscard]] inline auto angleAxis(float angle, const vec3& axis) -> quat
{
    return quat::fromAxisAngle(axis, angle);
}

/// @brief Matrices are row-major now, so the conversion is the identity
[[nodiscard]] constexpr auto rowMajor4(const mat4& m) noexcept -> mat4
{
    return m;
}
}

// Bridges, re-pointed at the new types; tools/migrate-math.sh renames their call sites to the library's spelling
export namespace aegis::math
{
[[nodiscard]] constexpr auto fromRows(Float4 r0, Float4 r1, Float4 r2, Float4 r3) noexcept -> Float4x4
{
    return Float4x4::fromRows(r0, r1, r2, r3);
}

[[nodiscard]] constexpr auto fromRows(Float3 r0, Float3 r1, Float3 r2) noexcept -> Float3x3
{
    return Float3x3::fromRows(r0, r1, r2);
}

[[nodiscard]] constexpr auto fromRows(Float4 r0, Float4 r1, Float4 r2) noexcept -> Float3x4
{
    return Float3x4::fromRows(r0, r1, r2);
}

[[nodiscard]] constexpr auto fromColumns(Float4 c0, Float4 c1, Float4 c2, Float4 c3) noexcept -> Float4x4
{
    return Float4x4::fromColumns(c0, c1, c2, c3);
}

[[nodiscard]] constexpr auto fromColumns(Float3 c0, Float3 c1, Float3 c2) noexcept -> Float3x3
{
    return Float3x3::fromColumns(c0, c1, c2);
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto row(const Matrix<T, R, C>& m, std::size_t i) noexcept -> Vector<T, C>
{
    return m.row(i);
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto column(const Matrix<T, R, C>& m, std::size_t i) noexcept -> Vector<T, R>
{
    return m.column(i);
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto element(Matrix<T, R, C>& m, std::size_t r, std::size_t c) noexcept -> T&
{
    return m[r, c];
}

template<Scalar T, std::size_t R, std::size_t C>
[[nodiscard]] constexpr auto element(const Matrix<T, R, C>& m, std::size_t r, std::size_t c) noexcept -> T
{
    return m[r, c];
}

[[nodiscard]] inline auto quatFromEuler(Float3 radians) noexcept -> Quat
{
    return Quat::fromEuler(radians);
}

[[nodiscard]] constexpr auto quatFromXyzw(float x, float y, float z, float w) noexcept -> Quat
{
    return Quat::fromXyzw(x, y, z, w);
}

namespace world
{
inline constexpr Float3 RIGHT = right<float>;
inline constexpr Float3 FORWARD = forward<float>;
inline constexpr Float3 UP = up<float>;
}
}
