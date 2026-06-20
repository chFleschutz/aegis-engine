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

template<typename T, std::size_t ChunkSize>
class ResourcePool
{
public:
    [[nodiscard]] auto totalSlotCount() const -> std::uint32_t
    {
        return static_cast<std::uint32_t>(m_chunks.size() * ChunkSize);
    }

    [[nodiscard]] auto get(Handle<T> handle) -> T&
    {
        assert(handle.generation() == slot(handle.index()).generation);
        assert(slot(handle.index()).resource.has_value());
        return slot(handle.index()).resource.value();
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
        auto& slot = slot(handle.index());
        if (slot.generation != handle.generation())
            return;

        slot.resource = std::nullopt;
        slot.generation += 1;

        m_freeSlots.emplace_back(handle.index());
    }

private:
    struct Slot
    {
        std::optional<T> resource;
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
