#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

import aegis.assets;
import Aegis.Math;

namespace
{
using aegis::assets::ErrorCode;
using aegis::assets::MeshPreprocessor;

// Two triangles of a unit quad, unindexed: 6 corners, 4 distinct vertices.
auto quad() -> MeshPreprocessor::Input
{
    MeshPreprocessor::Input input;
    const std::vector<glm::vec3> corners{
        { 0, 0, 0 },
        { 1, 0, 0 },
        { 1, 1, 0 },
        { 0, 0, 0 },
        { 1, 1, 0 },
        { 0, 1, 0 },
    };
    input.positions = corners;
    input.normals.assign(corners.size(), glm::vec3{ 0, 0, 1 });
    return input;
}
}

TEST_SUITE("assets::MeshPreprocessor")
{
    TEST_CASE("merges identical vertices and indexes them")
    {
        auto mesh = MeshPreprocessor::process(quad());
        REQUIRE(mesh.has_value());
        CHECK(mesh->vertices.size() == 4);
        CHECK(mesh->indices.size() == 6);
        for (const auto index : mesh->indices)
            CHECK(index < mesh->vertices.size());
    }

    TEST_CASE("meshlets cover every triangle exactly once")
    {
        auto mesh = MeshPreprocessor::process(quad());
        REQUIRE(mesh.has_value());
        REQUIRE(!mesh->meshlets.empty());

        std::size_t triangles{ 0 };
        for (const auto& meshlet : mesh->meshlets)
            triangles += meshlet.primitiveCount;
        CHECK(triangles == mesh->indices.size() / 3);
        CHECK(mesh->meshletPrimitives.size() == 3 * triangles);
    }

    TEST_CASE("the bounding sphere contains every vertex")
    {
        auto mesh = MeshPreprocessor::process(quad());
        REQUIRE(mesh.has_value());
        for (const auto& vertex : mesh->vertices)
            CHECK(glm::length(vertex.position - mesh->bounds.center) <= mesh->bounds.radius + 1e-4f);
    }

    TEST_CASE("missing uvs and colors get defaults")
    {
        auto mesh = MeshPreprocessor::process(quad());
        REQUIRE(mesh.has_value());
        for (const auto& vertex : mesh->vertices)
        {
            CHECK(vertex.uv.x == 0.0f);
            CHECK(vertex.uv.y == 0.0f);
            CHECK(vertex.color.x == 1.0f);
            CHECK(vertex.color.z == 1.0f);
        }
    }

    TEST_CASE("an indexed mesh keeps its triangles")
    {
        MeshPreprocessor::Input input;
        input.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } };
        input.normals.assign(4, glm::vec3{ 0, 0, 1 });
        input.indices = { 0, 1, 2, 0, 2, 3 };

        auto mesh = MeshPreprocessor::process(input);
        REQUIRE(mesh.has_value());
        CHECK(mesh->vertices.size() == 4);
        CHECK(mesh->indices.size() == 6);
    }

    TEST_CASE("rejects input it cannot turn into triangles")
    {
        SUBCASE("no positions")
        {
            auto result = MeshPreprocessor::process({});
            REQUIRE(!result.has_value());
            CHECK(result.error().code == ErrorCode::InvalidData);
        }

        SUBCASE("a normal count that does not match the positions")
        {
            auto input = quad();
            input.normals.pop_back();
            auto result = MeshPreprocessor::process(input);
            REQUIRE(!result.has_value());
            CHECK(result.error().code == ErrorCode::InvalidData);
        }

        SUBCASE("an index count that is not a multiple of 3")
        {
            auto input = quad();
            input.indices = { 0, 1, 2, 3 };
            auto result = MeshPreprocessor::process(input);
            REQUIRE(!result.has_value());
            CHECK(result.error().code == ErrorCode::InvalidData);
        }

        SUBCASE("an index past the last vertex")
        {
            auto input = quad();
            input.indices = { 0, 1, 6 };
            auto result = MeshPreprocessor::process(input);
            REQUIRE(!result.has_value());
            CHECK(result.error().code == ErrorCode::InvalidData);
        }
    }
}
