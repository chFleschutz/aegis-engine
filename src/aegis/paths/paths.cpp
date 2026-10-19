module;

#include <cstdlib>

module aegis.paths;

import std;

// AEGIS_SHADER_DIR and AEGIS_ASSETS_DIR are compile definitions of aegis-paths; this is the only file
// that reads them.

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
}

auto shaders() -> const std::filesystem::path&
{
    static const auto dir = resolve("AEGIS_SHADER_DIR", u8"" AEGIS_SHADER_DIR);
    return dir;
}

auto assets() -> const std::filesystem::path&
{
    static const auto dir = resolve("AEGIS_ASSETS_DIR", u8"" AEGIS_ASSETS_DIR);
    return dir;
}
}
