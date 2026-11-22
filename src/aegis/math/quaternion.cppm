export module aegis.math:quaternion;

import std;
import :scalar;
import :vector;
import :matrix;

export namespace aegis::math
{
/// @brief A rotation as a unit quaternion, stored `x y z w` (the vector part first, as in scene files)
/// @note There is no default constructor and no four-scalar constructor: the state and the component order are
/// always named (`identity()`, `fromXyzw`). Euler angles follow glm's convention, pinned by the tests.
template<std::floating_point T>
struct Quaternion
{
    using ValueType = T;

    T x;
    T y;
    T z;
    T w;

    [[nodiscard]] static constexpr auto identity() noexcept -> Quaternion
    {
        return Quaternion{ T{ 0 }, T{ 0 }, T{ 0 }, T{ 1 } };
    }

    /// @brief The quaternion with these components, in memory and scene-file order
    [[nodiscard]] static constexpr auto fromXyzw(T x, T y, T z, T w) noexcept -> Quaternion
    {
        return Quaternion{ x, y, z, w };
    }

    /// @brief The rotation by `radians` around `axis`, which must have unit length
    [[nodiscard]] static auto fromAxisAngle(Vector<T, 3> axis, T radians) noexcept -> Quaternion
    {
        const T s = std::sin(radians * T{ 0.5 });
        return Quaternion{ axis.x * s, axis.y * s, axis.z * s, std::cos(radians * T{ 0.5 }) };
    }

    /// @brief The rotation by the Euler angles `radians` (pitch around x, yaw around y, roll around z), as glm builds
    /// it: the inverse of eulerAngles()
    [[nodiscard]] static auto fromEuler(Vector<T, 3> radians) noexcept -> Quaternion
    {
        const T cx = std::cos(radians.x * T{ 0.5 });
        const T cy = std::cos(radians.y * T{ 0.5 });
        const T cz = std::cos(radians.z * T{ 0.5 });
        const T sx = std::sin(radians.x * T{ 0.5 });
        const T sy = std::sin(radians.y * T{ 0.5 });
        const T sz = std::sin(radians.z * T{ 0.5 });
        return Quaternion{
            sx * cy * cz - cx * sy * sz,
            cx * sy * cz + sx * cy * sz,
            cx * cy * sz - sx * sy * cz,
            cx * cy * cz + sx * sy * sz,
        };
    }

    /// @brief The rotation of a pure rotation matrix (orthonormal, determinant 1)
    [[nodiscard]] static auto fromMatrix(const Matrix<T, 3, 3>& m) noexcept -> Quaternion
    {
        // Solve for the largest component first, so the division below is by the best-conditioned value.
        const T fourXSquaredMinus1 = m[0, 0] - m[1, 1] - m[2, 2];
        const T fourYSquaredMinus1 = m[1, 1] - m[0, 0] - m[2, 2];
        const T fourZSquaredMinus1 = m[2, 2] - m[0, 0] - m[1, 1];
        const T fourWSquaredMinus1 = m[0, 0] + m[1, 1] + m[2, 2];

        int biggestIndex = 0;
        T fourBiggestSquaredMinus1 = fourWSquaredMinus1;
        if (fourXSquaredMinus1 > fourBiggestSquaredMinus1)
        {
            fourBiggestSquaredMinus1 = fourXSquaredMinus1;
            biggestIndex = 1;
        }
        if (fourYSquaredMinus1 > fourBiggestSquaredMinus1)
        {
            fourBiggestSquaredMinus1 = fourYSquaredMinus1;
            biggestIndex = 2;
        }
        if (fourZSquaredMinus1 > fourBiggestSquaredMinus1)
        {
            fourBiggestSquaredMinus1 = fourZSquaredMinus1;
            biggestIndex = 3;
        }

        const T biggest = std::sqrt(fourBiggestSquaredMinus1 + T{ 1 }) * T{ 0.5 };
        const T mult = T{ 0.25 } / biggest;
        switch (biggestIndex)
        {
        case 0:
            return Quaternion{ (m[2, 1] - m[1, 2]) * mult, (m[0, 2] - m[2, 0]) * mult, (m[1, 0] - m[0, 1]) * mult, biggest };
        case 1:
            return Quaternion{ biggest, (m[1, 0] + m[0, 1]) * mult, (m[0, 2] + m[2, 0]) * mult, (m[2, 1] - m[1, 2]) * mult };
        case 2:
            return Quaternion{ (m[1, 0] + m[0, 1]) * mult, biggest, (m[2, 1] + m[1, 2]) * mult, (m[0, 2] - m[2, 0]) * mult };
        default:
            return Quaternion{ (m[0, 2] + m[2, 0]) * mult, (m[2, 1] + m[1, 2]) * mult, biggest, (m[1, 0] - m[0, 1]) * mult };
        }
    }

    friend constexpr auto operator==(Quaternion, Quaternion) noexcept -> bool = default;

private:
    constexpr Quaternion(T x, T y, T z, T w) noexcept :
        x{ x },
        y{ y },
        z{ z },
        w{ w }
    {
    }
};

/// @brief The Hamilton product: the rotation `b` followed by `a`
template<std::floating_point T>
[[nodiscard]] constexpr auto operator*(Quaternion<T> a, Quaternion<T> b) noexcept -> Quaternion<T>
{
    return Quaternion<T>::fromXyzw(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y + a.y * b.w + a.z * b.x - a.x * b.z,
        a.w * b.z + a.z * b.w + a.x * b.y - a.y * b.x,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

/// @brief Rotates `v` by `q`
template<std::floating_point T>
[[nodiscard]] constexpr auto operator*(Quaternion<T> q, Vector<T, 3> v) noexcept -> Vector<T, 3>
{
    const Vector<T, 3> axis{ q.x, q.y, q.z };
    const Vector<T, 3> uv = cross(axis, v);
    const Vector<T, 3> uuv = cross(axis, uv);
    return v + (uv * q.w + uuv) * T{ 2 };
}

template<std::floating_point T>
[[nodiscard]] constexpr auto dot(Quaternion<T> a, Quaternion<T> b) noexcept -> T
{
    return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
}

template<std::floating_point T>
[[nodiscard]] auto length(Quaternion<T> q) noexcept -> T
{
    return std::sqrt(dot(q, q));
}

template<std::floating_point T>
[[nodiscard]] constexpr auto conjugate(Quaternion<T> q) noexcept -> Quaternion<T>
{
    return Quaternion<T>::fromXyzw(-q.x, -q.y, -q.z, q.w);
}

/// @brief The inverse rotation; for a unit quaternion the same as conjugate()
template<std::floating_point T>
[[nodiscard]] constexpr auto inverse(Quaternion<T> q) noexcept -> Quaternion<T>
{
    const T invLengthSquared = T{ 1 } / dot(q, q);
    return Quaternion<T>::fromXyzw(-q.x * invLengthSquared, -q.y * invLengthSquared, -q.z * invLengthSquared, q.w * invLengthSquared);
}

/// @brief `q` scaled to unit length; a zero quaternion becomes identity (as in glm), so the result is a rotation
template<std::floating_point T>
[[nodiscard]] auto normalize(Quaternion<T> q) noexcept -> Quaternion<T>
{
    const T len = length(q);
    if (len <= T{ 0 })
        return Quaternion<T>::identity();
    const T invLength = T{ 1 } / len;
    return Quaternion<T>::fromXyzw(q.x * invLength, q.y * invLength, q.z * invLength, q.w * invLength);
}

/// @brief Interpolates along the shorter arc from `a` (t = 0) to `b` (t = 1), as glm does
/// @note Nearly equal inputs interpolate linearly (unnormalized) instead, where the arc's sine would vanish.
template<std::floating_point T>
[[nodiscard]] auto slerp(Quaternion<T> a, Quaternion<T> b, T t) noexcept -> Quaternion<T>
{
    T cosTheta = dot(a, b);
    if (cosTheta < T{ 0 })
    {
        b = Quaternion<T>::fromXyzw(-b.x, -b.y, -b.z, -b.w);
        cosTheta = -cosTheta;
    }

    if (cosTheta > T{ 1 } - std::numeric_limits<T>::epsilon())
        return Quaternion<T>::fromXyzw(lerp(a.x, b.x, t), lerp(a.y, b.y, t), lerp(a.z, b.z, t), lerp(a.w, b.w, t));

    const T angle = std::acos(cosTheta);
    const T sinAngle = std::sin(angle);
    const T weightA = std::sin((T{ 1 } - t) * angle) / sinAngle;
    const T weightB = std::sin(t * angle) / sinAngle;
    return Quaternion<T>::fromXyzw(
        a.x * weightA + b.x * weightB,
        a.y * weightA + b.y * weightB,
        a.z * weightA + b.z * weightB,
        a.w * weightA + b.w * weightB);
}

/// @brief The rotation matrix of a unit quaternion
template<std::floating_point T>
[[nodiscard]] constexpr auto rotationMatrix(Quaternion<T> q) noexcept -> Matrix<T, 3, 3>
{
    const T xx = q.x * q.x;
    const T yy = q.y * q.y;
    const T zz = q.z * q.z;
    const T xy = q.x * q.y;
    const T xz = q.x * q.z;
    const T yz = q.y * q.z;
    const T wx = q.w * q.x;
    const T wy = q.w * q.y;
    const T wz = q.w * q.z;
    return Matrix<T, 3, 3>::fromRows(
        Vector<T, 3>{ T{ 1 } - T{ 2 } * (yy + zz), T{ 2 } * (xy - wz), T{ 2 } * (xz + wy) },
        Vector<T, 3>{ T{ 2 } * (xy + wz), T{ 1 } - T{ 2 } * (xx + zz), T{ 2 } * (yz - wx) },
        Vector<T, 3>{ T{ 2 } * (xz - wy), T{ 2 } * (yz + wx), T{ 1 } - T{ 2 } * (xx + yy) });
}

/// @brief The Euler angles in radians (pitch around x, yaw around y, roll around z), as glm computes them
/// @note Yaw is in [-pi/2, pi/2]; at the poles (yaw = ±pi/2) roll is 0 and pitch takes the whole rotation.
template<std::floating_point T>
[[nodiscard]] auto eulerAngles(Quaternion<T> q) noexcept -> Vector<T, 3>
{
    const auto bothNearZero = [](T a, T b) {
        return abs(a) <= std::numeric_limits<T>::epsilon() && abs(b) <= std::numeric_limits<T>::epsilon();
    };

    const T pitchY = T{ 2 } * (q.y * q.z + q.w * q.x);
    const T pitchX = q.w * q.w - q.x * q.x - q.y * q.y + q.z * q.z;
    const T pitch = bothNearZero(pitchX, pitchY) ? T{ 2 } * std::atan2(q.x, q.w) : std::atan2(pitchY, pitchX);

    const T yaw = std::asin(clamp(T{ -2 } * (q.x * q.z - q.w * q.y), T{ -1 }, T{ 1 }));

    const T rollY = T{ 2 } * (q.x * q.y + q.w * q.z);
    const T rollX = q.w * q.w + q.x * q.x - q.y * q.y - q.z * q.z;
    const T roll = bothNearZero(rollX, rollY) ? T{ 0 } : std::atan2(rollY, rollX);

    return { pitch, yaw, roll };
}

/// @brief Whether every component pair is nearlyEqual (see the scalar overload)
/// @note Compares components, so `q` and `-q`, the same rotation, are not nearly equal.
template<std::floating_point T>
[[nodiscard]] constexpr auto nearlyEqual(Quaternion<T> a, Quaternion<T> b, T tolerance = defaultTolerance<T>) noexcept -> bool
{
    return nearlyEqual(a.x, b.x, tolerance) && nearlyEqual(a.y, b.y, tolerance) && nearlyEqual(a.z, b.z, tolerance) &&
           nearlyEqual(a.w, b.w, tolerance);
}
}
