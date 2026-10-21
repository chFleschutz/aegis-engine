export module aegis.paths;

import std;

/// Where the engine finds its compiled shaders and its assets at run time.
///
/// The shaders and the engine content are looked up like a game's project (see locate): the environment
/// variable AEGIS_SHADER_DIR or AEGIS_CONTENT_DIR, then `shaders/` or `content/` next to the executable,
/// then the directory fixed at configure time (inside the build and source trees). The samples and the
/// cache are fixed at configure time and can be overridden with AEGIS_ASSETS_DIR and AEGIS_CACHE_DIR.
/// Non-ASCII paths are handled as UTF-8 (compile-time value) or in the native encoding (variable).
export namespace aegis::paths
{
/// @brief Where to look for one data folder, in order.
struct Lookup
{
    /// @brief Named explicitly (`--project`, an environment variable); empty for none.
    std::filesystem::path given;
    /// @brief The folder next to the executable, for a copied or installed game; empty for none.
    std::filesystem::path besideExecutable;
    /// @brief The configure-time path of a development build; empty for none.
    std::filesystem::path configured;
};

/// @brief The data folder 'lookup' names: 'given' if set, else the first of 'besideExecutable' and
///        'configured' that 'isDirectory' accepts.
/// @return An error naming the folders tried when 'given' is not a directory or neither of the others is.
[[nodiscard]] auto locate(const Lookup& lookup, const std::function<bool(const std::filesystem::path&)>& isDirectory)
    -> std::expected<std::filesystem::path, std::string>;

/// @brief The directory of the running executable; empty if the platform doesn't say.
[[nodiscard]] auto executableDirectory() -> const std::filesystem::path&;

/// @brief Directory holding the compiled shaders (`<name>.slang.spv`, in the shaders/ layout).
[[nodiscard]] auto shaders() -> const std::filesystem::path&;

/// @brief Root of the aegis-assets submodule.
[[nodiscard]] auto assets() -> const std::filesystem::path&;

/// @brief The engine content: normal assets (default textures, the fallback checker and cube) with their
///        sidecars, in the engine repository's `content/` folder. Read-only.
[[nodiscard]] auto content() -> const std::filesystem::path&;

/// @brief Where the asset index is kept (`<build>/.aegis`). Never committed.
[[nodiscard]] auto cache() -> const std::filesystem::path&;
}
