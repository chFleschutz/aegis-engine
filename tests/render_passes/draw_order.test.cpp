#include <doctest/doctest.h>

import std;
import aegis.math;
import aegis.render_passes;

namespace math = aegis::math;

namespace
{
using namespace aegis::render_graph;

auto indices(std::span<const detail::DepthSortedDraw> draws) -> std::vector<std::uint32_t>
{
    std::vector<std::uint32_t> result;
    for (const auto& draw : draws)
        result.push_back(draw.index);
    return result;
}
}

TEST_SUITE("render_graph::draw_order")
{
    TEST_CASE("viewDepth measures along the camera's view direction")
    {
        const auto view = aegis::math::viewMatrix(math::Float3{ 1.0f, 2.0f, 3.0f }, math::Float3{ 0.0f, 1.0f, 0.0f });
        CHECK(detail::viewDepth(view, math::Float3{ 1.0f, 7.0f, 3.0f }) == doctest::Approx(5.0f));
        // Sideways offsets leave the depth unchanged; behind the camera it is negative.
        CHECK(detail::viewDepth(view, math::Float3{ 4.0f, 7.0f, 1.0f }) == doctest::Approx(5.0f));
        CHECK(detail::viewDepth(view, math::Float3{ 1.0f, -1.0f, 3.0f }) == doctest::Approx(-3.0f));
    }

    TEST_CASE("worldCenter moves the bounding sphere's centre by the model matrix")
    {
        // Scaled by 2 and moved by (10, 20, 30): rows of the model matrix with the translation in .w.
        const auto model = aegis::math::fromRows(
            math::Float4{ 2.0f, 0.0f, 0.0f, 10.0f },
            math::Float4{ 0.0f, 2.0f, 0.0f, 20.0f },
            math::Float4{ 0.0f, 0.0f, 2.0f, 30.0f });
        const auto center = detail::worldCenter(model, math::Float3{ 1.0f, -1.0f, 0.5f });
        CHECK(center.x == doctest::Approx(12.0f));
        CHECK(center.y == doctest::Approx(18.0f));
        CHECK(center.z == doctest::Approx(31.0f));
    }

    TEST_CASE("sortBackToFront draws the deepest first")
    {
        std::vector<detail::DepthSortedDraw> draws{
            { .depth = 1.0f, .index = 0 },
            { .depth = 5.0f, .index = 1 },
            { .depth = -2.0f, .index = 2 },
            { .depth = 3.0f, .index = 3 },
        };
        detail::sortBackToFront(draws);
        CHECK(indices(draws) == std::vector<std::uint32_t>{ 1, 3, 0, 2 });
    }

    TEST_CASE("sortBackToFront keeps the order of draws at equal depth")
    {
        std::vector<detail::DepthSortedDraw> draws{
            { .depth = 2.0f, .index = 0 },
            { .depth = 4.0f, .index = 1 },
            { .depth = 2.0f, .index = 2 },
            { .depth = 4.0f, .index = 3 },
            { .depth = 2.0f, .index = 4 },
        };
        detail::sortBackToFront(draws);
        CHECK(indices(draws) == std::vector<std::uint32_t>{ 1, 3, 0, 2, 4 });
    }

    TEST_CASE("sortBackToFront orders draws seen from the other side in reverse")
    {
        // Three centres along x, seen from the +x and the -x side.
        const std::array centers{ math::Float3{ 0.0f }, math::Float3{ 1.0f, 0.0f, 0.0f }, math::Float3{ 2.0f, 0.0f, 0.0f } };
        const auto order = [&](const math::Float3& position, const math::Float3& forward) {
            const auto view = aegis::math::viewMatrix(position, forward);
            std::vector<detail::DepthSortedDraw> draws;
            for (std::uint32_t i = 0; i < centers.size(); ++i)
                draws.push_back({ .depth = detail::viewDepth(view, centers[i]), .index = i });
            detail::sortBackToFront(draws);
            return indices(draws);
        };
        CHECK(order(math::Float3{ 10.0f, 0.0f, 0.0f }, math::Float3{ -1.0f, 0.0f, 0.0f }) == std::vector<std::uint32_t>{ 0, 1, 2 });
        CHECK(order(math::Float3{ -10.0f, 0.0f, 0.0f }, math::Float3{ 1.0f, 0.0f, 0.0f }) == std::vector<std::uint32_t>{ 2, 1, 0 });
    }
}
