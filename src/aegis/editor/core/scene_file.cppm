export module aegis.editor.core:scene_file;
import std;
import aegis.reflect;
import aegis.world;

export namespace aegis::editor
{
/// @brief The scene file of every entity of 'registry' as the scene 'id' (serialize::saveScene, then
///        serialize::writeScene): the same world gives the same bytes. 'registry' is only read.
[[nodiscard]] auto sceneText(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id)
    -> std::expected<std::string, std::string>;

/// @brief Writes the scene file of 'registry' (sceneText) to 'file', replacing it.
[[nodiscard]] auto writeSceneFile(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id,
    const std::filesystem::path& file) -> std::expected<void, std::string>;

/// @brief The path in the content folder that Save As writes for 'typed' (what the user entered): trimmed, with '/'
///        separators and ".scene" appended when it has another extension or none ("levels\one" is
///        "levels/one.scene").
/// @return An error for an empty or absolute path, one that leaves the content folder (".."), or a dot-file or
///         dot-folder (the scan skips those).
[[nodiscard]] auto sceneFilePath(std::string_view typed) -> std::expected<std::string, std::string>;
}
