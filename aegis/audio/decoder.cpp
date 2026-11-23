module;

#include <miniaudio.h>

module aegis.audio;

import std;

namespace aegis::audio
{
auto decodeClip(std::span<const std::byte> bytes, std::string_view name) -> std::expected<assets::AudioClip, Error>
{
    // Channels and sample rate 0: the file's own.
    const auto config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (const auto result = ma_decoder_init_memory(bytes.data(), bytes.size(), &config, &decoder); result != MA_SUCCESS)
        return makeError(ErrorCode::DecodeFailed, std::format("'{}' is no WAV or Ogg Vorbis file ({})", name, ma_result_description(result)));

    assets::AudioClip clip{ .channels = decoder.outputChannels, .sampleRate = decoder.outputSampleRate };
    // Read to the end in chunks: a Vorbis stream's length is not always known up front.
    constexpr ma_uint64 chunkFrames{ 4096 };
    ma_result result{ MA_SUCCESS };
    while (result == MA_SUCCESS)
    {
        const auto offset = clip.samples.size();
        clip.samples.resize(offset + chunkFrames * clip.channels);
        ma_uint64 read{ 0 };
        result = ma_decoder_read_pcm_frames(&decoder, clip.samples.data() + offset, chunkFrames, &read);
        clip.samples.resize(offset + read * clip.channels);
        clip.frames += read;
    }
    ma_decoder_uninit(&decoder);

    if (result != MA_AT_END)
        return makeError(ErrorCode::DecodeFailed, std::format("Cannot decode '{}' ({})", name, ma_result_description(result)));
    if (clip.frames == 0 || clip.channels == 0)
        return makeError(ErrorCode::DecodeFailed, std::format("'{}' holds no audio", name));
    clip.samples.shrink_to_fit();
    return clip;
}
}
