module;

#include <cstdlib>

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

auto environment(const char* name) -> std::filesystem::path
{
#ifdef _WIN32
    wchar_t* value = nullptr;
    std::size_t length = 0;
    std::filesystem::path result;
    if (_wdupenv_s(&value, &length, std::filesystem::path{ name }.c_str()) == 0 && value)
    {
        result = value;
        std::free(value);
    }
    return result;
#else
    if (const char* value = std::getenv(name); value && *value)
        return value;
    return {};
#endif
}
}

void EditorPreferences::addRecentProject(const std::filesystem::path& project, std::size_t limit)
{
    const auto text = project.generic_u8string();
    const std::string entry{ text.begin(), text.end() };
    std::erase(recentProjects, entry);
    recentProjects.insert(recentProjects.begin(), entry);
    if (recentProjects.size() > limit)
        recentProjects.resize(limit);
}

void EditorPreferences::clamp()
{
    const EditorPreferences defaults;
    const auto fix = [](float& value, float low, float high, float fallback) {
        value = std::isfinite(value) ? std::clamp(value, low, high) : fallback;
    };
    fix(cameraSpeed, 0.01f, 1000.0f, defaults.cameraSpeed);
    fix(snapTranslate, 0.001f, 1000.0f, defaults.snapTranslate);
    fix(snapRotate, 0.01f, 180.0f, defaults.snapRotate);
    fix(snapScale, 0.001f, 100.0f, defaults.snapScale);
    fix(uiScale, 0.5f, 4.0f, defaults.uiScale);
}

auto preferencesPath() -> std::filesystem::path
{
#ifdef _WIN32
    if (auto base = environment("APPDATA"); !base.empty())
        return base / "Aegis" / "editor.json";
#else
    if (auto base = environment("XDG_CONFIG_HOME"); !base.empty())
        return base / "aegis" / "editor.json";
    if (auto home = environment("HOME"); !home.empty())
        return home / ".config" / "aegis" / "editor.json";
#endif
    return {};
}

auto loadPreferences(const std::filesystem::path& file) -> std::expected<EditorPreferences, std::string>
{
    std::error_code error;
    if (!std::filesystem::exists(file, error))
        return EditorPreferences{};

    std::ifstream stream{ file, std::ios::binary };
    if (!stream)
        return std::unexpected{ std::format("cannot open '{}'", file.string()) };
    const std::string text{ std::istreambuf_iterator<char>{ stream }, std::istreambuf_iterator<char>{} };

    EditorPreferences preferences;
    if (const auto parsed = glz::read<readOptions>(preferences, text); parsed)
        return std::unexpected{ std::format("'{}': {}", file.string(), glz::format_error(parsed, text)) };
    preferences.clamp();
    return preferences;
}

auto savePreferences(const EditorPreferences& preferences, const std::filesystem::path& file) -> std::expected<void, std::string>
{
    std::string text;
    if (const auto written = glz::write<writeOptions>(preferences, text); written)
        return std::unexpected{ std::format("cannot serialize the preferences: {}", glz::format_error(written, text)) };

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
