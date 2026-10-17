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
}
