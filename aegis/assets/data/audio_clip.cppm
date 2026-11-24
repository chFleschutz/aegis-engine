export module aegis.assets.data:audio_clip;
import std;

export namespace aegis::assets
{
/// @brief A sound decoded into memory: interleaved 32-bit float samples at the source's own sample rate and channel
///        count. Made from a `.wav` or `.ogg` file by audio::decodeClip when the asset library loads it; the import
///        cache keeps the file's bytes unchanged (ImportCache::loadAudioFile).
struct AudioClip
{
    /// @brief The asset type's name, for the editor (reflect::FieldInfo::assetType)
    static constexpr std::string_view assetType{ "AudioClip" };

    std::uint32_t channels{ 0 };
    std::uint32_t sampleRate{ 0 };
    /// @brief Samples per channel: samples.size() is frames * channels.
    std::uint64_t frames{ 0 };
    std::vector<float> samples;

    /// @brief The clip's length at its own sample rate.
    [[nodiscard]] auto seconds() const -> float { return sampleRate == 0 ? 0.0f : static_cast<float>(frames) / static_cast<float>(sampleRate); }
};
}
