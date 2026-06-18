module;
#include <array>
#include <cassert>
#include <cstdint>
#include <memory>
#include <ranges>
#include <vector>

export module aegis.rhi:resource_pool;

export namespace aegis::rhi
{
template<typename T>
struct Handle
{
    std::uint32_t index: 20;
    std::uint32_t generation: 12;
};

template<typename T, std::size_t ChunkSize>
    requires std::is_default_constructible_v<T>
class ResourcePool
{
public:
    [[nodiscard]] auto totalSlotCount() const -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_chunks.size() * ChunkSize);
    }

    [[nodiscard]] auto get(Handle<T> handle) -> T&
    {
        assert(handle.generation == slot(handle.index).generation);
        return slot(handle.index).resource;
    }

    auto allocate(T resource) -> Handle<T>
    {
        auto index = fetchNextFreeSlot();
        auto& slot = slot(index);
        slot.resource = std::move(resource);
        return Handle<T>{ index, slot.generation };
    }

    auto free(Handle<T> handle) -> void
    {
        auto& slot = slot(handle.index);
        if (slot.generation != handle.generation)
            return;

        slot.resource = {};
        slot.generation += 1;

        m_freeSlots.emplace_back(handle.index);
    }

private:
    struct Slot
    {
        T resource;
        std::uint32_t generation{ 0 };
    };

    struct Chunk
    {
        std::array<Slot, ChunkSize> slots;
    };

    auto slot(std::uint32_t index) -> Slot&
    {
        assert(index < totalSlotCount());
        std::uint32_t chunkIndex = index / ChunkSize;
        std::uint32_t slotIndex = index % ChunkSize;
        return m_chunks[chunkIndex].slots[slotIndex];
    }

    auto fetchNextFreeSlot() -> std::uint32_t
    {
        if (m_freeSlots.empty())
            growPool();

        auto index = m_freeSlots.back();
        m_freeSlots.pop_back();
        return index;
    }

    auto growPool() -> void
    {
        std::uint32_t startIndex = static_cast<std::uint32_t>(m_chunks.size()) * ChunkSize;
        m_chunks.emplace_back();

        m_freeSlots.reserve(m_freeSlots.size() + ChunkSize);

        auto indexRange = std::views::iota(startIndex, startIndex + ChunkSize);
        std::ranges::copy(indexRange | std::views::reverse, std::back_inserter(m_freeSlots));
    }

    std::vector<Chunk> m_chunks;
    std::vector<std::uint32_t> m_freeSlots;
};
}
