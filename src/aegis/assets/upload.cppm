module aegis.assets:upload;
import std;
import aegis.rhi;

namespace aegis::assets::detail
{
/// @brief Device::upload, retried while the staging ring is full (ErrorCode::NotReady). Each flush
///        either submits the open batch or reclaims the oldest in-flight one, so a retry makes
///        progress; blocking is fine for asset loading.
/// @note The upload completes with a later Device::flushUploads; nothing here waits for the GPU.
template<typename Handle, typename... Args>
auto uploadBlocking(rhi::Device& device, Handle dst, const Args&... args) -> std::expected<void, rhi::Error>
{
    // Bounded so a broken invariant surfaces as an error instead of a hang: an empty ring fits
    // anything below the overflow threshold, and anything above it bypasses the ring.
    constexpr int maxAttempts{ 16 };
    for (int attempt = 1;; ++attempt)
    {
        auto result = device.upload(dst, args...);
        if (result || result.error().code != rhi::ErrorCode::NotReady || attempt == maxAttempts)
            return result;
        std::ignore = device.flushUploads();
    }
}
}
