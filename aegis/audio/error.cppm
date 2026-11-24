export module aegis.audio:error;

import std;

export namespace aegis::audio
{
enum class ErrorCode
{
    DecodeFailed, // The bytes are no WAV or Ogg Vorbis file miniaudio can read
    DeviceFailed, // miniaudio's engine could not start, even without a device
    PlayFailed,   // A sound could not be created for a clip
    NoAudio,      // The registry has no AudioSystem (a world that doesn't play, or none was added)
    NotLoaded,    // The clip is not loaded (pending, or failed without a fallback), or the entity has no AudioSource
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

/// @brief Name of an error code as written by describe(), e.g. "DecodeFailed".
[[nodiscard]] constexpr auto errorCodeName(ErrorCode code) noexcept -> std::string_view
{
    switch (code)
    {
    case ErrorCode::DecodeFailed: return "DecodeFailed";
    case ErrorCode::DeviceFailed: return "DeviceFailed";
    case ErrorCode::PlayFailed: return "PlayFailed";
    case ErrorCode::NoAudio: return "NoAudio";
    case ErrorCode::NotLoaded: return "NotLoaded";
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
