module aegis.asset_library;

import std;
import aegis.assets;
import aegis.core;

namespace aegis
{
auto AssetCache::claimName(std::string_view name, std::type_index type, bool hasLoader) -> std::expected<void, AssetLibraryError>
{
    if (const auto it = m_names.find(name); it != m_names.end() && it->second != type)
        return makeError(AssetLibraryErrorCode::AlreadyRegistered, std::format("another C++ type is the asset type '{}'", name));
    if (hasLoader)
        return makeError(AssetLibraryErrorCode::AlreadyRegistered, std::format("the asset type '{}' has an asset loader", name));
    m_names.emplace(std::string{ name }, type);
    return {};
}

void AssetCache::logFailure(std::string_view assetType, assets::AssetId id, const std::string& message, bool fallback)
{
    log::warn("Failed to load {} {}: {}{}", assetType, id, message, fallback ? "; using a fallback" : "");
}
}
