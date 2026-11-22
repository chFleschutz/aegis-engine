#include <doctest/doctest.h>

import std;
import aegis.platform;
import aegis.project_file;
import aegis.serialize;

namespace
{
using namespace aegis;
using platform::AxisPair;
using platform::GamepadAxis;
using platform::GamepadButton;
using platform::InputSource;
using platform::Key;
using platform::MouseButton;

constexpr std::string_view FULL_FILE = R"({
  // Comments are fine
  "input": {
    "actions": { "Jump": ["Space", "GamepadA"], "Fire": ["MouseLeft", "GamepadRightTrigger"] },
    "axes": { "MoveX": [ { "negative": "A", "positive": "D" }, { "axis": "GamepadLeftX" } ] },
    "deadZone": 0.25
  }
})";

// The error parseProjectFile gives for 'text'; fails the test if it parses.
auto parseError(std::string_view text) -> serialize::Error
{
    auto file = parseProjectFile(text);
    REQUIRE_FALSE(file.has_value());
    return file.error();
}

class TempFolder
{
public:
    TempFolder()
    {
        static int counter = 0;
        path = std::filesystem::temp_directory_path() / std::format("aegis-project-test-{}-{}", std::chrono::steady_clock::now().time_since_epoch().count(), counter++);
        std::filesystem::create_directories(path);
    }
    TempFolder(const TempFolder&) = delete;
    auto operator=(const TempFolder&) -> TempFolder& = delete;
    ~TempFolder()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }

    auto write(std::string_view text) const -> void
    {
        std::ofstream{ path / PROJECT_FILE_NAME, std::ios::binary } << text;
    }

    std::filesystem::path path;
};
}

TEST_SUITE("engine::parseProjectFile")
{
    TEST_CASE("reads the input actions, axes and dead zone")
    {
        const auto file = parseProjectFile(FULL_FILE);
        REQUIRE(file.has_value());
        const auto& map = file->input;

        REQUIRE(map.action("Jump") != nullptr);
        CHECK(*map.action("Jump") == std::vector<InputSource>{ Key::Space, GamepadButton::A });
        CHECK(*map.action("Fire") == std::vector<InputSource>{ MouseButton::Left, GamepadAxis::RightTrigger });

        const auto* axis = map.axis("MoveX");
        REQUIRE(axis != nullptr);
        REQUIRE(axis->size() == 2);
        const auto& pair = std::get<AxisPair>((*axis)[0]);
        CHECK(pair.negative == InputSource{ Key::A });
        CHECK(pair.positive == InputSource{ Key::D });
        CHECK(std::get<GamepadAxis>((*axis)[1]) == GamepadAxis::LeftX);
        CHECK(map.deadZone() == doctest::Approx(0.25f));
    }

    TEST_CASE("every key is optional")
    {
        for (const auto text : { "{}", R"({ "input": {} })", R"({ "input": { "actions": {} } })" })
        {
            const auto file = parseProjectFile(text);
            REQUIRE(file.has_value());
            CHECK(file->input.empty());
            CHECK(file->input.deadZone() == platform::InputMap::DEFAULT_DEAD_ZONE);
        }
    }

    TEST_CASE("rejects text that is not JSON")
    {
        CHECK(parseError(R"({ "input": )").code == serialize::ErrorCode::Syntax);
    }

    TEST_CASE("rejects an unknown input name with its line")
    {
        const auto error = parseError("{ \"input\": {\n  \"actions\": {\n    \"Jump\": [\"Spcae\"] } } }");
        CHECK(error.code == serialize::ErrorCode::InvalidData);
        CHECK(error.message.contains("'Spcae'"));
        CHECK(error.line == 3);
    }

    TEST_CASE("rejects unknown keys")
    {
        CHECK(parseError(R"({ "inptu": {} })").message.contains("'inptu'"));
        CHECK(parseError(R"({ "input": { "action": {} } })").message.contains("'action'"));
        CHECK(parseError(R"({ "input": { "axes": { "X": [ { "axis": "GamepadLeftX", "scale": 2 } ] } } })").message.contains("'scale'"));
    }

    TEST_CASE("rejects a name taken twice")
    {
        // Twice in one object is a JSON error already; an action and an axis share one set of names.
        CHECK(parseError(R"({ "input": { "actions": { "Jump": [], "Jump": [] } } })").code == serialize::ErrorCode::Syntax);
        const auto error = parseError(R"({ "input": { "actions": { "Move": ["W"] }, "axes": { "Move": [] } } })");
        CHECK(error.code == serialize::ErrorCode::InvalidData);
        CHECK(error.message.contains("'Move'"));
    }

    TEST_CASE("rejects malformed axis bindings")
    {
        CHECK(parseError(R"({ "input": { "axes": { "X": [ { "axis": "Space" } ] } } })").message.contains("no gamepad axis"));
        CHECK(parseError(R"({ "input": { "axes": { "X": [ { "negative": "A" } ] } } })").message.contains("needs"));
        CHECK(parseError(R"({ "input": { "axes": { "X": [ { "axis": "GamepadLeftX", "negative": "A" } ] } } })").message.contains("not both"));
        CHECK(parseError(R"({ "input": { "axes": { "X": [ "GamepadLeftX" ] } } })").message.contains("an object"));
        CHECK(parseError(R"({ "input": { "axes": { "X": [ { "negative": "GamepadLeftX", "positive": "D" } ] } } })").message.contains("not in a pair"));
    }

    TEST_CASE("rejects values of the wrong kind")
    {
        CHECK(parseError(R"([])").code == serialize::ErrorCode::InvalidData);
        CHECK(parseError(R"({ "input": { "actions": { "Jump": "Space" } } })").message.contains("an array"));
        CHECK(parseError(R"({ "input": { "actions": { "Jump": [32] } } })").message.contains("a string"));
    }

    TEST_CASE("rejects a dead zone below 0 or from 1 up")
    {
        CHECK(parseError(R"({ "input": { "deadZone": 1 } })").message.contains("deadZone"));
        CHECK(parseError(R"({ "input": { "deadZone": -0.1 } })").message.contains("deadZone"));
        CHECK(parseError(R"({ "input": { "deadZone": "0.2" } })").message.contains("deadZone"));
    }
}

TEST_SUITE("engine::loadProjectFile")
{
    TEST_CASE("a folder without a project file has the default settings")
    {
        const TempFolder folder;
        const auto file = loadProjectFile(folder.path);
        REQUIRE(file.has_value());
        CHECK(file->input.empty());
        CHECK(loadProjectFile({}).has_value());
    }

    TEST_CASE("reads the project file of the folder")
    {
        const TempFolder folder;
        folder.write(FULL_FILE);
        const auto file = loadProjectFile(folder.path);
        REQUIRE(file.has_value());
        CHECK(file->input.action("Jump") != nullptr);
    }

    TEST_CASE("names the file and the line of an error")
    {
        const TempFolder folder;
        folder.write("{\n  \"input\": 3\n}");
        const auto file = loadProjectFile(folder.path);
        REQUIRE_FALSE(file.has_value());
        CHECK(file.error().contains("project.aegis"));
        CHECK(file.error().contains("line 2"));
    }
}
