module aegis.editor.core;
import std;
import aegis.reflect;
import aegis.world;

namespace aegis::editor
{
auto autosaveFolder(const std::filesystem::path& project) -> std::filesystem::path
{
    return project / ".aegis" / "editor" / "autosave";
}

auto autosavePath(const std::filesystem::path& folder, std::optional<std::uint64_t> scene) -> std::filesystem::path
{
    if (!scene || *scene == 0)
        return folder / "untitled.scene";
    return folder / std::format("{:016x}.scene", *scene);
}

auto autosaveIsNewer(std::filesystem::file_time_type autosave, std::optional<std::filesystem::file_time_type> scene) -> bool
{
    return !scene || autosave > *scene;
}

auto findAutosave(const std::filesystem::path& file, const std::filesystem::path& sceneFile) -> std::optional<std::filesystem::file_time_type>
{
    std::error_code error;
    if (!std::filesystem::is_regular_file(file, error))
        return std::nullopt;
    const auto written = std::filesystem::last_write_time(file, error);
    if (error)
        return std::nullopt;
    std::optional<std::filesystem::file_time_type> scene;
    if (!sceneFile.empty())
    {
        if (const auto time = std::filesystem::last_write_time(sceneFile, error); !error)
            scene = time;
    }
    return autosaveIsNewer(written, scene) ? std::optional{ written } : std::nullopt;
}

auto writeAutosave(world::Registry& registry, const reflect::TypeRegistry& types, std::uint64_t id, const std::filesystem::path& file)
    -> std::expected<void, std::string>
{
    std::error_code error;
    std::filesystem::create_directories(file.parent_path(), error);
    if (error)
        return std::unexpected{ std::format("Cannot create '{}': {}", file.parent_path().string(), error.message()) };
    return writeSceneFile(registry, types, id, file);
}

auto readAutosave(const std::filesystem::path& file) -> std::expected<std::string, std::string>
{
    std::ifstream stream{ file, std::ios::binary };
    if (!stream)
        return std::unexpected{ std::format("Cannot open '{}'", file.string()) };
    std::string text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };
    if (stream.bad())
        return std::unexpected{ std::format("Cannot read '{}'", file.string()) };
    return text;
}

auto recoveryLabel(std::string_view scene, std::chrono::local_seconds written, std::chrono::local_days today) -> std::string
{
    const auto day = std::chrono::floor<std::chrono::days>(written);
    const std::chrono::hh_mm_ss time{ written - day };
    const auto clock = std::format("{:02}:{:02}", time.hours().count(), time.minutes().count());
    if (day == today)
        return std::format("Unsaved changes to {} from {}", scene, clock);
    return std::format("Unsaved changes to {} from {:%F} {}", scene, std::chrono::year_month_day{ day }, clock);
}
}
