module;
#include <array>

export module aegis.render_graph:frustum;
import aegis.math;

export namespace aegis::render_graph
{
/// @brief Six planes (left, right, bottom, top, near, far), each { A, B, C, D } with Ax + By + Cz + D >= 0
///        inside and the normal (A, B, C) unit length. Laid out as shaders read it.
struct Frustum
{
    using Plane = glm::vec4;

    std::array<Plane, 6> planes;

    /// @brief Extracts the planes of a view-projection matrix (Gribb/Hartmann), for Vulkan's [0, 1]
    ///        depth range.
    [[nodiscard]] static auto extractFrom(const glm::mat4& viewProjection) -> Frustum
    {
        const auto row0 = glm::row(viewProjection, 0);
        const auto row1 = glm::row(viewProjection, 1);
        const auto row2 = glm::row(viewProjection, 2);
        const auto row3 = glm::row(viewProjection, 3);

        Frustum frustum{ {
            row3 + row0,
            row3 - row0,
            row3 + row1,
            row3 - row1,
            row2, // Near: z >= 0 in Vulkan's depth range
            row3 - row2,
        } };

        for (auto& plane : frustum.planes)
            plane /= glm::length(glm::vec3{ plane });
        return frustum;
    }

    /// @brief Whether a sphere is at least partly inside.
    [[nodiscard]] auto intersects(const glm::vec3& center, float radius) const -> bool
    {
        for (const auto& plane : planes)
        {
            if (glm::dot(glm::vec3{ plane }, center) + plane.w < -radius)
                return false;
        }
        return true;
    }
};
}
