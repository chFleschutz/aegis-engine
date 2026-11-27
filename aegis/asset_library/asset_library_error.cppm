export module aegis.asset_library:error;

import std;
import aegis.assets;
import aegis.serialize;

export namespace aegis
{
enum class AssetLibraryErrorCode
{
    NotFound,          // No asset has this path or ID
    WrongType,         // The asset is not of the kind asked for (a texture where a scene file is needed)
    ReadFailed,        // A file or the asset database could not be read, parsed, scanned or updated
    CreateFailed,      // A device resource could not be created: the default assets, the IBL pipelines, a material
    InstantiateFailed, // A scene could not be loaded or its entities could not be created
    InvalidValue,      // A value an asset cannot take: a material parameter the template lacks or of another kind
};

struct AssetLibraryError
{
    AssetLibraryErrorCode code;
    std::string message;
    std::optional<assets::Error> assets;       // The asset module's error, when it is the cause
    std::optional<serialize::Error> serialize; // The serializer's error, when it is the cause
};

[[nodiscard]] inline auto makeError(AssetLibraryErrorCode code, std::string message) -> std::unexpected<AssetLibraryError>
{
    return std::unexpected{ AssetLibraryError{ .code = code, .message = std::move(message) } };
}

[[nodiscard]] inline auto makeError(AssetLibraryErrorCode code, const assets::Error& cause, std::string message) -> std::unexpected<AssetLibraryError>
{
    return std::unexpected{ AssetLibraryError{ .code = code, .message = std::move(message), .assets = cause } };
}

[[nodiscard]] inline auto makeError(AssetLibraryErrorCode code, const serialize::Error& cause, std::string message) -> std::unexpected<AssetLibraryError>
{
    return std::unexpected{ AssetLibraryError{ .code = code, .message = std::move(message), .serialize = cause } };
}

/// @brief Name of an error code as written by describe(), e.g. "NotFound".
[[nodiscard]] constexpr auto errorCodeName(AssetLibraryErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case AssetLibraryErrorCode::NotFound: return "NotFound";
    case AssetLibraryErrorCode::WrongType: return "WrongType";
    case AssetLibraryErrorCode::ReadFailed: return "ReadFailed";
    case AssetLibraryErrorCode::CreateFailed: return "CreateFailed";
    case AssetLibraryErrorCode::InstantiateFailed: return "InstantiateFailed";
    case AssetLibraryErrorCode::InvalidValue: return "InvalidValue";
    }
    return "Unknown";
}

/// @brief The one place an AssetLibraryError is turned into text: "<code>: <message>", then " (caused by <cause>)".
[[nodiscard]] auto describe(const AssetLibraryError& error) -> std::string;
}
