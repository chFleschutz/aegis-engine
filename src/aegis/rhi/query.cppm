module;
#include <cstdint>
#include <expected>
#include <string_view>
#include <vector>

export module aegis.rhi:query;
import :common;
import :error;
import :fwd;
import vulkan;

export namespace aegis::rhi::detail
{
/// @brief Converts two raw timestamps to milliseconds.
/// @param periodNs Nanoseconds per timestamp tick (the device's timestampPeriod).
/// @param validBits Meaningful low bits of a timestamp (the queue family's timestampValidBits). The
///                  counter wraps at that width, so the delta is taken modulo 2^validBits.
/// @note Takes plain values rather than vk:: types so a test TU can call it.
[[nodiscard]] auto timestampDeltaMs(std::uint64_t begin, std::uint64_t end, float periodNs,
    std::uint32_t validBits) -> double;
}

export namespace aegis::rhi
{
/// @brief A pool of GPU timestamp queries for the graphics queue. Record with
///        CommandBuffer::resetQueries and CommandBuffer::writeTimestamp, then read the results back
///        once the GPU has executed those commands.
class QueryPool
{
    friend Device;

public:
    struct Desc
    {
        std::string_view name;
        std::uint32_t count;
    };

    [[nodiscard]] auto vk() const noexcept -> vk::QueryPool { return *m_pool; }
    [[nodiscard]] auto count() const noexcept -> std::uint32_t { return m_count; }

    /// @brief Reads the raw timestamps [first, first + count). Never blocks.
    /// @return ErrorCode::NotReady if the GPU has not written all of them yet.
    /// @warning The queries must have been reset (CommandBuffer::resetQueries) and executed since.
    [[nodiscard]] auto readTimestamps(std::uint32_t first, std::uint32_t count) const
        -> std::expected<std::vector<std::uint64_t>, Error>;

    /// @brief Milliseconds between two raw timestamps read from this pool.
    [[nodiscard]] auto elapsedMs(std::uint64_t begin, std::uint64_t end) const noexcept -> double;

private:
    [[nodiscard]] static auto create(const Device& device, const Desc& desc, std::uint32_t queueFamily)
        -> std::expected<QueryPool, Error>;

    QueryPool(vk::raii::QueryPool pool, std::uint32_t count, float periodNs, std::uint32_t validBits);

    vk::raii::QueryPool m_pool;
    std::uint32_t m_count;
    float m_periodNs;
    std::uint32_t m_validBits;
};
}
