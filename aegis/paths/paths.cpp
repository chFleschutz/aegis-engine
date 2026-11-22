module;

#include <cstdlib>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

module aegis.paths;

import std;

// AEGIS_SHADER_DIR, AEGIS_PROJECT_DIR, AEGIS_CONTENT_DIR and AEGIS_CACHE_DIR are compile definitions of
// aegis-paths; this is the only file that reads them.

namespace aegis::paths
{
namespace
{
// The environment override, if set and non-empty. Windows reads the wide environment so non-ASCII
// values survive; elsewhere bytes are taken as they are.
auto fromEnvironment(const char* name) -> std::optional<std::filesystem::path>
{
#ifdef _WIN32
    const auto wideName = std::filesystem::path{ std::string_view{ name } }.wstring();
    wchar_t* value = nullptr;
    std::size_t length = 0;
    if (_wdupenv_s(&value, &length, wideName.c_str()) == 0 && value)
    {
        std::filesystem::path result{ value };
        std::free(value);
        if (!result.empty())
            return result;
    }
#else
    if (const char* value = std::getenv(name); value && *value)
        return std::filesystem::path{ value };
#endif
    return std::nullopt;
}

// The compile definitions are UTF-8 string literals; a plain path from char* would use the ANSI code page.
auto fromLiteral(std::u8string_view utf8) -> std::filesystem::path
{
    return std::filesystem::path{ utf8 };
}

auto resolve(const char* variable, std::u8string_view fallback) -> std::filesystem::path
{
    if (auto overridden = fromEnvironment(variable))
        return *overridden;
    return fromLiteral(fallback);
}

auto isDirectory(const std::filesystem::path& path) -> bool
{
    std::error_code error;
    return std::filesystem::is_directory(path, error);
}

// The variable, then 'folder' next to the executable, then the configure-time path. When none exists the
// path still names where it should be, so the first failed load says what is missing.
auto find(const char* variable, std::string_view folder, std::u8string_view configured) -> std::filesystem::path
{
    const auto beside = executableDirectory();
    const Lookup lookup{
        .given = fromEnvironment(variable).value_or(std::filesystem::path{}),
        .besideExecutable = beside.empty() ? beside : beside / folder,
        .configured = fromLiteral(configured),
    };
    return locate(lookup, isDirectory).value_or(lookup.given.empty() ? lookup.configured : lookup.given);
}

auto findExecutableDirectory() -> std::filesystem::path
{
#ifdef _WIN32
    std::wstring buffer(MAX_PATH, wchar_t{});
    while (true)
    {
        const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0)
            return {};
        if (length < buffer.size())
        {
            buffer.resize(length);
            return std::filesystem::path{ buffer }.parent_path();
        }
        buffer.resize(buffer.size() * 2); // Truncated: a long path
    }
#else
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    return error ? std::filesystem::path{} : executable.parent_path();
#endif
}
}

auto locate(const Lookup& lookup, const std::function<bool(const std::filesystem::path&)>& isDirectory)
    -> std::expected<std::filesystem::path, std::string>
{
    // A folder named explicitly must exist: falling back would load other data than asked for.
    if (!lookup.given.empty())
    {
        if (isDirectory(lookup.given))
            return lookup.given;
        return std::unexpected{ std::format("'{}' is not a directory", lookup.given.string()) };
    }

    std::string tried;
    for (const auto* candidate : { &lookup.besideExecutable, &lookup.configured })
    {
        if (candidate->empty())
            continue;
        if (isDirectory(*candidate))
            return *candidate;
        tried += std::format("{}'{}'", tried.empty() ? "" : ", ", candidate->string());
    }
    if (tried.empty())
        return std::unexpected{ std::string{ "No folder to look in" } };
    return std::unexpected{ std::format("Found none of {}", tried) };
}

auto executableDirectory() -> const std::filesystem::path&
{
    static const auto dir = findExecutableDirectory();
    return dir;
}

auto shaders() -> const std::filesystem::path&
{
    static const auto dir = find("AEGIS_SHADER_DIR", "shaders", u8"" AEGIS_SHADER_DIR);
    return dir;
}

auto project() -> const std::filesystem::path&
{
    static const auto dir = resolve("AEGIS_PROJECT_DIR", u8"" AEGIS_PROJECT_DIR);
    return dir;
}

auto projectContent(const std::filesystem::path& project) -> std::filesystem::path
{
    return project / "content";
}

auto content() -> const std::filesystem::path&
{
    static const auto dir = find("AEGIS_CONTENT_DIR", "content", u8"" AEGIS_CONTENT_DIR);
    return dir;
}

auto cache() -> const std::filesystem::path&
{
    static const auto dir = resolve("AEGIS_CACHE_DIR", u8"" AEGIS_CACHE_DIR);
    return dir;
}

auto cacheOverride() -> const std::optional<std::filesystem::path>&
{
    static const auto dir = fromEnvironment("AEGIS_CACHE_DIR");
    return dir;
}
}
