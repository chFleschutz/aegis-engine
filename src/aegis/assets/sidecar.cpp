module;

#include <glaze/glaze.hpp>
#include <xxhash.h>

module aegis.assets;
import std;

namespace aegis::assets
{
namespace
{
// The file shapes, with IDs as text so a bad ID is reported by name instead of by glaze.
struct SettingsFile
{
    std::string upAxis{ "Y" };
    std::string handedness{ "Right" };
    double scale{ 1.0 };
    bool strictNames{ false };
    std::optional<bool> srgb;
};

struct SidecarFile
{
    std::string id;
    std::string importer;
    std::uint32_t importerVersion{ 1 };
    SettingsFile settings;
    std::map<std::string, std::string> subAssets;
};

struct HeaderFile
{
    std::string type;
    std::string id;
    std::uint32_t version{ 0 };
};

constexpr glz::opts readOptions{ .comments = true, .error_on_unknown_keys = false };
// Two-space indentation (glaze defaults to three).
struct WriteOptions : glz::opts
{
    std::uint8_t indentation_width = 2;
};
constexpr WriteOptions writeOptions{ { .prettify = true } };

auto parseId(const std::string& text, std::string_view what) -> std::expected<AssetId, Error>
{
    if (const auto id = AssetId::parse(text))
        return *id;
    return makeError(ErrorCode::InvalidData, std::format("{} '{}' is not 16 lowercase hex digits", what, text));
}
}
}

template<>
struct glz::meta<aegis::assets::SettingsFile>
{
    using T = aegis::assets::SettingsFile;
    static constexpr auto value = object("upAxis", &T::upAxis, "handedness", &T::handedness, "scale", &T::scale, "strictNames", &T::strictNames, "srgb", &T::srgb);
};

template<>
struct glz::meta<aegis::assets::SidecarFile>
{
    using T = aegis::assets::SidecarFile;
    static constexpr auto value = object("id", &T::id, "importer", &T::importer, "importerVersion", &T::importerVersion,
        "settings", &T::settings, "subAssets", &T::subAssets);
};

template<>
struct glz::meta<aegis::assets::HeaderFile>
{
    using T = aegis::assets::HeaderFile;
    static constexpr auto value = object("type", &T::type, "id", &T::id, "version", &T::version);
};

namespace aegis::assets
{
auto Sidecar::subAssetId(const std::string& key) -> AssetId
{
    auto [it, inserted] = subAssets.try_emplace(key, AssetId{});
    if (inserted)
        it->second = deriveSubAssetId(id, key);
    return it->second;
}

auto deriveSubAssetId(AssetId file, std::string_view key) -> AssetId
{
    std::string bytes(8, '\0');
    for (std::size_t i = 0; i < 8; ++i)
        bytes[i] = static_cast<char>(file.value >> (8 * i));
    bytes += key;
    return AssetId{ XXH3_64bits(bytes.data(), bytes.size()) };
}

auto sidecarPath(const std::filesystem::path& source) -> std::filesystem::path
{
    auto path = source;
    path += ".meta";
    return path;
}

auto parseSidecar(std::string_view json) -> std::expected<Sidecar, Error>
{
    SidecarFile file;
    if (const auto result = glz::read<readOptions>(file, json); result)
        return makeError(ErrorCode::DecodeFailed, std::format("sidecar: {}", glz::format_error(result, json)));

    Sidecar sidecar;
    auto id = parseId(file.id, "id");
    if (!id)
        return std::unexpected{ std::move(id.error()) };
    sidecar.id = *id;
    sidecar.importer = std::move(file.importer);
    sidecar.importerVersion = file.importerVersion;
    sidecar.settings = ImportSettings{ std::move(file.settings.upAxis), std::move(file.settings.handedness), file.settings.scale,
        file.settings.strictNames, file.settings.srgb };
    for (const auto& [key, text] : file.subAssets)
    {
        auto part = parseId(text, std::format("subAssets['{}']", key));
        if (!part)
            return std::unexpected{ std::move(part.error()) };
        sidecar.subAssets.emplace(key, *part);
    }
    return sidecar;
}

auto writeSidecar(const Sidecar& sidecar) -> std::string
{
    SidecarFile file{
        .id = sidecar.id.toString(),
        .importer = sidecar.importer,
        .importerVersion = sidecar.importerVersion,
        .settings = { sidecar.settings.upAxis, sidecar.settings.handedness, sidecar.settings.scale, sidecar.settings.strictNames, sidecar.settings.srgb },
    };
    for (const auto& [key, id] : sidecar.subAssets)
        file.subAssets.emplace(key, id.toString());

    std::string json;
    (void)glz::write<writeOptions>(file, json);
    json.push_back('\n');
    return json;
}

auto parseNativeHeader(std::string_view json) -> std::expected<NativeHeader, Error>
{
    HeaderFile file;
    if (const auto result = glz::read<readOptions>(file, json); result)
        return makeError(ErrorCode::DecodeFailed, std::format("native file header: {}", glz::format_error(result, json)));
    auto id = parseId(file.id, "id");
    if (!id)
        return std::unexpected{ std::move(id.error()) };
    if (file.type.empty())
        return makeError(ErrorCode::InvalidData, "native file header has no type");
    return NativeHeader{ std::move(file.type), *id, file.version };
}
}
