export module aegis.assets:asset_ref;
import std;
import :asset_id;
import aegis.reflect;

export namespace aegis::assets
{
enum class AssetState
{
    Pending, // Not loaded yet (or only an ID so far); skipped when drawing
    Ready,   // Loaded
    Failed,  // Missing or broken; the slot holds a fallback from the engine content, if there is one
};

/// @brief What an AssetRef points to: the loaded asset and how loading went. Shared by every reference to
///        the same ID, so a reload swaps the contents for all of them.
template<class T>
struct AssetSlot
{
    AssetState state{ AssetState::Pending };
    std::shared_ptr<T> value;
    std::string message; // Why it failed
};

/// @brief A hard reference to an asset: its ID plus the shared slot the library loads it into. Serializers
///        write only the ID (reflect::FieldKind::AssetRef); the slot is filled by AssetLibrary::load, which
///        deduplicates by ID.
/// @note A reference made from just an ID (a scene read from a file) has no slot and counts as pending until
///       the library resolves it. A failed reference still yields its fallback, so drawing needs no check
///       beyond get().
template<class T>
class AssetRef
{
public:
    AssetRef() = default;

    /// @brief A reference that is not loaded yet.
    explicit AssetRef(AssetId id) :
        m_id{ id }
    {
    }

    AssetRef(AssetId id, std::shared_ptr<AssetSlot<T>> slot) :
        m_id{ id },
        m_slot{ std::move(slot) }
    {
    }

    /// @brief A reference to an already loaded asset.
    [[nodiscard]] static auto ready(AssetId id, std::shared_ptr<T> value) -> AssetRef
    {
        return AssetRef{ id, std::make_shared<AssetSlot<T>>(AssetSlot<T>{ AssetState::Ready, std::move(value), {} }) };
    }

    [[nodiscard]] auto id() const noexcept -> AssetId { return m_id; }
    [[nodiscard]] auto state() const noexcept -> AssetState { return m_slot ? m_slot->state : AssetState::Pending; }
    [[nodiscard]] auto slot() const noexcept -> const std::shared_ptr<AssetSlot<T>>& { return m_slot; }

    /// @brief The asset to draw with: null while pending, the fallback (if any) when failed.
    [[nodiscard]] auto get() const noexcept -> T*
    {
        return m_slot && m_slot->state != AssetState::Pending ? m_slot->value.get() : nullptr;
    }

    [[nodiscard]] auto shared() const noexcept -> std::shared_ptr<T>
    {
        return m_slot && m_slot->state != AssetState::Pending ? m_slot->value : nullptr;
    }

    [[nodiscard]] auto operator->() const noexcept -> T* { return get(); }
    [[nodiscard]] auto operator*() const noexcept -> T& { return *get(); }
    [[nodiscard]] explicit operator bool() const noexcept { return get() != nullptr; }

    /// @brief References are equal when they name the same asset.
    [[nodiscard]] friend auto operator==(const AssetRef& a, const AssetRef& b) noexcept -> bool { return a.m_id == b.m_id; }

private:
    AssetId m_id;
    std::shared_ptr<AssetSlot<T>> m_slot;
};
}

/// An AssetRef field is an asset reference for reflection: it is written as its ID and read back as a
/// reference the library has yet to resolve.
export template<class T>
struct aegis::reflect::AssetFieldTraits<aegis::assets::AssetRef<T>>
{
    static auto id(const aegis::assets::AssetRef<T>& ref) -> std::uint64_t { return ref.id().value; }
    static auto make(std::uint64_t id) -> aegis::assets::AssetRef<T> { return aegis::assets::AssetRef<T>{ aegis::assets::AssetId{ id } }; }
};
