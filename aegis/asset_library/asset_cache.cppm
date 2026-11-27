export module aegis.asset_library:asset_cache;

import std;
import aegis.assets;
import :error;

export namespace aegis
{
/// @brief A C++ type that is an asset type: it names itself with `static constexpr std::string_view assetType`.
template<class T>
concept Asset = requires {
    { T::assetType } -> std::convertible_to<std::string_view>;
};

/// @brief The asset loader of the asset type T: how an AssetCache makes an asset of it from its ID, and what a
///        failed one holds instead.
template<class T>
struct AssetLoader
{
    /// @brief Makes the asset 'id' names, or says why it cannot (the message of the failed slot). It may load
    ///        other assets, and fill the slots of parts it makes along the way (AssetCache::fill).
    std::function<std::expected<std::shared_ptr<T>, std::string>(assets::AssetId id)> load;
    /// @brief What a failed load of 'id' holds instead (engine content, so a broken reference still draws); empty or
    ///        returning null for none.
    std::function<std::shared_ptr<T>(assets::AssetId id)> fallback;
    /// @brief Whether the loader can run now; empty for always. While it cannot (the scene loader outside a call that
    ///        names the types), a load fails without keeping the failure, so a later load tries again.
    std::function<bool()> ready;
    /// @brief Whether a failure is logged: once when the cache keeps it (its slot is filled), on every load while the
    ///        loader is not ready. Off for a type whose failures the caller returns as an error instead.
    bool logFailures{ true };
};

/// @brief The loaded assets of every asset type, one slot per type and ID, and the asset loader of each type, by
///        the asset type's name. Needs no device: AssetLibrary registers its loaders for the engine's types, a game
///        registers its own.
///
/// Loading an ID twice returns the same slot. A failed load is filled too, with its loader's fallback, so it fails
/// once; only a load that could not run (a type without a loader, a loader not ready) is not kept. Loading the same
/// ID as two types (a model file as a Model and as a Scene) gives two slots.
class AssetCache
{
public:
    /// @brief Registers how assets of the type T load.
    /// @return AlreadyRegistered if T, or another C++ type of the same asset type name, has a loader; nothing changes then.
    template<Asset T>
    [[nodiscard]] auto registerLoader(AssetLoader<T> loader) -> std::expected<void, AssetLibraryError>
    {
        auto& entry = typeEntry<T>();
        if (auto taken = claimName(T::assetType, typeid(T), entry.loader != nullptr); !taken)
            return std::unexpected{ std::move(taken.error()) };
        entry.loader = std::make_shared<const AssetLoader<T>>(std::move(loader));
        return {};
    }

    /// @brief Whether the type T has a loader.
    template<Asset T>
    [[nodiscard]] auto hasLoader() const -> bool
    {
        const auto it = m_types.find(typeid(T));
        return it != m_types.end() && it->second.loader;
    }

    /// @brief The asset 'id' names as a T, loaded by T's loader on first use. A failed load fills a failed slot with
    ///        the loader's fallback, if any. A T without a loader, or whose loader is not ready, gives a failed
    ///        reference of its own, with no fallback, that the cache does not keep.
    template<Asset T>
    auto load(assets::AssetId id) -> assets::AssetRef<T>
    {
        if (auto ref = find<T>(id))
            return *ref;
        const auto loader = std::static_pointer_cast<const AssetLoader<T>>(typeEntry<T>().loader);
        if (!loader || !loader->load)
            return notLoaded<T>(id, std::format("no asset loader for the asset type '{}'", T::assetType), true);
        if (loader->ready && !loader->ready())
            return notLoaded<T>(id, std::format("the asset loader of the asset type '{}' cannot load now", T::assetType), loader->logFailures);

        auto loaded = loader->load(id);
        if (loaded)
            return fill<T>(id, std::move(*loaded));
        auto fallback = loader->fallback ? loader->fallback(id) : nullptr;
        return fail<T>(id, std::move(loaded.error()), std::move(fallback), loader->logFailures);
    }

    /// @brief The slot of 'id' as a T, if it was loaded, failed or filled.
    template<Asset T>
    [[nodiscard]] auto find(assets::AssetId id) const -> std::optional<assets::AssetRef<T>>
    {
        const auto type = m_types.find(typeid(T));
        if (type == m_types.end())
            return std::nullopt;
        const auto slot = type->second.slots.find(id);
        if (slot == type->second.slots.end())
            return std::nullopt;
        return assets::AssetRef<T>{ id, std::static_pointer_cast<assets::AssetSlot<T>>(slot->second) };
    }

    /// @brief Puts 'value' in the slot of 'id' as a T, ready: an asset made at run time, or a part a loader made
    ///        (a model's meshes). A slot that exists keeps its references and gets the new value.
    template<Asset T>
    auto fill(assets::AssetId id, std::shared_ptr<T> value) -> assets::AssetRef<T>
    {
        const auto slot = slotOf<T>(id);
        slot->state = assets::AssetState::Ready;
        slot->value = std::move(value);
        slot->message.clear();
        return assets::AssetRef<T>{ id, slot };
    }

private:
    struct TypeEntry
    {
        std::shared_ptr<const void> loader;                               // An AssetLoader<T>, or null
        std::unordered_map<assets::AssetId, std::shared_ptr<void>> slots; // AssetSlot<T>s
    };

    template<Asset T>
    auto typeEntry() -> TypeEntry&
    {
        return m_types[typeid(T)];
    }

    template<Asset T>
    auto slotOf(assets::AssetId id) -> std::shared_ptr<assets::AssetSlot<T>>
    {
        auto& slot = typeEntry<T>().slots[id];
        if (!slot)
            slot = std::make_shared<assets::AssetSlot<T>>();
        return std::static_pointer_cast<assets::AssetSlot<T>>(slot);
    }

    template<Asset T>
    auto fail(assets::AssetId id, std::string message, std::shared_ptr<T> fallback, bool log) -> assets::AssetRef<T>
    {
        if (log)
            logFailure(T::assetType, id, message, fallback != nullptr);
        const auto slot = slotOf<T>(id);
        slot->state = assets::AssetState::Failed;
        slot->value = std::move(fallback);
        slot->message = std::move(message);
        return assets::AssetRef<T>{ id, slot };
    }

    // A failure that is not the asset's: a slot of its own, not kept.
    template<Asset T>
    static auto notLoaded(assets::AssetId id, std::string message, bool log) -> assets::AssetRef<T>
    {
        if (log)
            logFailure(T::assetType, id, message, false);
        return assets::AssetRef<T>{ id, std::make_shared<assets::AssetSlot<T>>(assets::AssetSlot<T>{ assets::AssetState::Failed, nullptr, std::move(message) }) };
    }

    // Records 'name' as the asset type name of 'type', unless another type has it or 'type' has a loader already.
    auto claimName(std::string_view name, std::type_index type, bool hasLoader) -> std::expected<void, AssetLibraryError>;
    static void logFailure(std::string_view assetType, assets::AssetId id, const std::string& message, bool fallback);

    std::unordered_map<std::type_index, TypeEntry> m_types;
    std::map<std::string, std::type_index, std::less<>> m_names; // The asset type names that have a loader
};
}
