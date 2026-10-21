#include <doctest/doctest.h>

import std;
import aegis.assets;

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
        path = std::filesystem::temp_directory_path() / std::format("aegis-db-test-{}-{}", AssetId::generate().toString(), counter++);
        std::filesystem::create_directories(path);
    }
    ~TempRoot()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    void write(const std::string& relative, const std::string& text) const
    {
        const auto file = path / relative;
        std::filesystem::create_directories(file.parent_path());
        std::ofstream{ file, std::ios::binary } << text;
    }

    void writeSidecarFor(const std::string& source, const Sidecar& sidecar) const
    {
        write(source, "source");
        write(source + ".meta", writeSidecar(sidecar));
    }
};

auto sidecarOf(AssetId id, std::initializer_list<std::pair<std::string, AssetId>> parts = {}) -> Sidecar
{
    Sidecar sidecar;
    sidecar.id = id;
    sidecar.importer = "gltf";
    for (const auto& [key, part] : parts)
        sidecar.subAssets.emplace(key, part);
    return sidecar;
}
}

TEST_SUITE("assets::AssetDatabase")
{
    TEST_CASE("an ID finds its source and a source finds its ID")
    {
        TempRoot root;
        const AssetId main{ 1 }, part{ 2 };
        root.writeSidecarFor("models/Helmet.gltf", sidecarOf(main, { { "Material/Mat", part } }));

        const auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);
        CHECK(database->size() == 2);

        const auto* entry = database->find(part);
        REQUIRE(entry);
        CHECK(entry->path == "models/Helmet.gltf");
        CHECK(entry->key == "Material/Mat");
        CHECK(database->find(0, "models/Helmet.gltf") == main);
        CHECK(database->find(0, "models/Helmet.gltf", "Material/Mat") == part);
        CHECK_FALSE(database->find(0, "models/Other.gltf"));
        CHECK(database->find(AssetId{ 99 }) == nullptr);
        CHECK(database->absolutePath(*entry) == root.path / "models/Helmet.gltf");
    }

    TEST_CASE("a native file carries its own ID")
    {
        TempRoot root;
        root.write("helmet.material", R"({"type":"Material","id":"5b0e17d2c94af3a1","version":1})");

        const auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);
        const auto* entry = database->find(AssetId{ 0x5b0e17d2c94af3a1ULL });
        REQUIRE(entry);
        CHECK(entry->type == "Material");
        CHECK(entry->path == "helmet.material");
    }

    TEST_CASE("a duplicate ID is rejected naming both files")
    {
        TempRoot root;
        root.writeSidecarFor("a/Helmet.gltf", sidecarOf(AssetId{ 7 }));
        root.writeSidecarFor("b/Helmet.gltf", sidecarOf(AssetId{ 7 }));

        const auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE_FALSE(database);
        CHECK(database.error().code == ErrorCode::InvalidData);
        CHECK(database.error().message.contains("a/Helmet.gltf"));
        CHECK(database.error().message.contains("b/Helmet.gltf"));
    }

    TEST_CASE("an ID shared between the project and the engine content is rejected")
    {
        TempRoot project, engine;
        project.writeSidecarFor("a.png", sidecarOf(AssetId{ 5 }));
        engine.writeSidecarFor("white.png", sidecarOf(AssetId{ 5 }));
        CHECK_FALSE(AssetDatabase::scan({ .roots = { { project.path }, { engine.path, true } } }));
    }

    TEST_CASE("the engine content is a second root")
    {
        TempRoot project, engine;
        project.writeSidecarFor("a.png", sidecarOf(AssetId{ 5 }));
        engine.writeSidecarFor("white.png", sidecarOf(AssetId{ 6 }));

        const auto database = AssetDatabase::scan({ .roots = { { project.path }, { engine.path, true } } });
        REQUIRE(database);
        CHECK(database->find(AssetId{ 6 })->root == 1);
        CHECK(database->roots()[1].readOnly);
    }

    TEST_CASE("a broken sidecar fails the scan naming the file")
    {
        TempRoot root;
        root.write("bad.png.meta", R"({"id":"NOPE"})");
        const auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE_FALSE(database);
        CHECK(database.error().message.contains("bad.png.meta"));
    }

    TEST_CASE("a missing root is an error")
    {
        const auto database = AssetDatabase::scan({ .roots = { { std::filesystem::path{ "does/not/exist" } } } });
        REQUIRE_FALSE(database);
        CHECK(database.error().code == ErrorCode::FileNotFound);
    }

    TEST_CASE("paths that differ only in case are found")
    {
        const std::vector<std::string> clashing{ "textures/Wood.png", "models/a.gltf", "textures/wood.png" };
        const auto clash = detail::findCaseClash(clashing);
        REQUIRE(clash);
        CHECK(clash->first == "textures/Wood.png");
        CHECK(clash->second == "textures/wood.png");

        const std::vector<std::string> distinct{ "textures/Wood.png", "textures/Wood2.png" };
        CHECK_FALSE(detail::findCaseClash(distinct));
    }

    TEST_CASE("a second scan rereads only changed files")
    {
        TempRoot root;
        root.writeSidecarFor("a.gltf", sidecarOf(AssetId{ 1 }));
        root.writeSidecarFor("b.gltf", sidecarOf(AssetId{ 2 }));
        const auto indexFile = root.path / ".aegis" / "index";

        const auto first = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
        REQUIRE(first);
        CHECK(first->filesRead() == 2);
        CHECK(std::filesystem::exists(indexFile));

        const auto second = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
        REQUIRE(second);
        CHECK(second->filesRead() == 0);
        CHECK(second->entries() == first->entries());

        root.write("b.gltf.meta", writeSidecar(sidecarOf(AssetId{ 3 }, { { "Material/New", AssetId{ 4 } } })));
        const auto third = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
        REQUIRE(third);
        CHECK(third->filesRead() == 1);
        CHECK(third->find(AssetId{ 4 }) != nullptr);
        CHECK(third->find(AssetId{ 2 }) == nullptr);
    }

    TEST_CASE("the cache folder is not scanned")
    {
        TempRoot root;
        root.write(".aegis/stray.gltf.meta", writeSidecar(sidecarOf(AssetId{ 9 })));
        const auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);
        CHECK(database->size() == 0);
    }

    TEST_CASE("createSidecars writes a sidecar with new IDs for foreign files")
    {
        TempRoot root;
        root.write("models/Helmet.gltf", "source");
        root.write("tex/wood.png", "source");
        root.write("readme.txt", "not an asset");

        const auto database = AssetDatabase::scan({ .roots = { { root.path } }, .createSidecars = true });
        REQUIRE(database);
        CHECK(database->size() == 2);
        CHECK(std::filesystem::exists(root.path / "models/Helmet.gltf.meta"));
        CHECK_FALSE(std::filesystem::exists(root.path / "readme.txt.meta"));

        const auto helmet = database->find(0, "models/Helmet.gltf");
        REQUIRE(helmet);

        // A second scan keeps the IDs it wrote.
        const auto again = AssetDatabase::scan({ .roots = { { root.path } }, .createSidecars = true });
        REQUIRE(again);
        CHECK(again->find(0, "models/Helmet.gltf") == helmet);
    }

    TEST_CASE("createSidecars leaves read-only roots alone")
    {
        TempRoot root;
        root.write("white.png", "source");
        const auto database = AssetDatabase::scan({ .roots = { { root.path, true } }, .createSidecars = true });
        REQUIRE(database);
        CHECK(database->size() == 0);
        CHECK_FALSE(std::filesystem::exists(root.path / "white.png.meta"));
    }

    TEST_CASE("addSubAssets records parts and rejects an ID another asset has")
    {
        TempRoot root;
        root.writeSidecarFor("a.gltf", sidecarOf(AssetId{ 1 }));
        root.writeSidecarFor("b.gltf", sidecarOf(AssetId{ 2 }));
        auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);

        const std::vector<std::pair<std::string, AssetId>> parts{ { "Material/A", AssetId{ 10 } } };
        CHECK(database->addSubAssets(0, "a.gltf", parts));
        CHECK(database->addSubAssets(0, "a.gltf", parts)); // Again: no change
        CHECK(database->find(AssetId{ 10 })->key == "Material/A");
        CHECK(database->find(0, "a.gltf", "Material/A") == AssetId{ 10 });

        const std::vector<std::pair<std::string, AssetId>> clashing{ { "Material/A", AssetId{ 2 } } };
        CHECK_FALSE(database->addSubAssets(0, "a.gltf", clashing));
    }

    TEST_CASE("the importer comes from the extension")
    {
        CHECK(importerFor("a/Helmet.GLTF") == "model");
        CHECK(importerFor("cube.obj") == "model");
        CHECK(importerFor("wood.png") == "texture");
        CHECK(importerFor("sky.hdr") == "environment");
        CHECK(importerFor("notes.txt").empty());
    }

    TEST_CASE("the engine content constants name the files of the content folder")
    {
        const auto database = AssetDatabase::scan({ .roots = { { std::filesystem::path{ AEGIS_TEST_CONTENT_DIR }, true } } });
        REQUIRE(database);
        CHECK(database->size() == 5);

        const auto path = [&](AssetId id) -> std::string {
            const auto* entry = database->find(id);
            return entry ? entry->path : std::string{};
        };
        CHECK(path(engine::white) == "white.png");
        CHECK(path(engine::black) == "black.png");
        CHECK(path(engine::normal) == "normal.png");
        CHECK(path(engine::checker) == "checker.png");
        CHECK(path(engine::cube) == "cube.obj");
    }

    TEST_CASE("a main entry carries its sidecar's settings, also from the index")
    {
        TempRoot root;
        auto sidecar = sidecarOf(AssetId{ 1 }, { { "Material/Mat", AssetId{ 2 } } });
        sidecar.settings.srgb = false;
        root.writeSidecarFor("normal.png", sidecar);
        const auto indexFile = root.path / ".aegis" / "index";
        for (const auto expectedReads : { 1u, 0u })
        {
            const auto database = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
            REQUIRE(database);
            CHECK(database->filesRead() == expectedReads);
            CHECK(database->find(AssetId{ 1 })->settings.srgb == false);
            CHECK(database->find(AssetId{ 2 })->settings == ImportSettings{});
        }
    }

    TEST_CASE("an index of another version is rebuilt")
    {
        TempRoot root;
        root.writeSidecarFor("a.gltf", sidecarOf(AssetId{ 1 }));
        const auto indexFile = root.path / ".aegis" / "index";
        REQUIRE(AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile }));

        // The same index with an older version number: none of it is trusted.
        std::string index;
        {
            std::ifstream in{ indexFile, std::ios::binary };
            index.assign(std::istreambuf_iterator<char>{ in }, std::istreambuf_iterator<char>{});
        }
        const auto version = index.find("\"version\": 2");
        REQUIRE(version != std::string::npos);
        index.replace(version, 12, "\"version\": 1");
        root.write(".aegis/index", index);

        const auto database = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
        REQUIRE(database);
        CHECK(database->filesRead() == 1);
    }
}
