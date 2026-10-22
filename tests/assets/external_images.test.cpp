#include <doctest/doctest.h>

import std;
import aegis.assets;
import aegis.core;

using namespace aegis;
using namespace aegis::assets;

namespace
{
// A scratch asset root that removes itself.
struct TempRoot
{
    std::filesystem::path path;

    TempRoot()
    {
        static std::atomic<int> counter{ 0 };
        path = std::filesystem::temp_directory_path() / std::format("aegis-images-test-{}-{}", AssetId::generate().toString(), counter++);
        std::filesystem::create_directories(path);
    }
    ~TempRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    TempRoot(const TempRoot&) = delete;
    auto operator=(const TempRoot&) -> TempRoot& = delete;

    // A source file (its contents are never read) with a sidecar.
    void add(const std::string& relative, AssetId id, std::string importer, std::optional<bool> srgb = std::nullopt) const
    {
        const auto file = path / relative;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{ file, std::ios::binary } << "source";
        Sidecar sidecar{ .id = id, .importer = std::move(importer) };
        sidecar.settings.srgb = srgb;
        std::ofstream{ file.string() + ".meta", std::ios::binary } << writeSidecar(sidecar);
    }

    [[nodiscard]] auto scan() const -> AssetDatabase
    {
        auto database = AssetDatabase::scan({ .roots = { { path } } });
        REQUIRE(database);
        return std::move(*database);
    }
};

class LogCapture
{
public:
    LogCapture()
    {
        log::setSink([this](log::Level level, std::string_view message) {
            if (level == log::Level::Warn)
                warnings.emplace_back(message);
        });
    }
    ~LogCapture() { log::setSink({}); }
    LogCapture(const LogCapture&) = delete;
    auto operator=(const LogCapture&) -> LogCapture& = delete;

    std::vector<std::string> warnings;
};

auto external(std::string uri, bool srgb) -> ModelImage
{
    return ModelImage{ .name = uri, .uri = std::move(uri), .srgb = srgb };
}

auto entryOf(const AssetDatabase& database, std::string_view path) -> AssetEntry
{
    const auto id = database.find(0, path);
    REQUIRE(id);
    return *database.find(*id);
}
}

TEST_SUITE("assets::resolveExternalImages")
{
    TEST_CASE("an external image gets the ID of its file's texture asset")
    {
        TempRoot root;
        const AssetId albedo{ 10 }, normal{ 11 };
        root.add("models/helmet/Helmet.gltf", AssetId{ 1 }, "model");
        root.add("models/helmet/albedo.png", albedo, "texture", true);
        root.add("textures/normal.png", normal, "texture", false);
        const auto database = root.scan();

        ModelData model;
        model.images = { external("albedo.png", true), external("../../textures/normal.png", false) };
        const LogCapture log;
        resolveExternalImages(database, entryOf(database, "models/helmet/Helmet.gltf"), model);
        CHECK(model.images[0].texture == albedo);
        CHECK(model.images[1].texture == normal);
        CHECK(log.warnings.empty());
    }

    TEST_CASE("two models that use one image get the same ID")
    {
        TempRoot root;
        const AssetId shared{ 10 };
        root.add("models/a/A.gltf", AssetId{ 1 }, "model");
        root.add("models/b/B.gltf", AssetId{ 2 }, "model");
        root.add("models/shared.png", shared, "texture", true);
        const auto database = root.scan();

        ModelData a, b;
        a.images = { external("../shared.png", true) };
        b.images = { external("../a/../shared.png", true) };
        resolveExternalImages(database, entryOf(database, "models/a/A.gltf"), a);
        resolveExternalImages(database, entryOf(database, "models/b/B.gltf"), b);
        CHECK(a.images[0].texture == shared);
        CHECK(b.images[0].texture == shared);
    }

    TEST_CASE("a usage the image's settings disagree with warns once naming both files")
    {
        TempRoot root;
        root.add("models/Helmet.gltf", AssetId{ 1 }, "model");
        root.add("models/normal.png", AssetId{ 10 }, "texture"); // No setting: imported as sRGB
        const auto database = root.scan();

        ModelData model;
        model.images = { external("normal.png", false) };
        const LogCapture log;
        resolveExternalImages(database, entryOf(database, "models/Helmet.gltf"), model);
        CHECK(model.images[0].texture == AssetId{ 10 });
        REQUIRE(log.warnings.size() == 1);
        CHECK(log.warnings[0].contains("models/Helmet.gltf"));
        CHECK(log.warnings[0].contains("models/normal.png"));
        CHECK(log.warnings[0].contains("\"srgb\": false"));
    }

    TEST_CASE("an image without a texture asset keeps no ID and warns")
    {
        TempRoot root;
        root.add("models/Helmet.gltf", AssetId{ 1 }, "model");
        root.add("models/scene.gltf", AssetId{ 2 }, "model");
        const auto database = root.scan();

        ModelData model;
        model.images = { external("missing.png", true), external("scene.gltf", true), external("../../outside.png", true) };
        const LogCapture log;
        resolveExternalImages(database, entryOf(database, "models/Helmet.gltf"), model);
        for (const auto& image : model.images)
            CHECK_FALSE(image.texture.valid());
        CHECK(log.warnings.size() == 3);
    }

    TEST_CASE("embedded images are left alone")
    {
        TempRoot root;
        root.add("models/Helmet.gltf", AssetId{ 1 }, "model");
        const auto database = root.scan();

        ModelData model;
        model.images = { ModelImage{ .name = "embedded", .srgb = true } };
        const LogCapture log;
        resolveExternalImages(database, entryOf(database, "models/Helmet.gltf"), model);
        CHECK_FALSE(model.images[0].texture.valid());
        CHECK(log.warnings.empty());
    }
}

TEST_SUITE("assets::detail::externalImagePath")
{
    TEST_CASE("resolves a URI relative to the model's folder")
    {
        CHECK(detail::externalImagePath("models/helmet/Helmet.gltf", "albedo.png") == "models/helmet/albedo.png");
        CHECK(detail::externalImagePath("models/helmet/Helmet.gltf", "textures/albedo.png") == "models/helmet/textures/albedo.png");
        CHECK(detail::externalImagePath("models/helmet/Helmet.gltf", "../albedo.png") == "models/albedo.png");
        CHECK(detail::externalImagePath("models/helmet/Helmet.gltf", "./a/../albedo.png") == "models/helmet/albedo.png");
        CHECK(detail::externalImagePath("Helmet.gltf", "albedo.png") == "albedo.png");
    }

    TEST_CASE("rejects a URI outside the root")
    {
        CHECK_FALSE(detail::externalImagePath("models/Helmet.gltf", "../../albedo.png"));
        CHECK_FALSE(detail::externalImagePath("Helmet.gltf", "../albedo.png"));
        CHECK_FALSE(detail::externalImagePath("models/Helmet.gltf", "/albedo.png"));
    }
}
