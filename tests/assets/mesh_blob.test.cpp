#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.math;

namespace
{
using namespace aegis;
using assets::ErrorCode;

auto quad() -> assets::MeshData
{
    auto mesh = assets::MeshPreprocessor::process({
        .positions = { { 0, 0, 0 }, { 1, 0, 0 }, { 0, 1, 0 }, { 1, 1, 0 } },
        .uvs = { { 0, 1 }, { 1, 1 }, { 0, 0 }, { 1, 0 } },
        .indices = { 0, 1, 2, 2, 1, 3 },
    });
    REQUIRE(mesh);
    return std::move(*mesh);
}

auto sameBytes(const auto& a, const auto& b) -> bool
{
    return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(a[0])) == 0);
}
}

TEST_SUITE("assets::MeshBlob")
{
    TEST_CASE("a mesh round-trips through a blob")
    {
        const auto mesh = quad();
        const auto blob = assets::writeMeshBlob(mesh, 0x0123456789abcdef);
        const auto read = assets::readMeshBlob(blob);
        REQUIRE(read);
        CHECK(sameBytes(read->vertices, mesh.vertices));
        CHECK(read->indices == mesh.indices);
        CHECK(sameBytes(read->meshlets, mesh.meshlets));
        CHECK(read->meshletVertices == mesh.meshletVertices);
        CHECK(read->meshletPrimitives == mesh.meshletPrimitives);
        CHECK(read->bounds.center.x == mesh.bounds.center.x);
        CHECK(read->bounds.center.y == mesh.bounds.center.y);
        CHECK(read->bounds.center.z == mesh.bounds.center.z);
        CHECK(read->bounds.radius == mesh.bounds.radius);
    }

    TEST_CASE("the header holds the key and the counts")
    {
        const auto mesh = quad();
        const auto header = assets::readMeshBlobHeader(assets::writeMeshBlob(mesh, 42));
        REQUIRE(header);
        CHECK(header->magic == assets::meshBlobMagic);
        CHECK(header->type == assets::meshBlobTypeStaticMesh);
        CHECK(header->version == assets::meshBlobVersion);
        CHECK(header->key == 42);
        CHECK(header->vertexCount == mesh.vertices.size());
        CHECK(header->indexCount == mesh.indices.size());
        CHECK(header->meshletCount == mesh.meshlets.size());
        CHECK(header->meshletVertexCount == mesh.meshletVertices.size());
        CHECK(header->meshletPrimitiveBytes == mesh.meshletPrimitives.size());
    }

    TEST_CASE("every array starts aligned")
    {
        const assets::MeshBlobHeader header{ .vertexCount = 3, .indexCount = 3, .meshletCount = 1, .meshletVertexCount = 3, .meshletPrimitiveBytes = 3 };
        const auto layout = assets::meshBlobLayout(header);
        for (const auto offset : { layout.vertices, layout.indices, layout.meshlets, layout.meshletVertices, layout.meshletPrimitives, layout.size })
            CHECK(offset % assets::meshBlobAlignment == 0);
        CHECK(layout.vertices >= sizeof(assets::MeshBlobHeader));
        CHECK(layout.indices >= layout.vertices + 3 * sizeof(assets::Vertex));
        CHECK(layout.size >= layout.meshletPrimitives + 3);
    }

    TEST_CASE("writing the same mesh gives the same bytes")
    {
        const auto mesh = quad();
        CHECK(assets::writeMeshBlob(mesh, 7) == assets::writeMeshBlob(mesh, 7));
    }

    TEST_CASE("an empty mesh round-trips")
    {
        const auto read = assets::readMeshBlob(assets::writeMeshBlob({}, 1));
        REQUIRE(read);
        CHECK(read->vertices.empty());
        CHECK(read->meshlets.empty());
    }

    TEST_CASE("a short blob is rejected")
    {
        const std::vector<std::byte> blob(16);
        CHECK(assets::readMeshBlob(blob).error().code == ErrorCode::DecodeFailed);
    }

    TEST_CASE("another magic is rejected")
    {
        auto blob = assets::writeMeshBlob(quad(), 1);
        blob[0] = std::byte{ 'X' };
        CHECK(assets::readMeshBlob(blob).error().code == ErrorCode::DecodeFailed);
    }

    TEST_CASE("another version is rejected")
    {
        auto blob = assets::writeMeshBlob(quad(), 1);
        const auto version = assets::meshBlobVersion + 1;
        std::memcpy(blob.data() + 8, &version, sizeof(version)); // After magic and type
        CHECK(assets::readMeshBlob(blob).error().code == ErrorCode::UnsupportedFormat);
    }

    TEST_CASE("a truncated blob is rejected")
    {
        auto blob = assets::writeMeshBlob(quad(), 1);
        blob.resize(blob.size() - assets::meshBlobAlignment);
        CHECK(assets::readMeshBlob(blob).error().code == ErrorCode::InvalidData);
    }
}
