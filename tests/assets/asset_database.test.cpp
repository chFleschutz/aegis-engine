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

auto sidecarOf(AssetId id, std::initializer_list<std::pair<std::string, AssetId>> parts = {}, std::string importer = "model") -> Sidecar
{
    Sidecar sidecar;
    sidecar.id = id;
    sidecar.importer = std::move(importer);
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

    TEST_CASE("a model's entity IDs in its sidecar are no assets")
    {
        TempRoot root;
        const AssetId main{ 1 }, part{ 2 }, entity{ 3 };
        root.writeSidecarFor("models/Helmet.gltf", sidecarOf(main, { { "Material/Mat", part }, { "Entity/Body", entity } }));

        const auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);
        CHECK(database->size() == 2);
        CHECK(database->find(entity) == nullptr);
        CHECK_FALSE(database->find(0, "models/Helmet.gltf", "Entity/Body"));
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
        CHECK(database->size() == 8); // The six files and the cube's mesh and default material

        const auto path = [&](AssetId id) -> std::string {
            const auto* entry = database->find(id);
            return entry ? entry->path : std::string{};
        };
        CHECK(path(engine::white) == "white.png");
        CHECK(path(engine::black) == "black.png");
        CHECK(path(engine::normal) == "normal.png");
        CHECK(path(engine::checker) == "checker.png");
        CHECK(path(engine::cube) == "cube.obj");
        CHECK(path(engine::pbrTemplate) == "pbr.template");
    }

    TEST_CASE("the engine cube's parts have IDs in its sidecar")
    {
        const auto database = AssetDatabase::scan({ .roots = { { std::filesystem::path{ AEGIS_TEST_CONTENT_DIR }, true } } });
        REQUIRE(database);
        CHECK(database->find(0, "cube.obj", "StaticMesh/cube/0"));
        CHECK(database->find(0, "cube.obj", SubAssetKeys::defaultMaterial));
    }

    TEST_CASE("the engine cube's sidecar records the ID of its root entity")
    {
        std::ifstream stream{ std::filesystem::path{ AEGIS_TEST_CONTENT_DIR } / "cube.obj.meta", std::ios::binary };
        const std::string text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
        const auto sidecar = parseSidecar(text);
        REQUIRE(sidecar);
        CHECK(sidecar->subAssets.contains(std::string{ SubAssetKeys::rootEntity }));
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
        const auto version = index.find("\"version\": 3");
        REQUIRE(version != std::string::npos);
        index.replace(version, 12, "\"version\": 2");
        root.write(".aegis/index", index);

        const auto database = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
        REQUIRE(database);
        CHECK(database->filesRead() == 1);
    }

    TEST_CASE("every entry has its asset type, also from the index")
    {
        TempRoot root;
        root.writeSidecarFor("Helmet.gltf",
            sidecarOf(AssetId{ 1 }, { { "StaticMesh/Body/0", AssetId{ 2 } }, { "Material/Metal", AssetId{ 3 } }, { "Texture/#0", AssetId{ 4 } },
                                        { "Entity/Body", AssetId{ 5 } } }));
        root.writeSidecarFor("wood.png", sidecarOf(AssetId{ 6 }, {}, "texture"));
        root.writeSidecarFor("sky.hdr", sidecarOf(AssetId{ 7 }, {}, "environment"));
        root.write("level.scene", R"({"type":"Scene","id":"0000000000000008","version":1})");
        root.write("wood.material", R"({"type":"Material","id":"0000000000000009","version":1})");
        const auto indexFile = root.path / ".aegis" / "index";

        for (const auto expectedReads : { 5u, 0u })
        {
            const auto database = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
            REQUIRE(database);
            CHECK(database->filesRead() == expectedReads);
            REQUIRE(database->size() == 8);
            const auto type = [&](std::uint64_t id) { return database->find(AssetId{ id })->type; };
            CHECK(type(1) == "Scene");
            CHECK(type(2) == "StaticMesh");
            CHECK(type(3) == "Material");
            CHECK(type(4) == "Texture");
            CHECK(type(6) == "Texture");
            CHECK(type(7) == "Environment");
            CHECK(type(8) == "Scene");
            CHECK(type(9) == "Material");
        }
    }

    TEST_CASE("every entry of the engine content has its asset type")
    {
        const auto database = AssetDatabase::scan({ .roots = { { std::filesystem::path{ AEGIS_TEST_CONTENT_DIR }, true } } });
        REQUIRE(database);
        for (const auto& entry : database->entries())
            CHECK_MESSAGE(!entry.type.empty(), entry.path, " ", entry.key);

        CHECK(database->find(engine::white)->type == "Texture");
        CHECK(database->find(engine::cube)->type == "Scene");
        CHECK(database->find(engine::pbrTemplate)->type == "MaterialTemplate");
        CHECK(database->find(*database->find(0, "cube.obj", "StaticMesh/cube/0"))->type == "StaticMesh");
        CHECK(database->find(*database->find(0, "cube.obj", SubAssetKeys::defaultMaterial))->type == "Material");
    }

    TEST_CASE("a sidecar of an unknown importer fails the scan naming the file")
    {
        TempRoot root;
        root.writeSidecarFor("a.gltf", sidecarOf(AssetId{ 1 }, {}, "gltf"));
        const auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE_FALSE(database);
        CHECK(database.error().code == ErrorCode::InvalidData);
        CHECK(database.error().message.contains("a.gltf.meta"));
    }

    TEST_CASE("a scene written after the scan is found by ID once added, also by the next scan from the index")
    {
        TempRoot root;
        root.writeSidecarFor("a.gltf", sidecarOf(AssetId{ 1 }));
        const auto indexFile = root.path / ".aegis" / "index";
        auto database = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
        REQUIRE(database);

        root.write("levels/new.scene", R"({"type":"Scene","id":"0000000000000002","version":1})");
        CHECK_FALSE(database->find(AssetId{ 2 }));
        REQUIRE(database->addFile(0, "levels/new.scene"));
        const auto* entry = database->find(AssetId{ 2 });
        REQUIRE(entry);
        CHECK(entry->path == "levels/new.scene");
        CHECK(entry->type == "Scene");
        CHECK(database->find(0, "levels/new.scene") == AssetId{ 2 });

        const auto again = AssetDatabase::scan({ .roots = { { root.path } }, .indexFile = indexFile });
        REQUIRE(again);
        CHECK(again->filesRead() == 0);
        CHECK(again->entries() == database->entries());
    }

    TEST_CASE("adding a rewritten file replaces its entries")
    {
        TempRoot root;
        root.write("a.scene", R"({"type":"Scene","id":"0000000000000001","version":1})");
        auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);

        root.write("a.scene", R"({"type":"Scene","id":"0000000000000002","version":1})");
        REQUIRE(database->addFile(0, "a.scene"));
        CHECK(database->size() == 1);
        CHECK_FALSE(database->find(AssetId{ 1 }));
        CHECK(database->find(0, "a.scene") == AssetId{ 2 });
    }

    TEST_CASE("adding a file whose ID another file has is rejected naming both")
    {
        TempRoot root;
        root.write("a.scene", R"({"type":"Scene","id":"0000000000000001","version":1})");
        auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);

        root.write("copy.scene", R"({"type":"Scene","id":"0000000000000001","version":1})");
        const auto added = database->addFile(0, "copy.scene");
        REQUIRE_FALSE(added);
        CHECK(added.error().code == ErrorCode::InvalidData);
        CHECK(added.error().message.contains("a.scene"));
        CHECK(added.error().message.contains("copy.scene"));
        CHECK(database->find(AssetId{ 1 })->path == "a.scene");
    }

    TEST_CASE("adding a file rejects a missing file, a non-asset and a case-only clash")
    {
        TempRoot root;
        root.write("Level.scene", R"({"type":"Scene","id":"0000000000000001","version":1})");
        root.write("notes.txt", "not an asset");
        auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);

        const auto failure = [&](std::string_view path) -> std::optional<ErrorCode> {
            const auto added = database->addFile(0, path);
            return added ? std::nullopt : std::optional{ added.error().code };
        };
        CHECK(failure("missing.scene") == ErrorCode::FileNotFound);
        CHECK(failure("notes.txt") == ErrorCode::InvalidData);
        root.write("level.scene", R"({"type":"Scene","id":"0000000000000002","version":1})");
        CHECK(failure("level.scene") == ErrorCode::InvalidData);
        CHECK(database->size() == 1);
    }

    TEST_CASE("adding a foreign file writes its sidecar under createSidecars")
    {
        TempRoot root;
        auto database = AssetDatabase::scan({ .roots = { { root.path } }, .createSidecars = true });
        REQUIRE(database);

        root.write("wood.png", "source");
        REQUIRE(database->addFile(0, "wood.png"));
        CHECK(std::filesystem::exists(root.path / "wood.png.meta"));
        const auto id = database->find(0, "wood.png");
        REQUIRE(id);
        CHECK(database->find(*id)->type == "Texture");
    }

    TEST_CASE("a rescan finds files changed since the scan and keeps the entries if it fails")
    {
        TempRoot root;
        root.write("a.scene", R"({"type":"Scene","id":"0000000000000001","version":1})");
        auto database = AssetDatabase::scan({ .roots = { { root.path } } });
        REQUIRE(database);

        root.write("b.scene", R"({"type":"Scene","id":"0000000000000002","version":1})");
        REQUIRE(database->rescan());
        CHECK(database->size() == 2);
        CHECK(database->find(AssetId{ 2 }));

        root.write("broken.scene", "{");
        CHECK_FALSE(database->rescan());
        CHECK(database->size() == 2);
        CHECK(database->roots().size() == 1);
    }
}
