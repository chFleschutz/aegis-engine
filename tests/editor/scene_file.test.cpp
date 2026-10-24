#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;
import aegis.reflect;
import aegis.serialize;
import aegis.world;
import aegis.world.types;

namespace
{
using aegis::editor::sceneFilePath;
using aegis::editor::sceneText;
using aegis::editor::writeSceneFile;
using aegis::reflect::TypeRegistry;
using aegis::world::World;

// A file in the temp folder, removed at the end of the test.
class TempFile
{
public:
    TempFile() :
        m_path{ std::filesystem::temp_directory_path() / std::format("aegis-scene-{}.scene", std::chrono::steady_clock::now().time_since_epoch().count()) }
    {
    }
    TempFile(const TempFile&) = delete;
    auto operator=(const TempFile&) -> TempFile& = delete;
    ~TempFile()
    {
        std::error_code ignored;
        std::filesystem::remove(m_path, ignored);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& { return m_path; }
    [[nodiscard]] auto read() const -> std::string
    {
        std::ifstream stream{ m_path, std::ios::binary };
        return std::string{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
    }

private:
    std::filesystem::path m_path;
};
}

TEST_SUITE("editor::writeSceneFile")
{
    TEST_CASE("a saved scene reopens unchanged")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        World edited;
        const auto parent = edited.registry().create("Room", glm::vec3{ 1.0f, 0.0f, 2.0f });
        REQUIRE(edited.registry().setParent(edited.registry().create("Chair"), parent).has_value());

        TempFile file;
        REQUIRE(writeSceneFile(edited.registry(), types, 7, file.path()).has_value());
        const auto text = file.read();

        const auto parsed = aegis::serialize::parseScene(text, types);
        REQUIRE(parsed.has_value());
        CHECK(parsed->id == 7);
        World reopened;
        REQUIRE(aegis::serialize::instantiate(reopened.registry(), types, *parsed).has_value());
        const auto saved = sceneText(reopened.registry(), types, 7);
        REQUIRE(saved.has_value());
        CHECK(*saved == text);
    }

    TEST_CASE("fails for a folder that does not exist")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        World edited;
        const auto missing = std::filesystem::temp_directory_path() / "aegis-no-such-folder" / "a.scene";
        CHECK_FALSE(writeSceneFile(edited.registry(), types, 7, missing).has_value());
    }
}

TEST_SUITE("editor::sceneFilePath")
{
    TEST_CASE("appends the scene extension and uses forward slashes")
    {
        CHECK(sceneFilePath("levels\\one") == "levels/one.scene");
        CHECK(sceneFilePath("  two.scene ") == "two.scene");
        CHECK(sceneFilePath("notes.txt") == "notes.txt.scene");
    }

    TEST_CASE("refuses empty, absolute and escaping paths")
    {
        CHECK_FALSE(sceneFilePath("").has_value());
        CHECK_FALSE(sceneFilePath("   ").has_value());
        CHECK_FALSE(sceneFilePath("/abs.scene").has_value());
        CHECK_FALSE(sceneFilePath("C:/abs.scene").has_value());
        CHECK_FALSE(sceneFilePath("levels/a:b.scene").has_value());
        CHECK_FALSE(sceneFilePath("../out.scene").has_value());
        CHECK_FALSE(sceneFilePath("a//b.scene").has_value());
        CHECK_FALSE(sceneFilePath("folder/").has_value());
    }

    TEST_CASE("refuses hidden files and folders")
    {
        CHECK_FALSE(sceneFilePath(".hidden.scene").has_value());
        CHECK_FALSE(sceneFilePath(".aegis/x.scene").has_value());
    }
}
