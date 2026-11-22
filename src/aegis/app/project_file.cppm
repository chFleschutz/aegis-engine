export module aegis.project_file;

import std;
import aegis.platform;
import aegis.serialize;

export namespace aegis
{
/// @brief The project file's name in a project folder.
inline constexpr std::string_view PROJECT_FILE_NAME{ "project.aegis" };

/// @brief The project file, `<project>/project.aegis`: the project's settings. So far only its input map.
///
/// JSON (comments allowed), every key optional, an unknown key an error:
/// @code
/// { "input": {
///     "actions": { "Jump": ["Space", "GamepadA"] },
///     "axes": { "MoveX": [ { "negative": "A", "positive": "D" }, { "axis": "GamepadLeftX" } ] },
///     "deadZone": 0.15 } }
/// @endcode
/// Inputs are named as platform::parseInputSource reads them; an action or axis name is taken once.
struct ProjectFile
{
    platform::InputMap input;
};

/// @brief Reads a project file's text. serialize::ErrorCode::Syntax for text that is not JSON, InvalidData for JSON of
///        the wrong shape (an unknown key or input name, a name taken twice, a dead zone outside [0, 1)), with its line.
[[nodiscard]] auto parseProjectFile(std::string_view text) -> std::expected<ProjectFile, serialize::Error>;

/// @brief Reads the project file of the folder 'project'. A folder without one has the default settings (an empty
///        input map), and so does an empty 'project'.
/// @return The file's path and what is wrong with it, or why it could not be read.
[[nodiscard]] auto loadProjectFile(const std::filesystem::path& project) -> std::expected<ProjectFile, std::string>;
}
