#include <doctest/doctest.h>

import std;
import aegis.editor.core;

namespace
{
using aegis::editor::RecentScenes;
using Paths = std::vector<std::string>;

// A fresh folder under the temp directory, removed afterwards.
class TempFolder
{
public:
    TempFolder() :
        m_path{ std::filesystem::temp_directory_path() / std::format("aegis-recent-{}", std::chrono::steady_clock::now().time_since_epoch().count()) }
    {
        std::filesystem::create_directories(m_path);
    }
    ~TempFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& { return m_path; }

private:
    std::filesystem::path m_path;
};

void write(const std::filesystem::path& file, std::string_view text)
{
    std::ofstream stream{ file, std::ios::binary };
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
}
}

TEST_SUITE("editor::RecentScenes")
{
    TEST_CASE("lists the most recent scene first, once")
    {
        RecentScenes scenes;
        scenes.add("scenes/a.scene");
        scenes.add("scenes/b.scene");
        scenes.add("scenes/a.scene");
        CHECK(scenes.paths == Paths{ "scenes/a.scene", "scenes/b.scene" });
    }

    TEST_CASE("keeps at most the limit, dropping the oldest")
    {
        RecentScenes scenes;
        for (std::size_t i = 0; i <= RecentScenes::limit; ++i)
            scenes.add(std::format("{}.scene", i));
        REQUIRE(scenes.paths.size() == RecentScenes::limit);
        CHECK(scenes.paths.front() == std::format("{}.scene", RecentScenes::limit));
        CHECK(std::ranges::find(scenes.paths, "0.scene") == scenes.paths.end());
    }

    TEST_CASE("drops the scenes that are gone and keeps the order of the rest")
    {
        RecentScenes scenes{ .paths = { "c.scene", "gone.scene", "b.scene", "also_gone.scene", "a.scene" } };
        CHECK(scenes.prune([](std::string_view path) { return !path.contains("gone"); }));
        CHECK(scenes.paths == Paths{ "c.scene", "b.scene", "a.scene" });
        CHECK_FALSE(scenes.prune([](std::string_view) { return true; }));
    }

    TEST_CASE("survives a save and load")
    {
        TempFolder folder;
        const auto file = aegis::editor::recentScenesPath(folder.path()); // The folders are created
        const RecentScenes saved{ .paths = { "scenes/b.scene", "scenes/a.scene" } };
        REQUIRE(aegis::editor::saveRecentScenes(saved, file));

        const auto loaded = aegis::editor::loadRecentScenes(file);
        REQUIRE(loaded);
        CHECK(*loaded == saved);
    }

    TEST_CASE("is empty when the file does not exist")
    {
        TempFolder folder;
        const auto loaded = aegis::editor::loadRecentScenes(folder.path() / "missing.json");
        REQUIRE(loaded);
        CHECK(loaded->paths.empty());
    }

    TEST_CASE("cuts a hand-edited file to the limit without duplicates, in its order")
    {
        TempFolder folder;
        const auto file = folder.path() / "recent_scenes.json";
        std::string list;
        for (std::size_t i = 0; i < RecentScenes::limit + 2; ++i)
            list += std::format("\"{}.scene\", ", i);
        write(file, std::format(R"({{ "paths": [ "0.scene", {} "last.scene" ] }})", list));

        const auto loaded = aegis::editor::loadRecentScenes(file);
        REQUIRE(loaded);
        REQUIRE(loaded->paths.size() == RecentScenes::limit);
        CHECK(loaded->paths[0] == "0.scene");
        CHECK(loaded->paths[1] == "1.scene");
        CHECK(std::ranges::count(loaded->paths, "0.scene") == 1);
    }

    TEST_CASE("reports a file that is not valid")
    {
        TempFolder folder;
        const auto file = folder.path() / "recent_scenes.json";
        write(file, "{ not json");
        CHECK_FALSE(aegis::editor::loadRecentScenes(file));
    }
}
