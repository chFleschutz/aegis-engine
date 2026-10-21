#include <doctest/doctest.h>

import std;
import aegis.assets;

using aegis::assets::AssetId;
using aegis::assets::ErrorCode;
using aegis::assets::Sidecar;

namespace
{
// The file's text without carriage returns, which a Windows checkout may add.
auto readGolden() -> std::string
{
    std::ifstream stream{ std::filesystem::path{ AEGIS_TEST_GOLDEN_DIR } / "Helmet.gltf.meta", std::ios::binary };
    std::string text;
    for (char c; stream.get(c);)
    {
        if (c != '\r')
            text.push_back(c);
    }
    return text;
}
}

TEST_SUITE("assets::Sidecar")
{
    TEST_CASE("the golden sidecar keeps loading unchanged")
    {
        const auto text = readGolden();
        REQUIRE_FALSE(text.empty());

        const auto sidecar = aegis::assets::parseSidecar(text);
        REQUIRE(sidecar);
        CHECK(sidecar->id == AssetId{ 0xa3f09c2e4b1d7788ULL });
        CHECK(sidecar->importer == "gltf");
        CHECK(sidecar->importerVersion == 1);
        CHECK(sidecar->settings.upAxis == "Y");
        CHECK(sidecar->settings.handedness == "Right");
        CHECK(sidecar->settings.scale == 1.0);
        CHECK_FALSE(sidecar->settings.strictNames);
        REQUIRE(sidecar->subAssets.size() == 3);
        CHECK(sidecar->subAssets.at("StaticMesh/Helmet/0") == AssetId{ 0x1f6b2c0d93ae5547ULL });
        CHECK(sidecar->subAssets.at("Material/HelmetMat") == AssetId{ 0xc4d8e1a07b2f9036ULL });
        CHECK(sidecar->subAssets.at("Texture/Helmet_albedo.png") == AssetId{ 0x08e3f5b6a1c47d29ULL });
    }

    TEST_CASE("the golden sidecar is written back byte for byte")
    {
        const auto text = readGolden();
        const auto sidecar = aegis::assets::parseSidecar(text);
        REQUIRE(sidecar);
        CHECK(aegis::assets::writeSidecar(*sidecar) == text);
    }

    TEST_CASE("a sidecar round-trips")
    {
        Sidecar sidecar;
        sidecar.id = AssetId::generate();
        sidecar.importer = "texture";
        sidecar.settings.scale = 0.01;
        sidecar.settings.strictNames = true;
        sidecar.subAssetId("Texture/a");
        sidecar.subAssetId("Texture/b");

        const auto parsed = aegis::assets::parseSidecar(aegis::assets::writeSidecar(sidecar));
        REQUIRE(parsed);
        CHECK(*parsed == sidecar);
    }

    TEST_CASE("a part keeps its ID when the parts are reordered")
    {
        Sidecar sidecar;
        sidecar.id = AssetId::generate();
        const auto first = sidecar.subAssetId("Material/A");
        const auto second = sidecar.subAssetId("Material/B");
        CHECK(sidecar.subAssetId("Material/B") == second);
        CHECK(sidecar.subAssetId("Material/A") == first);
        CHECK(first != second);
    }

    TEST_CASE("entries of removed parts stay in the table")
    {
        Sidecar sidecar;
        sidecar.id = AssetId::generate();
        const auto removed = sidecar.subAssetId("Material/Gone");
        sidecar.subAssetId("Material/Other");
        auto parsed = aegis::assets::parseSidecar(aegis::assets::writeSidecar(sidecar));
        REQUIRE(parsed);
        CHECK(parsed->subAssetId("Material/Gone") == removed);
    }

    TEST_CASE("a malformed ID is rejected")
    {
        const auto upper = aegis::assets::parseSidecar(R"({"id":"A3F09C2E4B1D7788","importer":"gltf"})");
        REQUIRE_FALSE(upper);
        CHECK(upper.error().code == ErrorCode::InvalidData);

        CHECK_FALSE(aegis::assets::parseSidecar(R"({"id":"a3f09c","importer":"gltf"})"));
        CHECK_FALSE(aegis::assets::parseSidecar(
            R"({"id":"a3f09c2e4b1d7788","importer":"gltf","subAssets":{"Material/A":"xyz"}})"));
    }

    TEST_CASE("broken JSON is a decode error")
    {
        const auto parsed = aegis::assets::parseSidecar("{ not json");
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().code == ErrorCode::DecodeFailed);
    }

    TEST_CASE("comments and unknown keys are accepted on read")
    {
        const auto parsed = aegis::assets::parseSidecar(R"({
            // hand-edited
            "id": "a3f09c2e4b1d7788", "importer": "gltf", "future": 1
        })");
        CHECK(parsed);
    }

    TEST_CASE("the sidecar of a file is its full name plus meta")
    {
        CHECK(aegis::assets::sidecarPath("assets/Helmet.gltf").generic_string() == "assets/Helmet.gltf.meta");
        CHECK(aegis::assets::sidecarPath("assets/Helmet.png").generic_string() == "assets/Helmet.png.meta");
    }

    TEST_CASE("a native header reads type id and version")
    {
        const auto header = aegis::assets::parseNativeHeader(
            R"({"type":"Material","id":"5b0e17d2c94af3a1","version":1,"template":"2e7a90c4d15b8f63"})");
        REQUIRE(header);
        CHECK(header->type == "Material");
        CHECK(header->id == AssetId{ 0x5b0e17d2c94af3a1ULL });
        CHECK(header->version == 1);
        CHECK_FALSE(aegis::assets::parseNativeHeader(R"({"id":"5b0e17d2c94af3a1"})"));
    }
}
