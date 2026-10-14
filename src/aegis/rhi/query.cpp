module;
#include <cassert>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

module aegis.rhi;
import :query;
import :debug;
import :vulkan_conversions;
import vulkan_hpp;

namespace aegis::rhi::detail
{
auto timestampDeltaMs(std::uint64_t begin, std::uint64_t end, float periodNs, std::uint32_t validBits)
    -> double
{
    const auto mask = validBits >= 64 ? ~std::uint64_t{ 0 } : (std::uint64_t{ 1 } << validBits) - 1;
    const auto ticks = (end - begin) & mask;
    return static_cast<double>(ticks) * static_cast<double>(periodNs) / 1'000'000.0;
}
}

namespace aegis::rhi
{
auto QueryPool::readTimestamps(std::uint32_t first, std::uint32_t count) const
    -> std::expected<std::vector<std::uint64_t>, Error>
{
    assert(first + count <= m_count && "Query range exceeds the pool");

    auto results = m_pool.getResults<std::uint64_t>(first, count, count * sizeof(std::uint64_t),
        sizeof(std::uint64_t), vk::QueryResultFlagBits::e64);
    if (results.result != vk::Result::eSuccess)
        return makeError(toRHI(results.result));

    return std::move(results.value);
}

auto QueryPool::elapsedMs(std::uint64_t begin, std::uint64_t end) const noexcept -> double
{
    return detail::timestampDeltaMs(begin, end, m_periodNs, m_validBits);
}

auto QueryPool::create(const Device& device, const Desc& desc, std::uint32_t queueFamily)
    -> std::expected<QueryPool, Error>
{
    const auto validBits = device.physicalDevice().getQueueFamilyProperties()[queueFamily].timestampValidBits;
    if (validBits == 0)
        return makeError(ErrorCode::InitializationFailed); // The queue does not support timestamps

    vk::QueryPoolCreateInfo poolInfo{
        .queryType = vk::QueryType::eTimestamp,
        .queryCount = desc.count,
    };

    auto pool = device->createQueryPool(poolInfo);
    if (!pool.has_value())
        return makeError(toRHI(pool.result));

    debug::setName(*device, **pool, desc.name);

    return QueryPool{
        std::move(*pool),
        desc.count,
        device.properties().core.properties.limits.timestampPeriod,
        validBits
    };
}

QueryPool::QueryPool(vk::raii::QueryPool pool, std::uint32_t count, float periodNs, std::uint32_t validBits) :
    m_pool{ std::move(pool) },
    m_count{ count },
    m_periodNs{ periodNs },
    m_validBits{ validBits }
{
}
}
