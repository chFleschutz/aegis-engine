export module aegis.paths;

import std;

/// Where the engine finds its compiled shaders and its assets at run time.
///
/// Both directories are fixed at configure time (inside the build and source trees) and can be
/// overridden per process with the environment variables AEGIS_SHADER_DIR and AEGIS_ASSETS_DIR.
/// Non-ASCII paths are handled as UTF-8 (compile-time value) or in the native encoding (variable).
export namespace aegis::paths
{
/// @brief Directory holding the compiled shaders (`<name>.slang.spv`, in the shaders/ layout).
[[nodiscard]] auto shaders() -> const std::filesystem::path&;

/// @brief Root of the aegis-assets submodule.
[[nodiscard]] auto assets() -> const std::filesystem::path&;
}
