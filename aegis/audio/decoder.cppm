export module aegis.audio:decoder;

import std;
import aegis.assets.data;
import :error;

export namespace aegis::audio
{
/// @brief Decodes the bytes of a `.wav` or `.ogg` (Vorbis) file into memory, at the file's own sample rate and
///        channel count, as 32-bit float samples.
/// @param name Names the file in the error message
/// @return ErrorCode::DecodeFailed if miniaudio cannot read the bytes or they hold no frames.
[[nodiscard]] auto decodeClip(std::span<const std::byte> bytes, std::string_view name) -> std::expected<assets::AudioClip, Error>;
}
