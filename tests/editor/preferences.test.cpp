#include <doctest/doctest.h>

import std;
import aegis.editor.core;

namespace
{
using aegis::editor::EditorPreferences;

// A fresh folder under the temp directory, removed afterwards.
class TempFolder
{
public:
    TempFolder() :
        m_path{ std::filesystem::temp_directory_path() / std::format("aegis-prefs-{}", std::chrono::steady_clock::now().time_since_epoch().count()) }
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

TEST_SUITE("editor::EditorPreferences")
{
    TEST_CASE("survives a save and load")
    {
        TempFolder folder;
        const auto file = folder.path() / "config" / "editor.json"; // The folder is created

        EditorPreferences saved;
        saved.cameraSpeed = 12.5f;
        saved.snapTranslate = 0.25f;
        saved.snapRotate = 45.0f;
        saved.snapScale = 0.5f;
        saved.showIcons = false;
        saved.uiScale = 1.5f;
        saved.recentProjects = { "C:/games/one", "C:/games/two" };
        REQUIRE(aegis::editor::savePreferences(saved, file));

        const auto loaded = aegis::editor::loadPreferences(file);
        REQUIRE(loaded);
        CHECK(loaded->cameraSpeed == saved.cameraSpeed);
        CHECK(loaded->snapTranslate == saved.snapTranslate);
        CHECK(loaded->snapRotate == saved.snapRotate);
        CHECK(loaded->snapScale == saved.snapScale);
        CHECK(loaded->showIcons == saved.showIcons);
        CHECK(loaded->uiScale == saved.uiScale);
        CHECK(loaded->recentProjects == saved.recentProjects);
    }

    TEST_CASE("loads the defaults when the file does not exist")
    {
        TempFolder folder;
        const auto loaded = aegis::editor::loadPreferences(folder.path() / "missing.json");
        REQUIRE(loaded);
        CHECK(loaded->cameraSpeed == EditorPreferences{}.cameraSpeed);
        CHECK(loaded->recentProjects.empty());
    }

    TEST_CASE("keeps the defaults of keys the file lacks and ignores unknown ones")
    {
        TempFolder folder;
        const auto file = folder.path() / "editor.json";
        write(file, R"({ "uiScale": 2.0, "somethingNew": true })");

        const auto loaded = aegis::editor::loadPreferences(file);
        REQUIRE(loaded);
        CHECK(loaded->uiScale == 2.0f);
        CHECK(loaded->cameraSpeed == EditorPreferences{}.cameraSpeed);
    }

    TEST_CASE("reads the viewport icons' toggle by its key")
    {
        TempFolder folder;
        const auto file = folder.path() / "editor.json";
        write(file, R"({ "showIcons": false })");

        const auto loaded = aegis::editor::loadPreferences(file);
        REQUIRE(loaded);
        CHECK_FALSE(loaded->showIcons);
        CHECK(EditorPreferences{}.showIcons);
    }

    TEST_CASE("reports a file that is not valid")
    {
        TempFolder folder;
        const auto file = folder.path() / "editor.json";
        write(file, "{ not json");

        CHECK_FALSE(aegis::editor::loadPreferences(file));
    }

    TEST_CASE("puts out-of-range values back into range when loading")
    {
        TempFolder folder;
        const auto file = folder.path() / "editor.json";
        write(file, R"({ "uiScale": 100.0, "snapTranslate": -1.0 })");

        const auto loaded = aegis::editor::loadPreferences(file);
        REQUIRE(loaded);
        CHECK(loaded->uiScale <= 4.0f);
        CHECK(loaded->snapTranslate > 0.0f);
    }

    TEST_CASE("lists the most recent project first, once, up to a limit")
    {
        EditorPreferences preferences;
        preferences.addRecentProject("a", 3);
        preferences.addRecentProject("b", 3);
        preferences.addRecentProject("c", 3);
        preferences.addRecentProject("a", 3);
        CHECK(preferences.recentProjects == std::vector<std::string>{ "a", "c", "b" });

        preferences.addRecentProject("d", 3);
        CHECK(preferences.recentProjects == std::vector<std::string>{ "d", "a", "c" });
    }

    TEST_CASE("finds the UI scale among the steps")
    {
        CHECK(aegis::editor::uiScaleStep(0.75f) == 0);
        CHECK(aegis::editor::uiScaleStep(1.0f) == 1);
        CHECK(aegis::editor::uiScaleStep(2.0f) == 5);
        CHECK_FALSE(aegis::editor::uiScaleStep(1.3f));
        CHECK_FALSE(aegis::editor::uiScaleStep(4.0f));
    }

    TEST_CASE("labels a step by its percentage and any other scale as custom")
    {
        CHECK(aegis::editor::uiScaleLabel(1.0f) == "100%");
        CHECK(aegis::editor::uiScaleLabel(1.75f) == "175%");
        CHECK(aegis::editor::uiScaleLabel(1.3f) == "Custom (1.30)");
    }

    TEST_CASE("labels the effective scale with the monitor's")
    {
        CHECK(aegis::editor::effectiveScaleLabel(1.5f, 1.5f) == "\u00D7 1.5 monitor = 225%");
        CHECK(aegis::editor::effectiveScaleLabel(1.0f, 0.75f) == "\u00D7 1 monitor = 75%");
    }
}
