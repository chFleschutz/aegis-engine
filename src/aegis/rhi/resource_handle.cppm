module;
#include <cassert>
#include <cstdint>
#include <limits>

export module aegis.rhi:resource_handle;
import :fwd;

export namespace aegis::rhi
{
template<typename T>
struct ResourceHandle
{
    static constexpr std::uint32_t InvalidValue{ std::numeric_limits<std::uint32_t>::max() };
    static constexpr std::uint32_t IndexBits{ 20 };
    static constexpr std::uint32_t GenerationBits{ 12 };
    static constexpr std::uint32_t IndexMask{ (1u << IndexBits) - 1 };
    static constexpr std::uint32_t GenerationMask{ (1u << GenerationBits) - 1 };

    static_assert(IndexBits + GenerationBits == 32);

    // Packed as | 12-bit generation | 20-bit index |
    std::uint32_t value{ InvalidValue };

    ResourceHandle() = default;

    ResourceHandle(std::uint32_t index, std::uint32_t generation) :
        value{ (generation << IndexBits) | index }
    {
        assert(index <= IndexMask);
        assert(generation <= GenerationMask);
    }

    auto operator<=>(const ResourceHandle&) const = default;

    [[nodiscard]] auto isValid() const -> bool { return value != InvalidValue; }
    [[nodiscard]] auto index() const -> std::uint32_t { return value & IndexMask; }
    [[nodiscard]] auto generation() const -> std::uint32_t { return (value >> IndexBits) & GenerationMask; }
};

using BufferHandle = ResourceHandle<Buffer>;
using ImageHandle = ResourceHandle<Image>;
using ImageViewHandle = ResourceHandle<ImageView>;
}
