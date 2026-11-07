export module aegis.editor.core:autosave;
import std;
import aegis.reflect;
import aegis.world;

export namespace aegis::editor
{
/// @brief `<project>/.aegis/editor/autosave`, where the editor keeps the autosaves of the project's scenes.
[[nodiscard]] auto autosaveFolder(const std::filesystem::path& project) -> std::filesystem::path;

/// @brief The autosave of the scene 'scene' (its asset ID) in 'folder': `<ID>.scene`, the ID as in files (16 hex
///        digits), or `untitled.scene` for a scene that has no file (none, or 0).
[[nodiscard]] auto autosavePath(const std::filesystem::path& folder, std::optional<std::uint64_t> scene) -> std::filesystem::path;

/// @brief Whether an autosave written at 'autosave' holds changes the scene file lacks: it is newer than the file
///        (written at 'scene'), or the scene has no file.
[[nodiscard]] auto autosaveIsNewer(std::filesystem::file_time_type autosave, std::optional<std::filesystem::file_time_type> scene)
    -> bool;

/// @brief When the autosave 'file' was written, if it is there and newer than 'sceneFile' (autosaveIsNewer; an empty
///        'sceneFile', or one that is gone, counts as none): the autosave to offer.
[[nodiscard]] auto findAutosave(const std::filesystem::path& file, const std::filesystem::path& sceneFile)
    -> std::optional<std::filesystem::file_time_type>;

/// @brief Writes the scene file of 'registry' as the scene 'id' (sceneText) to the autosave 'file', creating its
///        folder, atomically (writeFileAtomically).
[[nodiscard]] auto writeAutosave(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id,
    const std::filesystem::path& file) -> std::expected<void, std::string>;

/// @brief The text of the autosave 'file', to restore.
[[nodiscard]] auto readAutosave(const std::filesystem::path& file) -> std::expected<std::string, std::string>;

/// @brief What the recovery bar says of an autosave of 'scene' (its file name) written at 'written', local time:
///        "Unsaved changes to sponza.scene from 14:32", with the date when that is not 'today'
///        ("... from 2026-11-05 14:32").
[[nodiscard]] auto recoveryLabel(std::string_view scene, std::chrono::local_seconds written, std::chrono::local_days today)
    -> std::string;
}
