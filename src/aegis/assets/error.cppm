export module aegis.assets:error;
import std;
import aegis.rhi;

export namespace aegis::assets
{
enum class ErrorCode
{
    FileNotFound,
    DecodeFailed,      // The file exists, but its contents could not be parsed
    UnsupportedFormat, // Parsed, but this format or feature is not supported (yet)
    InvalidData,       // Parsed, but required data is missing or inconsistent
    InvalidArgument,   // A caller bug, e.g. an unknown material parameter or data that does not match its Desc
    GpuFailure,        // An rhi call failed; Error::rhi holds its error
};

struct Error
{
    ErrorCode code;
    std::string message;
    std::optional<rhi::Error> rhi;
};

[[nodiscard]] inline auto makeError(ErrorCode code, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message) } };
}

[[nodiscard]] inline auto makeError(const rhi::Error& error, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = ErrorCode::GpuFailure, .message = std::move(message), .rhi = error } };
}

/// @brief Name of an error code as written by describe(), e.g. "FileNotFound".
[[nodiscard]] constexpr auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::FileNotFound: return "FileNotFound";
    case ErrorCode::DecodeFailed: return "DecodeFailed";
    case ErrorCode::UnsupportedFormat: return "UnsupportedFormat";
    case ErrorCode::InvalidData: return "InvalidData";
    case ErrorCode::InvalidArgument: return "InvalidArgument";
    case ErrorCode::GpuFailure: return "GpuFailure";
    }
    return "Unknown";
}

/// @brief Text for logs: "<code>: <message>", plus "(caused by <rhi::describe>)" when an rhi call failed.
[[nodiscard]] inline auto describe(const Error& error) -> std::string
{
    if (error.rhi)
        return std::format("{}: {} (caused by {})", errorCodeName(error.code), error.message, rhi::describe(*error.rhi));
    return std::format("{}: {}", errorCodeName(error.code), error.message);
}
}
