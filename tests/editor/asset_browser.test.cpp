#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;

namespace
{
using aegis::editor::acceptsAsset;
using aegis::editor::AssetFilter;
using aegis::editor::AssetItem;
using aegis::editor::AssetRoot;
using aegis::editor::AssetRow;
using aegis::editor::AssetTree;
using aegis::editor::canInstantiate;
using aegis::editor::containsIgnoringCase;
using aegis::editor::dropPoint;
using aegis::editor::makeAssetPayload;
using aegis::editor::Ray;

constexpr std::size_t project = 0;
constexpr std::size_t engine = 1;

// A project with a model (a scene with mesh, material and texture parts), a scene, a texture in a subfolder, and
// the engine content's checker texture.
auto sampleTree() -> AssetTree
{
    std::vector<AssetItem> items{
        { .id = 1, .root = project, .path = "models/helmet.gltf", .type = "Scene" },
        { .id = 2, .root = project, .path = "models/helmet.gltf", .key = "StaticMesh/Helmet/0", .type = "StaticMesh" },
        { .id = 3, .root = project, .path = "models/helmet.gltf", .key = "Material/Visor", .type = "Material" },
        { .id = 4, .root = project, .path = "models/helmet.gltf", .key = "Texture/albedo", .type = "Texture" },
        { .id = 5, .root = project, .path = "scenes/simple.scene", .type = "Scene" },
        { .id = 6, .root = project, .path = "models/textures/Brick.png", .type = "Texture" },
        { .id = 7, .root = engine, .path = "textures/checker.png", .type = "Texture" },
        { .id = 8, .root = project, .path = "readme.material", .type = "Material" },
    };
    return AssetTree::build(std::move(items), { AssetRoot{ .name = "Project" }, AssetRoot{ .name = "Engine", .readOnly = true } });
}

// The names of a folder's subfolders.
auto folderNames(const AssetTree& tree, std::size_t folder) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for (const auto child : tree.folders()[folder].folders)
        names.push_back(tree.folders()[child].name);
    return names;
}

// The file rows' file paths.
auto rowPaths(const AssetTree& tree, const std::vector<AssetRow>& rows) -> std::vector<std::string>
{
    std::vector<std::string> paths;
    for (const auto& row : rows)
    {
        if (!row.folder)
            paths.push_back(tree.files()[row.file].path);
    }
    return paths;
}

// The folder rows' folder names.
auto rowFolders(const AssetTree& tree, const std::vector<AssetRow>& rows) -> std::vector<std::string>
{
    std::vector<std::string> names;
    for (const auto& row : rows)
    {
        if (row.folder)
            names.push_back(tree.folders()[*row.folder].name);
    }
    return names;
}

// The IDs of a row's parts.
auto partIds(const AssetTree& tree, const AssetRow& row) -> std::vector<std::uint64_t>
{
    std::vector<std::uint64_t> ids;
    for (const auto part : row.parts)
        ids.push_back(tree.items()[part].id);
    return ids;
}
}

TEST_SUITE("editor::AssetTree")
{
    TEST_CASE("has one root folder per root and the folders of the paths below them")
    {
        const auto tree = sampleTree();
        CHECK(tree.folders()[tree.rootFolder(project)].name == "Project");
        CHECK(tree.folders()[tree.rootFolder(engine)].name == "Engine");
        CHECK(folderNames(tree, tree.rootFolder(project)) == std::vector<std::string>{ "models", "scenes" });
        CHECK(folderNames(tree, tree.rootFolder(engine)) == std::vector<std::string>{ "textures" });

        const auto models = tree.findFolder(project, "models");
        REQUIRE(models);
        CHECK(folderNames(tree, *models) == std::vector<std::string>{ "textures" });
        const auto textures = tree.findFolder(project, "models/textures");
        REQUIRE(textures);
        CHECK(tree.folders()[*textures].parent == models);
        CHECK_FALSE(tree.findFolder(engine, "models"));
    }

    TEST_CASE("groups a file's parts under its main asset, sorted by key")
    {
        const auto tree = sampleTree();
        const auto rows = tree.rows(*tree.findFolder(project, "models"), {});
        REQUIRE(rows.size() == 2); // The textures folder, then the file
        const auto& row = rows[1];
        const auto& file = tree.files()[row.file];
        CHECK(file.name == "helmet.gltf");
        REQUIRE(file.main);
        CHECK(tree.items()[*file.main].id == 1);
        CHECK(row.mainMatches);
        CHECK(partIds(tree, row) == std::vector<std::uint64_t>{ 3, 2, 4 }); // Material, StaticMesh, Texture
    }

    TEST_CASE("lists only the folder's own files without a filter")
    {
        const auto tree = sampleTree();
        CHECK(rowPaths(tree, tree.rows(tree.rootFolder(project), {})) == std::vector<std::string>{ "readme.material" });
        CHECK(rowPaths(tree, tree.rows(*tree.findFolder(project, "models"), {})) == std::vector<std::string>{ "models/helmet.gltf" });
    }

    TEST_CASE("lists the subfolders before the files without a filter")
    {
        const auto tree = sampleTree();
        const auto rows = tree.rows(tree.rootFolder(project), {});
        REQUIRE(rows.size() == 3);
        CHECK(rowFolders(tree, rows) == std::vector<std::string>{ "models", "scenes" });
        CHECK(rows[0].folder);
        CHECK(rows[1].folder);
        CHECK_FALSE(rows[2].folder);
        CHECK(tree.folders()[*rows[1].folder].path == "scenes");
    }

    TEST_CASE("a folder with only folders lists just them")
    {
        const auto tree = sampleTree();
        const auto rows = tree.rows(*tree.findFolder(project, "models"), {});
        CHECK(rowFolders(tree, rows) == std::vector<std::string>{ "textures" });
        CHECK(rowPaths(tree, rows) == std::vector<std::string>{ "models/helmet.gltf" });
        CHECK(tree.rows(tree.rootFolder(engine), {}).size() == 1);
        CHECK(tree.rows(*tree.findFolder(project, "models/textures"), {}).size() == 1);
    }

    TEST_CASE("lists the types of its items once each, sorted")
    {
        CHECK(sampleTree().types() == std::vector<std::string>{ "Material", "Scene", "StaticMesh", "Texture" });
    }

    TEST_CASE("a file whose main asset is missing still lists its parts")
    {
        const auto tree = AssetTree::build({ AssetItem{ .id = 9, .path = "a.gltf", .key = "Texture/x", .type = "Texture" } }, { AssetRoot{ .name = "Project" } });
        const auto rows = tree.rows(tree.rootFolder(project), {});
        REQUIRE(rows.size() == 1);
        CHECK_FALSE(rows[0].mainMatches);
        CHECK(partIds(tree, rows[0]) == std::vector<std::uint64_t>{ 9 });
    }
}

TEST_SUITE("editor::AssetFilter")
{
    TEST_CASE("a type filter searches the subfolders and keeps the parts of that type")
    {
        const auto tree = sampleTree();
        const auto rows = tree.rows(tree.rootFolder(project), AssetFilter{ .type = "Texture" });
        CHECK(rowPaths(tree, rows) == std::vector<std::string>{ "models/helmet.gltf", "models/textures/Brick.png" });
        CHECK(rowFolders(tree, rows).empty());
        CHECK_FALSE(rows[0].mainMatches); // The model is a Scene
        CHECK(partIds(tree, rows[0]) == std::vector<std::uint64_t>{ 4 });
        CHECK(rows[1].mainMatches);
    }

    TEST_CASE("a search matches file names and keys, ignoring case")
    {
        const auto tree = sampleTree();
        const auto byName = tree.rows(tree.rootFolder(project), AssetFilter{ .search = "BRICK" });
        CHECK(rowPaths(tree, byName) == std::vector<std::string>{ "models/textures/Brick.png" });

        const auto byKey = tree.rows(tree.rootFolder(project), AssetFilter{ .search = "visor" });
        REQUIRE(byKey.size() == 1);
        CHECK_FALSE(byKey[0].mainMatches);
        CHECK(partIds(tree, byKey[0]) == std::vector<std::uint64_t>{ 3 });
    }

    TEST_CASE("a file name match keeps the file's parts")
    {
        const auto tree = sampleTree();
        const auto rows = tree.rows(tree.rootFolder(project), AssetFilter{ .search = "helmet" });
        REQUIRE(rows.size() == 1);
        CHECK(rows[0].mainMatches);
        CHECK(rows[0].parts.size() == 3);
    }

    TEST_CASE("the search and the type both have to match")
    {
        const auto tree = sampleTree();
        CHECK(tree.rows(tree.rootFolder(project), AssetFilter{ .search = "brick", .type = "Scene" }).empty());
        CHECK(tree.rows(tree.rootFolder(engine), AssetFilter{ .search = "checker", .type = "Texture" }).size() == 1);
    }
}

TEST_SUITE("editor::containsIgnoringCase")
{
    TEST_CASE("finds a part in any case")
    {
        CHECK(containsIgnoringCase("Helmet Visor", "visor"));
        CHECK(containsIgnoringCase("Helmet Visor", "HELMET V"));
        CHECK_FALSE(containsIgnoringCase("Helmet", "visor"));
    }

    TEST_CASE("every text holds an empty part")
    {
        CHECK(containsIgnoringCase("Helmet", ""));
        CHECK(containsIgnoringCase("", ""));
        CHECK_FALSE(containsIgnoringCase("", "a"));
    }
}

TEST_SUITE("editor::AssetPayload")
{
    TEST_CASE("a slot accepts only an asset of its type")
    {
        const auto texture = makeAssetPayload(4, "Texture");
        CHECK(texture.assetType() == "Texture");
        CHECK(acceptsAsset("Texture", texture));
        CHECK_FALSE(acceptsAsset("StaticMesh", texture));
        CHECK_FALSE(acceptsAsset("", texture));
        CHECK_FALSE(acceptsAsset("Texture", makeAssetPayload(0, "Texture")));
    }

    TEST_CASE("only a scene can be instantiated")
    {
        CHECK(canInstantiate(makeAssetPayload(1, "Scene")));
        CHECK_FALSE(canInstantiate(makeAssetPayload(4, "Texture")));
        CHECK_FALSE(canInstantiate(makeAssetPayload(0, "Scene")));
    }

    TEST_CASE("cuts a long type name to fit and keeps it terminated")
    {
        const std::string longType(100, 'x');
        const auto payload = makeAssetPayload(1, longType);
        CHECK(payload.assetType().size() == payload.type.size() - 1);
        CHECK(payload.type.back() == '\0');
    }
}

TEST_SUITE("editor::dropPoint")
{
    TEST_CASE("is where the ray meets the ground")
    {
        const Ray down{ .origin = { 1.0f, 2.0f, 10.0f }, .direction = glm::normalize(glm::vec3{ 0.0f, 1.0f, -1.0f }) };
        const auto point = dropPoint(down, 100.0f, 5.0f);
        CHECK(point.x == doctest::Approx(1.0f));
        CHECK(point.y == doctest::Approx(12.0f));
        CHECK(point.z == doctest::Approx(0.0f));
    }

    TEST_CASE("falls back to a distance along the ray when the ground is behind, parallel or too far")
    {
        const Ray up{ .origin = { 0.0f, 0.0f, 1.0f }, .direction = { 0.0f, 0.0f, 1.0f } };
        CHECK(dropPoint(up, 100.0f, 5.0f).z == doctest::Approx(6.0f));
        const Ray level{ .origin = { 0.0f, 0.0f, 1.0f }, .direction = { 1.0f, 0.0f, 0.0f } };
        CHECK(dropPoint(level, 100.0f, 5.0f).x == doctest::Approx(5.0f));
        const Ray far{ .origin = { 0.0f, 0.0f, 1000.0f }, .direction = { 0.0f, 0.0f, -1.0f } };
        CHECK(dropPoint(far, 100.0f, 5.0f).z == doctest::Approx(995.0f));
    }
}
