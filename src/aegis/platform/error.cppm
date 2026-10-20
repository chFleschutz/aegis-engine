export module aegis.platform:error;

import std;

export namespace aegis::platform
{
enum class ErrorCode
{
    InitFailed,           // GLFW could not initialize, e.g. there is no display
    WindowCreationFailed, // GLFW initialized, but refused to create the window
};

struct Error
{
    ErrorCode code;
    std::string message;
};

[[nodiscard]] inline auto makeError(ErrorCode code, std::string message) -> std::unexpected<Error>
{
    return std::unexpected{ Error{ .code = code, .message = std::move(message) } };
}

/// @brief Name of an error code as written by describe(), e.g. "InitFailed".
[[nodiscard]] constexpr auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::InitFailed: return "InitFailed";
    case ErrorCode::WindowCreationFailed: return "WindowCreationFailed";
    }
    return "Unknown";
}

/// @brief Text for logs: "<code>: <message>".
[[nodiscard]] inline auto describe(const Error& error) -> std::string
{
    auto text = std::string{ errorCodeName(error.code) };
    if (!error.message.empty())
        text += std::format(": {}", error.message);
    return text;
}
}
