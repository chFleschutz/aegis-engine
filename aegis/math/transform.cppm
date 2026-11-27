module;

#include <cassert>

export module aegis.math:transform;

import std;
import :scalar;
import :vector;
import :matrix;
import :quaternion;

export namespace aegis::math
{
/// @brief The engine's world axes: right handed, x right, y forward, z up
namespace world
{
template<std::floating_point T>
inline constexpr Vector<T, 3> right{ T{ 1 }, T{ 0 }, T{ 0 } };

template<std::floating_point T>
inline constexpr Vector<T, 3> forward{ T{ 0 }, T{ 1 }, T{ 0 } };

template<std::floating_point T>
inline constexpr Vector<T, 3> up{ T{ 0 }, T{ 0 }, T{ 1 } };
}

/// @brief The forward direction of a rotation: world::forward rotated by `rotation`
template<std::floating_point T>
[[nodiscard]] constexpr auto forward(Quaternion<T> rotation) noexcept -> Vector<T, 3>
{
    return rotation * world::forward<T>;
}

/// @brief The right direction of a rotation: world::right rotated by `rotation`
template<std::floating_point T>
[[nodiscard]] constexpr auto right(Quaternion<T> rotation) noexcept -> Vector<T, 3>
{
    return rotation * world::right<T>;
}

/// @brief The up direction of a rotation: world::up rotated by `rotation`
template<std::floating_point T>
[[nodiscard]] constexpr auto up(Quaternion<T> rotation) noexcept -> Vector<T, 3>
{
    return rotation * world::up<T>;
}

/// @brief Composes a transformation matrix from location, rotation and scale: scale first, then rotate, then move
template<std::floating_point T>
[[nodiscard]] constexpr auto transformMatrix(Vector<T, 3> location, Quaternion<T> rotation, Vector<T, 3> scale) noexcept
    -> Matrix<T, 4, 4>
{
    const Matrix<T, 3, 3> r = rotationMatrix(rotation);
    return Matrix<T, 4, 4>::fromColumns(
        Vector<T, 4>{ r.column(0) * scale.x, T{ 0 } },
        Vector<T, 4>{ r.column(1) * scale.y, T{ 0 } },
        Vector<T, 4>{ r.column(2) * scale.z, T{ 0 } },
        Vector<T, 4>{ location, T{ 1 } });
}

/// @brief The parts of a transformation matrix, as decomposeTRS returns them
template<std::floating_point T>
struct DecomposedTransform
{
    Vector<T, 3> location;
    Quaternion<T> rotation;
    Vector<T, 3> scale;
};

/// @brief Decomposes a transformation matrix into location, rotation and scale
/// @note A mirrored matrix (negative determinant) keeps its mirror as a negative x scale, so that
/// transformMatrix(location, rotation, scale) reproduces it. The rotation is always a unit quaternion; if an axis has
/// zero length the rotation is undefined and is the identity.
template<std::floating_point T>
[[nodiscard]] auto decomposeTRS(const Matrix<T, 4, 4>& matrix) noexcept -> DecomposedTransform<T>
{
    const Vector<T, 3> location{ matrix.column(3) };
    Vector<T, 3> scale{
        length(Vector<T, 3>{ matrix.column(0) }),
        length(Vector<T, 3>{ matrix.column(1) }),
        length(Vector<T, 3>{ matrix.column(2) }),
    };
    if (scale.x == T{ 0 } || scale.y == T{ 0 } || scale.z == T{ 0 })
        return { location, Quaternion<T>::identity(), scale };

    auto rotation = Matrix<T, 3, 3>{ matrix };
    rotation.setColumn(0, rotation.column(0) / scale.x);
    rotation.setColumn(1, rotation.column(1) / scale.y);
    rotation.setColumn(2, rotation.column(2) / scale.z);
    if (determinant(rotation) < T{ 0 })
    {
        scale.x = -scale.x;
        rotation.setColumn(0, -rotation.column(0));
    }
    return { location, normalize(Quaternion<T>::fromMatrix(rotation)), scale };
}

/// @brief Builds a right handed view matrix for a camera at `location` looking along `forward`, with world::up as up
/// @note `forward` must not be parallel to world::up (asserted): there is no sideways axis then, and every element
///       would be NaN.
template<std::floating_point T>
[[nodiscard]] auto viewMatrix(Vector<T, 3> location, Vector<T, 3> forward) noexcept -> Matrix<T, 4, 4>
{
    const Vector<T, 3> f = normalize(forward);
    assert(length(cross(f, world::up<T>)) > T{ 1e-6 } && "viewMatrix: forward is parallel to world::up (z); look along y");
    const Vector<T, 3> s = normalize(cross(f, world::up<T>)); // Sideways
    const Vector<T, 3> u = cross(s, f);
    return Matrix<T, 4, 4>::fromRows(
        Vector<T, 4>{ s, -dot(s, location) },
        Vector<T, 4>{ u, -dot(u, location) },
        Vector<T, 4>{ -f, dot(f, location) },
        Vector<T, 4>{ T{ 0 }, T{ 0 }, T{ 0 }, T{ 1 } });
}

/// @brief Builds the inverse of viewMatrix(location, forward): the camera's own transform
/// @note `forward` must not be parallel to world::up (asserted), as for viewMatrix.
template<std::floating_point T>
[[nodiscard]] auto inverseViewMatrix(Vector<T, 3> location, Vector<T, 3> forward) noexcept -> Matrix<T, 4, 4>
{
    const Vector<T, 3> f = normalize(forward);
    assert(length(cross(f, world::up<T>)) > T{ 1e-6 } && "inverseViewMatrix: forward is parallel to world::up (z); look along y");
    const Vector<T, 3> s = normalize(cross(f, world::up<T>));
    const Vector<T, 3> u = cross(s, f);
    return Matrix<T, 4, 4>::fromColumns(
        Vector<T, 4>{ s, T{ 0 } },
        Vector<T, 4>{ u, T{ 0 } },
        Vector<T, 4>{ -f, T{ 0 } },
        Vector<T, 4>{ location, T{ 1 } });
}

/// @brief A right handed perspective projection into Vulkan clip space: depth 0 at `near` to 1 at `far`, y down
/// @param fovY The vertical field of view in radians
/// @param aspect Width over height
template<std::floating_point T>
[[nodiscard]] auto perspective(T fovY, T aspect, T near, T far) noexcept -> Matrix<T, 4, 4>
{
    const T tanHalfFovY = std::tan(fovY / T{ 2 });
    auto projection = Matrix<T, 4, 4>::zero();
    projection[0, 0] = T{ 1 } / (aspect * tanHalfFovY);
    projection[1, 1] = T{ -1 } / tanHalfFovY;
    projection[2, 2] = far / (near - far);
    projection[2, 3] = -(far * near) / (far - near);
    projection[3, 2] = T{ -1 };
    return projection;
}

/// @brief A right handed orthographic projection of a box centred on the view axis into Vulkan clip space: x in
/// [-halfWidth, halfWidth], y in [-halfHeight, halfHeight] (flipped to y down), depth 0 at `near` to 1 at `far`
template<std::floating_point T>
[[nodiscard]] constexpr auto orthographic(T halfWidth, T halfHeight, T near, T far) noexcept -> Matrix<T, 4, 4>
{
    auto projection = Matrix<T, 4, 4>::identity();
    projection[0, 0] = T{ 1 } / halfWidth;
    projection[1, 1] = T{ -1 } / halfHeight;
    projection[2, 2] = T{ -1 } / (far - near);
    projection[2, 3] = (T{ 0 } - near) / (far - near); // +0 at near = 0, as the hand-written sun shadow projection had
    return projection;
}
}
