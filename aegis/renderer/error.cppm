export module aegis.renderer:error;
import std;
import aegis.rhi;

export namespace aegis::renderer
{
enum class ErrorCode
{
    RHIInitializationFailed,
    SwapchainOutOfDate,
    FrameBeginFailed,
    QueueSubmitFailed,
    SwapchainPresentFailed,
    SwapchainRecreateFailed,
    ResourceRecreateFailed,
};

struct Error
{
    ErrorCode code;
    std::string message;
    /// @brief The rhi error that caused this one, if any. Copied, never turned into text.
    std::optional<rhi::Error> rhi;
};

[[nodiscard]] inline auto makeError(ErrorCode code, std::string message = {}) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message), .rhi = std::nullopt } };
}

/// @brief Wraps an rhi error: 'message' adds context, the cause is kept as it is.
[[nodiscard]] inline auto makeError(ErrorCode code, std::string message, const rhi::Error& cause) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message), .rhi = cause } };
}

/// @brief Name of an error code as written by describe(), e.g. "SwapchainOutOfDate".
[[nodiscard]] constexpr auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::RHIInitializationFailed: return "RHIInitializationFailed";
    case ErrorCode::SwapchainOutOfDate: return "SwapchainOutOfDate";
    case ErrorCode::FrameBeginFailed: return "FrameBeginFailed";
    case ErrorCode::QueueSubmitFailed: return "QueueSubmitFailed";
    case ErrorCode::SwapchainPresentFailed: return "SwapchainPresentFailed";
    case ErrorCode::SwapchainRecreateFailed: return "SwapchainRecreateFailed";
    case ErrorCode::ResourceRecreateFailed: return "ResourceRecreateFailed";
    }
    return "Unknown";
}

/// @brief Text for logs: "<code name>: <message>", then " (caused by <rhi::describe>)" if there is a cause.
[[nodiscard]] inline auto describe(const Error& error) -> std::string
{
    auto text = std::string{ errorCodeName(error.code) };
    if (!error.message.empty())
        text += std::format(": {}", error.message);
    if (error.rhi)
        text += std::format(" (caused by {})", rhi::describe(*error.rhi));
    return text;
}
}
