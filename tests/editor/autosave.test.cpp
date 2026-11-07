#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.reflect;
import aegis.serialize;
import aegis.world;
import aegis.world.types;

namespace
{
using aegis::editor::autosaveIsNewer;
using aegis::editor::autosavePath;
using aegis::editor::findAutosave;
using aegis::editor::recoveryLabel;
using aegis::reflect::TypeRegistry;
using aegis::world::World;
using Clock = std::filesystem::file_time_type::clock;
using namespace std::chrono_literals;

// A fresh folder under the temp directory, removed afterwards.
class TempFolder
{
public:
    TempFolder() :
        m_path{ std::filesystem::temp_directory_path() / std::format("aegis-autosave-{}", std::chrono::steady_clock::now().time_since_epoch().count()) }
    {
        std::filesystem::create_directories(m_path);
    }
    TempFolder(const TempFolder&) = delete;
    auto operator=(const TempFolder&) -> TempFolder& = delete;
    ~TempFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    [[nodiscard]] auto path() const -> const std::filesystem::path& { return m_path; }

private:
    std::filesystem::path m_path;
};

// Writes 'file' and sets its time to 'written'.
void write(const std::filesystem::path& file, std::filesystem::file_time_type written)
{
    std::ofstream{ file, std::ios::binary } << "scene";
    std::filesystem::last_write_time(file, written);
}

auto localTime(int year, unsigned month, unsigned day, std::chrono::seconds time) -> std::chrono::local_seconds
{
    return std::chrono::local_days{ std::chrono::year{ year } / month / day } + time;
}
}

TEST_SUITE("editor::autosavePath")
{
    TEST_CASE("names a saved scene's autosave by its ID")
    {
        const std::filesystem::path folder{ "project/.aegis/editor/autosave" };
        CHECK(aegis::editor::autosaveFolder("project") == folder);
        CHECK(autosavePath(folder, 0x00ab'cdef'0123'4567) == folder / "00abcdef01234567.scene");
    }

    TEST_CASE("names an untitled scene's autosave untitled")
    {
        const std::filesystem::path folder{ "autosave" };
        CHECK(autosavePath(folder, std::nullopt) == folder / "untitled.scene");
        CHECK(autosavePath(folder, 0) == folder / "untitled.scene");
    }
}

TEST_SUITE("editor::autosaveIsNewer")
{
    TEST_CASE("offers an autosave newer than its scene file and not an older or equal one")
    {
        const auto scene = Clock::now();
        CHECK(autosaveIsNewer(scene + 1s, scene));
        CHECK_FALSE(autosaveIsNewer(scene - 1s, scene));
        CHECK_FALSE(autosaveIsNewer(scene, scene));
    }

    TEST_CASE("offers any autosave of a scene without a file")
    {
        CHECK(autosaveIsNewer(Clock::now() - 24h, std::nullopt));
    }
}

TEST_SUITE("editor::findAutosave")
{
    TEST_CASE("finds an autosave newer than the scene file")
    {
        TempFolder folder;
        const auto scene = folder.path() / "level.scene";
        const auto autosave = folder.path() / "autosave.scene";
        const auto now = Clock::now();
        write(scene, now - 1h);
        write(autosave, now);

        const auto found = findAutosave(autosave, scene);
        REQUIRE(found);
        CHECK(*found == std::filesystem::last_write_time(autosave));
    }

    TEST_CASE("ignores an autosave older than the scene file")
    {
        TempFolder folder;
        const auto scene = folder.path() / "level.scene";
        const auto autosave = folder.path() / "autosave.scene";
        const auto now = Clock::now();
        write(autosave, now - 1h);
        write(scene, now);

        CHECK_FALSE(findAutosave(autosave, scene));
    }

    TEST_CASE("finds an untitled scene's autosave and none that is missing")
    {
        TempFolder folder;
        const auto autosave = folder.path() / "untitled.scene";
        CHECK_FALSE(findAutosave(autosave, {}));
        write(autosave, Clock::now());
        CHECK(findAutosave(autosave, {}));
    }
}

TEST_SUITE("editor::writeAutosave")
{
    TEST_CASE("writes the scene into a folder it creates and reads it back")
    {
        TypeRegistry types;
        REQUIRE(aegis::world::registerComponents(types).has_value());
        World edited;
        (void)edited.registry().create("Chair");

        TempFolder folder;
        const auto file = autosavePath(aegis::editor::autosaveFolder(folder.path()), 7);
        REQUIRE(aegis::editor::writeAutosave(edited.registry(), types, 7, file).has_value());

        const auto text = aegis::editor::readAutosave(file);
        REQUIRE(text.has_value());
        CHECK(*text == aegis::editor::sceneText(edited.registry(), types, 7));
        const auto parsed = aegis::serialize::parseScene(*text, types);
        REQUIRE(parsed.has_value());
        CHECK(parsed->id == 7);
    }

    TEST_CASE("reading a missing autosave fails")
    {
        TempFolder folder;
        CHECK_FALSE(aegis::editor::readAutosave(folder.path() / "missing.scene").has_value());
    }
}

TEST_SUITE("editor::recoveryLabel")
{
    TEST_CASE("names the scene and the time of today's autosave")
    {
        const auto written = localTime(2026, 11, 6, 14h + 32min + 59s);
        const auto today = std::chrono::floor<std::chrono::days>(written);
        CHECK(recoveryLabel("sponza.scene", written, today) == "Unsaved changes to sponza.scene from 14:32");
        CHECK(recoveryLabel("Untitled", localTime(2026, 11, 6, 9h + 5min), today) == "Unsaved changes to Untitled from 09:05");
    }

    TEST_CASE("adds the date of an older autosave")
    {
        const auto today = std::chrono::local_days{ std::chrono::year{ 2026 } / 11 / 6 };
        CHECK(recoveryLabel("sponza.scene", localTime(2026, 11, 5, 23h + 1min), today) == "Unsaved changes to sponza.scene from 2026-11-05 23:01");
    }
}
