export module aegis.rhi:error;
import std;
import :common;

export namespace aegis::rhi
{
struct Error
{
    ErrorCode code;
#ifndef NDEBUG
    std::source_location location;
#endif
};

// clang-format off: it can't lay out a parameter list split by #ifndef
[[nodiscard]] inline auto makeError(ErrorCode code
#ifndef NDEBUG
    ,
    std::source_location loc = std::source_location::current()
#endif
) noexcept -> std::unexpected<Error>
// clang-format on
{
    return std::unexpected{
        Error{
            .code = code,
#ifndef NDEBUG
            .location = std::move(loc)
#endif
        }
    };
}

/// @brief Name of an error code as written by describe(), e.g. "OutOfDate".
[[nodiscard]] constexpr auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::Unknown: return "Unknown";
    case ErrorCode::DeviceLost: return "DeviceLost";
    case ErrorCode::SurfaceLost: return "SurfaceLost";
    case ErrorCode::OutOfDate: return "OutOfDate";
    case ErrorCode::OutOfHostMemory: return "OutOfHostMemory";
    case ErrorCode::OutOfDeviceMemory: return "OutOfDeviceMemory";
    case ErrorCode::InitializationFailed: return "InitializationFailed";
    case ErrorCode::NotReady: return "NotReady";
    case ErrorCode::FormatNotSupported: return "FormatNotSupported";
    case ErrorCode::OutOfDescriptors: return "OutOfDescriptors";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    }
    return "Unknown";
}

/// @brief One-line text for logs: the code name, plus "at file:line (function)" in debug builds.
[[nodiscard]] inline auto describe(const Error& error) -> std::string
{
#ifndef NDEBUG
    return std::format("{} at {}:{} ({})",
        errorCodeName(error.code),
        error.location.file_name(),
        error.location.line(),
        error.location.function_name());
#else
    return std::string{ errorCodeName(error.code) };
#endif
}
}
