module;
#include <cassert>

export module aegis.rhi:resource_pool;
import std;
import :fwd;
import :resource_handle;

export namespace aegis::rhi
{
/// @brief Generational slot storage. Slots live in fixed-size chunks that never move, so a reference
///        returned by get() stays valid while the pool grows, until its resource is popped or replaced.
template<typename T>
class ResourcePool
{
public:
    static constexpr std::uint32_t ChunkSize{ 256 };

    ResourcePool() { m_freeSlots.reserve(ChunkSize); }

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
        // Not a structured binding: MSVC resolves std::pair's tuple protocol in the instantiating
        // translation unit, which need not include <utility>.
        const auto next = fetchNextSlot();
        const auto index = next.first;
        auto& slot = next.second;
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

    using Chunk = std::array<Slot, ChunkSize>;

    auto getSlot(ResourceHandle<T> handle) -> Slot&
    {
        assert(handle.isValid());
        assert(handle.index() < m_slotCount);

        return slotAt(handle.index());
    }

    auto getSlot(ResourceHandle<T> handle) const -> const Slot&
    {
        assert(handle.isValid());
        assert(handle.index() < m_slotCount);

        return slotAt(handle.index());
    }

    auto slotAt(std::uint32_t index) -> Slot& { return (*m_chunks[index / ChunkSize])[index % ChunkSize]; }
    auto slotAt(std::uint32_t index) const -> const Slot& { return (*m_chunks[index / ChunkSize])[index % ChunkSize]; }

    auto fetchNextSlot() -> std::pair<std::uint32_t, Slot&>
    {
        if (m_freeSlots.empty())
        {
            auto index = m_slotCount;
            if (index % ChunkSize == 0)
                m_chunks.push_back(std::make_unique<Chunk>());

            ++m_slotCount;
            return { index, slotAt(index) };
        }

        auto index = m_freeSlots.back();
        m_freeSlots.pop_back();
        return { index, slotAt(index) };
    }

    // Growing appends a chunk; existing chunks, and the slots in them, never move.
    std::vector<std::unique_ptr<Chunk>> m_chunks{};
    std::uint32_t m_slotCount{ 0 };
    std::vector<std::uint32_t> m_freeSlots{};
};
}
