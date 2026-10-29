export module aegis.editor.core:recent_scenes;
import std;

export namespace aegis::editor
{
/// @brief The scenes of a project opened or saved last, for File > Open Recent. Kept per project, not in the user's
///        preferences (`<project>/.aegis/editor/recent_scenes.json`, recentScenesPath).
struct RecentScenes
{
    /// @brief The most scenes kept; the oldest are dropped past it.
    static constexpr std::size_t limit = 10;

    /// @brief Paths in the project's content folder, with '/', the most recent first.
    std::vector<std::string> paths;

    /// @brief Moves 'path' to the front, dropping the oldest beyond the limit.
    void add(std::string_view path);
    /// @brief Drops the paths 'exists' says are gone; whether any was.
    auto prune(const std::function<bool(std::string_view path)>& exists) -> bool;

    [[nodiscard]] auto operator==(const RecentScenes&) const -> bool = default;
};

/// @brief `<project>/.aegis/editor/recent_scenes.json`, next to the window layout.
[[nodiscard]] auto recentScenesPath(const std::filesystem::path& project) -> std::filesystem::path;

/// @brief Reads 'file'. A missing file gives an empty list; more paths than the limit are cut, duplicates dropped.
/// @return The reason as text when the file exists but cannot be read or parsed.
[[nodiscard]] auto loadRecentScenes(const std::filesystem::path& file) -> std::expected<RecentScenes, std::string>;

/// @brief Writes 'file', creating its folder.
[[nodiscard]] auto saveRecentScenes(const RecentScenes& scenes, const std::filesystem::path& file) -> std::expected<void, std::string>;
}
