module;

#include <glaze/glaze.hpp>

module aegis.editor.core;
import std;

namespace aegis::editor
{
namespace
{
constexpr glz::opts readOptions{ .comments = true, .error_on_unknown_keys = false };
// Two-space indentation (glaze defaults to three).
struct WriteOptions : glz::opts
{
    std::uint8_t indentation_width = 2;
};
constexpr WriteOptions writeOptions{ { .prettify = true } };
}

void RecentScenes::add(std::string_view path)
{
    std::erase(paths, path);
    paths.insert(paths.begin(), std::string{ path });
    if (paths.size() > limit)
        paths.resize(limit);
}

auto RecentScenes::prune(const std::function<bool(std::string_view path)>& exists) -> bool
{
    return std::erase_if(paths, [&exists](const std::string& path) { return !exists(path); }) > 0;
}

auto recentScenesPath(const std::filesystem::path& project) -> std::filesystem::path
{
    return project / ".aegis" / "editor" / "recent_scenes.json";
}

auto loadRecentScenes(const std::filesystem::path& file) -> std::expected<RecentScenes, std::string>
{
    std::error_code error;
    if (!std::filesystem::exists(file, error))
        return RecentScenes{};

    std::ifstream stream{ file, std::ios::binary };
    if (!stream)
        return std::unexpected{ std::format("cannot open '{}'", file.string()) };
    const std::string text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };

    RecentScenes read;
    if (const auto parsed = glz::read<readOptions>(read, text); parsed)
        return std::unexpected{ std::format("'{}': {}", file.string(), glz::format_error(parsed, text)) };
    // A hand-edited file: added from the oldest, so the order stays and the limit and duplicates hold
    RecentScenes scenes;
    for (const auto& path : read.paths | std::views::reverse)
        scenes.add(path);
    return scenes;
}

auto saveRecentScenes(const RecentScenes& scenes, const std::filesystem::path& file) -> std::expected<void, std::string>
{
    std::string text;
    if (const auto written = glz::write<writeOptions>(scenes, text); written)
        return std::unexpected{ std::format("cannot serialize the recent scenes: {}", glz::format_error(written, text)) };

    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error)
        return std::unexpected{ std::format("cannot create '{}': {}", file.parent_path().string(), error.message()) };

    std::ofstream stream{ file, std::ios::binary | std::ios::trunc };
    stream << text << '\n';
    if (!stream)
        return std::unexpected{ std::format("cannot write '{}'", file.string()) };
    return {};
}
}
