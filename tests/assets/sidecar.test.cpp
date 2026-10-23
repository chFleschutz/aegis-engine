#include <doctest/doctest.h>

import std;
import aegis.assets;

using aegis::assets::AssetId;
using aegis::assets::ErrorCode;
using aegis::assets::Sidecar;

namespace
{
// The golden file's text without carriage returns, which a Windows checkout may add.
auto readGolden(std::string_view name = "Helmet.gltf.meta") -> std::string
{
    std::ifstream stream{ std::filesystem::path{ AEGIS_TEST_GOLDEN_DIR } / name, std::ios::binary };
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
        CHECK(sidecar->subAssets.at("Texture/Helmet_albedo") == AssetId{ 0x08e3f5b6a1c47d29ULL });
    }

    TEST_CASE("the golden sidecar is written back byte for byte")
    {
        const auto text = readGolden();
        const auto sidecar = aegis::assets::parseSidecar(text);
        REQUIRE(sidecar);
        CHECK(aegis::assets::writeSidecar(*sidecar) == text);
    }

    TEST_CASE("the golden sidecar with part settings keeps loading unchanged")
    {
        const auto text = readGolden("Embedded.glb.meta");
        REQUIRE_FALSE(text.empty());

        const auto sidecar = aegis::assets::parseSidecar(text);
        REQUIRE(sidecar);
        CHECK(sidecar->id == AssetId{ 0x5e21c7a9d04b8f36ULL });
        REQUIRE(sidecar->subAssets.size() == 3);
        CHECK(sidecar->subAssets.at("Texture/Helmet_albedo") == AssetId{ 0x08e3f5b6a1c47d29ULL });
        CHECK(sidecar->subAssets.at("Texture/Helmet_normal") == AssetId{ 0x7a1c90e2b35f4d68ULL });
        REQUIRE(sidecar->partSettings.size() == 1);
        const auto& settings = sidecar->partSettings.at("Texture/Helmet_normal");
        CHECK(settings.srgb == true);
        CHECK(settings.upAxis == "Y");
        CHECK(settings.scale == 1.0);
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

    TEST_CASE("a sidecar with part settings round-trips")
    {
        Sidecar sidecar;
        sidecar.id = AssetId::generate();
        sidecar.importer = "gltf";
        sidecar.subAssetId("Texture/plain");
        sidecar.subAssetId("Texture/linear");
        sidecar.subAssetId("Texture/scaled");
        sidecar.partSettings["Texture/linear"].srgb = false;
        sidecar.partSettings["Texture/scaled"].scale = 2.0;

        const auto text = aegis::assets::writeSidecar(sidecar);
        const auto parsed = aegis::assets::parseSidecar(text);
        REQUIRE(parsed);
        CHECK(*parsed == sidecar);
        CHECK_FALSE(parsed->partSettings.contains("Texture/plain"));
        CHECK(text.contains(std::format(R"("Texture/plain": "{}")", sidecar.subAssets.at("Texture/plain").toString())));
    }

    TEST_CASE("a part entry writes only the settings that differ from the defaults")
    {
        Sidecar sidecar;
        sidecar.id = AssetId{ 1 };
        sidecar.subAssets.emplace("Texture/normal", AssetId{ 2 });
        sidecar.partSettings["Texture/normal"].srgb = true;
        const auto text = aegis::assets::writeSidecar(sidecar);
        const auto entry = text.substr(text.find("\"subAssets\""));
        CHECK(entry.contains(R"("srgb": true)"));
        CHECK_FALSE(entry.contains("upAxis"));
        CHECK_FALSE(entry.contains("scale"));
    }

    TEST_CASE("a part entry with settings reads with or without the plain form around it")
    {
        const auto parsed = aegis::assets::parseSidecar(R"({"id":"a3f09c2e4b1d7788","importer":"gltf","subAssets":{
            "Material/A":"c4d8e1a07b2f9036",
            "Texture/B":{"id":"08e3f5b6a1c47d29","settings":{"srgb":false}},
            "Texture/C":{"id":"1f6b2c0d93ae5547"}}})");
        REQUIRE(parsed);
        CHECK(parsed->subAssets.at("Material/A") == AssetId{ 0xc4d8e1a07b2f9036ULL });
        CHECK(parsed->subAssets.at("Texture/B") == AssetId{ 0x08e3f5b6a1c47d29ULL });
        CHECK(parsed->partSettings.at("Texture/B").srgb == false);
        CHECK(parsed->subAssets.at("Texture/C") == AssetId{ 0x1f6b2c0d93ae5547ULL });
        CHECK(parsed->partSettings.at("Texture/C") == aegis::assets::ImportSettings{});
        CHECK_FALSE(parsed->partSettings.contains("Material/A"));
    }

    TEST_CASE("a part entry with a malformed ID is rejected")
    {
        const auto parsed = aegis::assets::parseSidecar(
            R"({"id":"a3f09c2e4b1d7788","importer":"gltf","subAssets":{"Texture/B":{"id":"xyz","settings":{}}}})");
        REQUIRE_FALSE(parsed);
        CHECK(parsed.error().code == ErrorCode::InvalidData);
    }

    TEST_CASE("a texture reads as sRGB by its setting, else by its usage")
    {
        aegis::assets::ImportSettings settings;
        CHECK(aegis::assets::textureSrgb(settings, true));
        CHECK_FALSE(aegis::assets::textureSrgb(settings, false));
        settings.srgb = true;
        CHECK(aegis::assets::textureSrgb(settings, false));
        settings.srgb = false;
        CHECK_FALSE(aegis::assets::textureSrgb(settings, true));
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

    TEST_CASE("a new part's ID is derived from the file ID and the key")
    {
        // Pinned: recorded IDs are data, so the derivation must never change.
        const AssetId file{ 0xa3f09c2e4b1d7788ULL };
        CHECK(aegis::assets::deriveSubAssetId(file, "StaticMesh/Helmet/0") == AssetId{ 0x6663531068ee810fULL });
        CHECK(aegis::assets::deriveSubAssetId(file, "Material/HelmetMat") == AssetId{ 0x8973c6f52d1e69dbULL });
        CHECK(aegis::assets::deriveSubAssetId(file, "Entity/#root") == AssetId{ 0x18786c54ec5e299dULL });
        CHECK(aegis::assets::deriveSubAssetId(AssetId{ 1 }, R"(Material/a\/b)") == AssetId{ 0x5b57eda85cea2f1cULL });
        CHECK(aegis::assets::deriveSubAssetId(AssetId{ 0xa3f09c2e4b1d7789ULL }, "StaticMesh/Helmet/0") == AssetId{ 0xf7bfcb2147a70124ULL });
    }

    TEST_CASE("two sidecars with the same file ID record the same parts identically")
    {
        Sidecar first;
        first.id = AssetId{ 0xa3f09c2e4b1d7788ULL };
        first.importer = "gltf";
        auto second = first;
        first.subAssetId("Material/A");
        first.subAssetId("Entity/Body");
        second.subAssetId("Entity/Body");
        second.subAssetId("Material/A");
        CHECK(aegis::assets::writeSidecar(first) == aegis::assets::writeSidecar(second));
    }

    TEST_CASE("a recorded ID is kept, not derived again")
    {
        Sidecar sidecar;
        sidecar.id = AssetId{ 0xa3f09c2e4b1d7788ULL };
        sidecar.subAssets.emplace("Material/HelmetMat", AssetId{ 0xc4d8e1a07b2f9036ULL });
        CHECK(sidecar.subAssetId("Material/HelmetMat") == AssetId{ 0xc4d8e1a07b2f9036ULL });
        CHECK(sidecar.subAssetId("Material/New") == aegis::assets::deriveSubAssetId(sidecar.id, "Material/New"));
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
