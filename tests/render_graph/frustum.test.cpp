#include <doctest/doctest.h>

import aegis.render_graph;
import aegis.math;

TEST_SUITE("render_graph::Frustum")
{
    using aegis::render_graph::Frustum;

    // With an identity view-projection the frustum is clip space: x, y in [-1, 1], z in [0, 1].
    const auto clipSpace = Frustum::extractFrom(glm::mat4{ 1.0f });

    TEST_CASE("a sphere inside is visible")
    {
        CHECK(clipSpace.intersects({ 0.0f, 0.0f, 0.5f }, 0.1f));
    }

    TEST_CASE("a sphere entirely outside one plane is culled")
    {
        CHECK(!clipSpace.intersects({ 3.0f, 0.0f, 0.5f }, 1.0f));  // Right of x = 1
        CHECK(!clipSpace.intersects({ 0.0f, 0.0f, -2.0f }, 1.0f)); // In front of the near plane z = 0
        CHECK(!clipSpace.intersects({ 0.0f, 0.0f, 3.0f }, 1.0f));  // Beyond the far plane z = 1
    }

    TEST_CASE("a sphere straddling a plane is visible")
    {
        CHECK(clipSpace.intersects({ 1.5f, 0.0f, 0.5f }, 1.0f));
    }

    TEST_CASE("plane normals are unit length")
    {
        const auto scaled = Frustum::extractFrom(glm::mat4{ 4.0f });
        for (const auto& plane : scaled.planes)
            CHECK(glm::length(glm::vec3{ plane }) == doctest::Approx(1.0f));
    }
}
