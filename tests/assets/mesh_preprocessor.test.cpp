#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;

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

// Signed area (z of the cross product, halved) of each output triangle, in the XY plane.
// Positive is counterclockwise seen from +Z.
auto signedAreas(const aegis::assets::MeshData& mesh) -> std::vector<float>
{
    std::vector<float> areas;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
    {
        const auto a = mesh.vertices[mesh.indices[i + 0]].position;
        const auto b = mesh.vertices[mesh.indices[i + 1]].position;
        const auto c = mesh.vertices[mesh.indices[i + 2]].position;
        areas.push_back(0.5f * glm::cross(b - a, c - a).z);
    }
    return areas;
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

    TEST_CASE("a triangle strip becomes a list with one winding")
    {
        // A 2x1 ribbon in strip order: 6 vertices, 4 clockwise triangles. Read as a list, the same
        // indices would give 2 triangles of opposite winding.
        MeshPreprocessor::Input input;
        input.positions = { { 0, 0, 0 }, { 0, 1, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 2, 0, 0 }, { 2, 1, 0 } };
        input.normals.assign(6, glm::vec3{ 0, 0, -1 });
        input.topology = MeshPreprocessor::Topology::TriangleStrip;

        SUBCASE("unindexed") {}
        SUBCASE("indexed")
        {
            input.indices = { 0, 1, 2, 3, 4, 5 };
        }

        auto mesh = MeshPreprocessor::process(input);
        REQUIRE(mesh.has_value());
        const auto areas = signedAreas(*mesh);
        CHECK(areas.size() == 4);
        for (const auto area : areas)
            CHECK(area == doctest::Approx(-0.5f));
    }

    TEST_CASE("a triangle fan becomes a list around its first vertex")
    {
        MeshPreprocessor::Input input;
        input.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 }, { -1, 1, 0 } };
        input.normals.assign(5, glm::vec3{ 0, 0, 1 });
        input.indices = { 0, 1, 2, 3, 4 };
        input.topology = MeshPreprocessor::Topology::TriangleFan;

        auto mesh = MeshPreprocessor::process(input);
        REQUIRE(mesh.has_value());
        const auto areas = signedAreas(*mesh);
        CHECK(areas.size() == 3);
        for (const auto area : areas)
            CHECK(area == doctest::Approx(0.5f));
    }

    TEST_CASE("generates flat normals when the input has none")
    {
        // An indexed quad whose two triangles share two vertices: flat normals split them from the other triangle's.
        MeshPreprocessor::Input input;
        input.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 1 } };
        input.indices = { 0, 1, 2, 0, 2, 3 };

        auto mesh = MeshPreprocessor::process(input);
        REQUIRE(mesh.has_value());
        CHECK(mesh->vertices.size() == 6);
        REQUIRE(mesh->indices.size() == 6);
        for (std::size_t i = 0; i < mesh->indices.size(); i += 3)
        {
            const auto& a = mesh->vertices[mesh->indices[i + 0]];
            const auto& b = mesh->vertices[mesh->indices[i + 1]];
            const auto& c = mesh->vertices[mesh->indices[i + 2]];
            const auto expected = glm::normalize(glm::cross(b.position - a.position, c.position - a.position));
            for (const auto* vertex : { &a, &b, &c })
            {
                CHECK(vertex->normal.x == doctest::Approx(expected.x));
                CHECK(vertex->normal.y == doctest::Approx(expected.y));
                CHECK(vertex->normal.z == doctest::Approx(expected.z));
            }
        }
    }

    TEST_CASE("a degenerate triangle gets a unit normal")
    {
        MeshPreprocessor::Input input;
        input.positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 2, 0, 0 } };

        auto mesh = MeshPreprocessor::process(input);
        REQUIRE(mesh.has_value());
        for (const auto& vertex : mesh->vertices)
            CHECK(glm::length(vertex.normal) == doctest::Approx(1.0f));
    }

    TEST_CASE("rejects meshlet limits the mesh shader cannot hold")
    {
        // The mesh shader declares at most 64 vertices and 126 primitives per meshlet; a larger meshlet
        // would be undefined behavior on the GPU, and a count above 255 would wrap in the uint8 fields.
        const auto rejects = [](std::size_t maxVertices, std::size_t maxTriangles) {
            auto input = quad();
            input.maxVerticesPerMeshlet = maxVertices;
            input.maxTrianglesPerMeshlet = maxTriangles;
            auto result = MeshPreprocessor::process(input);
            return !result.has_value() && result.error().code == ErrorCode::InvalidData;
        };

        CHECK(rejects(64, 128)); // meshoptimizer's usual 128 triangles
        CHECK(rejects(64, 127));
        CHECK(rejects(65, 126));
        CHECK(rejects(256, 126));
        CHECK(rejects(64, 0));
        CHECK(rejects(0, 126));
        CHECK(rejects(2, 126)); // meshoptimizer needs room for a triangle's 3 vertices
    }

    TEST_CASE("accepts smaller meshlet limits")
    {
        auto input = quad();
        input.maxVerticesPerMeshlet = 32;
        input.maxTrianglesPerMeshlet = 32;
        auto mesh = MeshPreprocessor::process(input);
        REQUIRE(mesh.has_value());
        for (const auto& meshlet : mesh->meshlets)
        {
            CHECK(meshlet.vertexCount <= 32);
            CHECK(meshlet.primitiveCount <= 32);
        }
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

        SUBCASE("a strip with fewer than 3 indices")
        {
            auto input = quad();
            input.indices = { 0, 1 };
            input.topology = MeshPreprocessor::Topology::TriangleStrip;
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
