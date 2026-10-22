export module aegis.editor.core:preferences;
import std;

export namespace aegis::editor
{
/// @brief The editor's settings that belong to the user, not to a project or scene. Kept in the user config
///        folder (preferencesPath) as JSON.
struct EditorPreferences
{
    /// @brief The viewport camera's speed, in units per second.
    float cameraSpeed{ 5.0f };
    /// @brief The gizmo's snap steps while Ctrl is held: units, degrees and a scale factor.
    float snapTranslate{ 0.5f };
    float snapRotate{ 15.0f };
    float snapScale{ 0.1f };
    /// @brief Scales the whole UI (fonts and sizes), 1 is unscaled.
    float uiScale{ 1.0f };
    /// @brief Project folders as UTF-8, the most recent first.
    std::vector<std::string> recentProjects;

    /// @brief Moves 'project' to the front of recentProjects, dropping the oldest beyond 'limit'.
    void addRecentProject(const std::filesystem::path& project, std::size_t limit = 10);
    /// @brief Puts values a hand-edited file may get wrong back into range.
    void clamp();

    [[nodiscard]] auto operator==(const EditorPreferences&) const -> bool = default;
};

/// @brief `<config>/Aegis/editor.json`: %APPDATA% on Windows, $XDG_CONFIG_HOME or ~/.config (as `aegis`) on
///        Linux. Empty if the platform names no config folder.
[[nodiscard]] auto preferencesPath() -> std::filesystem::path;

/// @brief Reads 'file'. A missing file gives the defaults; missing keys keep theirs.
/// @return The reason as text when the file exists but cannot be read or parsed.
[[nodiscard]] auto loadPreferences(const std::filesystem::path& file) -> std::expected<EditorPreferences, std::string>;

/// @brief Writes 'file', creating its folder.
[[nodiscard]] auto savePreferences(const EditorPreferences& preferences, const std::filesystem::path& file) -> std::expected<void, std::string>;
}
