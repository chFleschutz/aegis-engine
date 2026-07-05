module;
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

export module aegis.rhi:resource_pool;
import :fwd;
import :resource_handle;

export namespace aegis::rhi
{
template<typename T>
class ResourcePool
{
public:
    static constexpr std::size_t DefaultCapacity{ 256 };

    ResourcePool()
    {
        m_slots.reserve(DefaultCapacity);
        m_freeSlots.reserve(DefaultCapacity);
    }

    [[nodiscard]] auto get(ResourceHandle<T> handle) -> T&
    {
        auto& slot = getSlot(handle);
        assert(slot.resource.has_value() && "Resource has been invalidated");
        assert(slot.generation == handle.generation() && "Handle is out of date");

        return slot.resource.value();
    }

    [[nodiscard]] auto get(ResourceHandle<T> handle) const -> const T&
    {
        auto& slot = getSlot(handle);
        assert(slot.resource.has_value() && "Resource has been invalidated");
        assert(slot.generation == handle.generation() && "Handle is out of date");

        return slot.resource.value();
    }

    [[nodiscard]] auto push(T resource) -> ResourceHandle<T>
    {
        auto [index, slot] = fetchNextSlot();
        assert(index <= ResourceHandle<T>::IndexMask);
        assert(!slot.resource.has_value());

        slot.resource = std::move(resource);
        return ResourceHandle<T>{ index, slot.generation };
    }

    auto pop(ResourceHandle<T> handle) -> T
    {
        auto& slot = getSlot(handle);
        assert(slot.resource.has_value());
        assert(slot.generation == handle.generation());

        m_freeSlots.emplace_back(handle.index());
        T resource = std::move(*slot.resource);
        slot.resource = std::nullopt;
        slot.generation = (slot.generation + 1) % ResourceHandle<T>::GenerationMask;
        return resource;
    }

    /// @brief Replaces the resource at the handle with the newResource and returns the old resource
    /// @note This function does not invalidate the handle (no generation bump)
    auto replace(ResourceHandle<T> handle, T newResource) -> T
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

    auto getSlot(ResourceHandle<T> handle) -> Slot&
    {
        assert(handle.isValid());
        assert(handle.index() < static_cast<std::uint32_t>(m_slots.size()));

        return m_slots[handle.index()];
    }

    auto getSlot(ResourceHandle<T> handle) const -> const Slot&
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

    std::vector<Slot> m_slots{};
    std::vector<std::uint32_t> m_freeSlots{};
};
}
