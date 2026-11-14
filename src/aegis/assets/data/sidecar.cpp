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

// A part's settings: only the ones that differ from the defaults.
struct PartSettingsFile
{
    std::optional<std::string> upAxis;
    std::optional<std::string> handedness;
    std::optional<double> scale;
    std::optional<bool> strictNames;
    std::optional<bool> srgb;
};

struct PartFile
{
    std::string id;
    PartSettingsFile settings;
};

// A part's entry: its ID, or its ID and settings.
using EntryFile = std::variant<std::string, PartFile>;

struct SidecarFile
{
    std::string id;
    std::string importer;
    std::uint32_t importerVersion{ 1 };
    SettingsFile settings;
    std::map<std::string, EntryFile> subAssets;
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

auto toSettings(const PartSettingsFile& file) -> ImportSettings
{
    ImportSettings settings;
    settings.upAxis = file.upAxis.value_or(settings.upAxis);
    settings.handedness = file.handedness.value_or(settings.handedness);
    settings.scale = file.scale.value_or(settings.scale);
    settings.strictNames = file.strictNames.value_or(settings.strictNames);
    settings.srgb = file.srgb;
    return settings;
}

auto toFile(const ImportSettings& settings) -> PartSettingsFile
{
    const ImportSettings defaults;
    PartSettingsFile file;
    if (settings.upAxis != defaults.upAxis)
        file.upAxis = settings.upAxis;
    if (settings.handedness != defaults.handedness)
        file.handedness = settings.handedness;
    if (settings.scale != defaults.scale)
        file.scale = settings.scale;
    if (settings.strictNames != defaults.strictNames)
        file.strictNames = settings.strictNames;
    file.srgb = settings.srgb;
    return file;
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
struct glz::meta<aegis::assets::PartSettingsFile>
{
    using T = aegis::assets::PartSettingsFile;
    static constexpr auto value = object("upAxis", &T::upAxis, "handedness", &T::handedness, "scale", &T::scale, "strictNames", &T::strictNames, "srgb", &T::srgb);
};

template<>
struct glz::meta<aegis::assets::PartFile>
{
    using T = aegis::assets::PartFile;
    static constexpr auto value = object("id", &T::id, "settings", &T::settings);
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
auto textureSrgb(const ImportSettings& settings, bool usage) -> bool
{
    return settings.srgb.value_or(usage);
}

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
    for (const auto& [key, entry] : file.subAssets)
    {
        const auto* withSettings = std::get_if<PartFile>(&entry);
        auto part = parseId(withSettings ? withSettings->id : std::get<std::string>(entry), std::format("subAssets['{}']", key));
        if (!part)
            return std::unexpected{ std::move(part.error()) };
        sidecar.subAssets.emplace(key, *part);
        if (withSettings)
            sidecar.partSettings.emplace(key, toSettings(withSettings->settings));
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
    {
        if (const auto settings = sidecar.partSettings.find(key); settings != sidecar.partSettings.end())
            file.subAssets.emplace(key, PartFile{ id.toString(), toFile(settings->second) });
        else
            file.subAssets.emplace(key, id.toString());
    }

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
