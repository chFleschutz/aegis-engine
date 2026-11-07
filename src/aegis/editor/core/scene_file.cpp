module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.serialize;
import aegis.world;

namespace aegis::editor
{
namespace
{
// `.<name>.tmp` next to 'file'.
auto tempFile(const std::filesystem::path& file) -> std::filesystem::path
{
    std::filesystem::path name{ "." };
    name += file.filename();
    name += ".tmp";
    return file.parent_path() / name;
}
}

auto writeFileAtomically(const std::filesystem::path& file, std::string_view text) -> std::expected<void, std::string>
{
    const auto temp = tempFile(file);
    std::error_code ignored;
    std::ofstream stream{ temp, std::ios::binary | std::ios::trunc };
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    stream.close();
    if (!stream)
    {
        std::filesystem::remove(temp, ignored);
        return std::unexpected{ std::format("Cannot write '{}'", temp.string()) };
    }
    // Replaces 'file' on both platforms (MoveFileEx's MOVEFILE_REPLACE_EXISTING on Windows)
    std::error_code error;
    std::filesystem::rename(temp, file, error);
    if (error)
    {
        std::filesystem::remove(temp, ignored);
        return std::unexpected{ std::format("Cannot replace '{}': {}", file.string(), error.message()) };
    }
    return {};
}

auto sceneText(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id) -> std::expected<std::string, std::string>
{
    const auto scene = serialize::saveScene(registry, types, id);
    if (!scene)
        return std::unexpected{ std::format("Cannot capture the scene: {}", serialize::describe(scene.error())) };
    return serialize::writeScene(*scene);
}

auto writeSceneFile(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id, const std::filesystem::path& file)
    -> std::expected<void, std::string>
{
    const auto text = sceneText(registry, types, id);
    if (!text)
        return std::unexpected{ text.error() };
    return writeFileAtomically(file, *text);
}

auto sceneFilePath(std::string_view typed) -> std::expected<std::string, std::string>
{
    const auto first = typed.find_first_not_of(" \t");
    if (first == std::string_view::npos)
        return std::unexpected{ std::string{ "No file name" } };
    std::string path{ typed.substr(first, typed.find_last_not_of(" \t") - first + 1) };
    std::ranges::replace(path, '\\', '/');
    // A drive letter on any platform: the project must open on Windows too, where ':' names no file
    if (path.starts_with('/') || path.contains(':'))
        return std::unexpected{ std::format("'{}' is not a path in the content folder", path) };
    if (path.ends_with('/'))
        return std::unexpected{ std::format("'{}' names a folder", path) };
    for (const auto part : std::views::split(path, '/'))
    {
        const std::string_view name{ part.begin(), part.end() };
        if (name.empty() || name == ".")
            return std::unexpected{ std::format("'{}' has an empty folder name", path) };
        if (name.starts_with('.'))
            return std::unexpected{ std::format("'{}' leaves the content folder or names a hidden file", path) };
    }
    if (!path.ends_with(".scene"))
        path += ".scene";
    return path;
}
}
