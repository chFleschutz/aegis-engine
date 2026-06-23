module;
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <ranges>
#include <vector>

export module aegis.rhi:resource_pool;

export namespace aegis::rhi
{
template<typename T>
struct Handle
{
    static constexpr std::uint32_t InvalidValue{ std::numeric_limits<std::uint32_t>::max() };
    static constexpr std::uint32_t IndexBits{ 20 };
    static constexpr std::uint32_t GenerationBits{ 12 };
    static constexpr std::uint32_t IndexMask{ (1u << IndexBits) - 1 };
    static constexpr std::uint32_t GenerationMask{ (1u << GenerationBits) - 1 };

    static_assert(IndexBits + GenerationBits == 32);

    // Packed as | 12-bit generation | 20-bit index |
    std::uint32_t value{ InvalidValue };

    Handle() = default;

    Handle(std::uint32_t index, std::uint32_t generation) :
        value{ (generation << IndexBits) | index }
    {
        assert(index <= IndexMask);
        assert(generation <= GenerationMask);
    }

    auto operator<=>(const Handle&) const = default;

    [[nodiscard]] auto isValid() const -> bool { return value != InvalidValue; }
    [[nodiscard]] auto index() const -> std::uint32_t { return value & IndexMask; }
    [[nodiscard]] auto generation() const -> std::uint32_t { return (value >> IndexBits) & GenerationMask; }
};

template<typename T>
class ResourcePool
{
public:
    static constexpr std::size_t DefaultCapacity{ 256 };

    ResourcePool()
    {
        m_slots.resize(DefaultCapacity);
        m_freeSlots.reserve(DefaultCapacity);
    }

    [[nodiscard]] auto get(Handle<T> handle) -> T&
    {
        auto& slot = getSlot(handle);
        assert(slot.resource.has_value() && "Resource has been invalidated");
        assert(slot.generation == handle.generation() && "Handle is out of date");

        return slot.resource.value();
    }

    [[nodiscard]] auto allocate(T resource) -> Handle<T>
    {
        auto [index, slot] = fetchNextSlot();
        assert(index <= Handle<T>::IndexMask);
        assert(!slot.resource.has_value());

        slot.resource = std::move(resource);
        return Handle<T>{ index, slot.generation };
    }

    auto free(Handle<T> handle) -> void
    {
        auto& slot = getSlot(handle);
        if (slot.generation != handle.generation())
            return;

        slot.resource = std::nullopt;
        slot.generation = (slot.generation + 1) % Handle<T>::GenerationMask;

        m_freeSlots.emplace_back(handle.index());
    }

    /// @brief Replaces the resource at the handle with the newResource and returns the old resource
    /// @note This function does not invalidate the handle (no generation bump)
    auto replace(Handle<T> handle, T newResource) -> T
    {
        auto& slot = getSlot(handle);
        assert(slot.generation == handle.generation());
        assert(slot.resource.has_value());

        return std::move(std::exchange(slot.resource, std::move(newResource)).value());
    }

private:
    struct Slot
    {
        std::optional<T> resource;
        std::uint32_t generation{ 0 };
    };

    auto getSlot(Handle<T> handle) -> Slot&
    {
        assert(handle.isValid());
        assert(handle.index() < static_cast<std::uint32_t>(m_slots.size()));

        return m_slots[handle.index()];
    }

    auto fetchNextSlot() -> std::pair<std::uint32_t, Slot&>
    {
        if (m_freeSlots.empty())
        {
            auto index = static_cast<std::uint32_t>(m_slots.size());
            auto& slot = m_slots.emplace_back();
            return { index, slot };
        }

        auto index = m_freeSlots.back();
        m_freeSlots.pop_back();
        return { index, m_slots[index] };
    }

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeSlots;
};
}
