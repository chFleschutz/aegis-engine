export module aegis.audio:audio_engine;

import std;
import aegis.assets.data;
import aegis.math;
import :error;

export namespace aegis::audio
{
/// @brief Identifies a sound an AudioEngine plays: a plain value, never reused. The default names no sound.
struct SoundId
{
    std::uint64_t value{ 0 };

    [[nodiscard]] auto valid() const noexcept -> bool { return value != 0; }
    [[nodiscard]] friend auto operator==(const SoundId&, const SoundId&) -> bool = default;
};

/// @brief How a sound plays; AudioEngine::set changes it while it plays.
struct SoundDesc
{
    float volume{ 1.0f };
    /// @brief Playback speed: 2 is an octave up and twice as fast.
    float pitch{ 1.0f };
    bool loop{ false };
    /// @brief Placed at 'position' and heard from the listener (panned and attenuated); otherwise heard as it is.
    bool spatial{ false };
    math::Float3 position{};
    /// @brief Full volume up to this distance from the listener, then inverse attenuation (rolloff 1).
    float minDistance{ 1.0f };
    /// @brief The attenuation stops here: farther away the sound stays as quiet as at this distance.
    float maxDistance{ 100.0f };
};

/// @brief What tests read to see that a sound played without hearing it.
struct AudioStats
{
    std::uint64_t started{ 0 }; ///< Sounds started since the engine was created
    std::uint32_t playing{ 0 }; ///< Sounds playing now: started, and neither finished nor stopped
};

/// @brief miniaudio's engine: the device, the mix of every playing sound and the listener. One per Engine, shared
///        by its worlds (each world's AudioSystem plays its own sounds through it).
class AudioEngine
{
public:
    struct Desc
    {
        /// @brief Play through the default audio device. Without one, nothing is heard and the mix advances by frame
        ///        time only (advance): headless runs and tests, the same on every machine. A device that cannot start
        ///        falls back to that, with a warning.
        bool device{ true };
        /// @brief The mix's format; clips of other rates are resampled.
        std::uint32_t sampleRate{ 48000 };
        std::uint32_t channels{ 2 };
    };

    /// @return ErrorCode::DeviceFailed if miniaudio's engine cannot start even without a device.
    [[nodiscard]] static auto create(const Desc& desc) -> std::expected<std::unique_ptr<AudioEngine>, Error>;

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) = delete;
    ~AudioEngine();

    auto operator=(const AudioEngine&) -> AudioEngine& = delete;
    auto operator=(AudioEngine&&) -> AudioEngine& = delete;

    /// @brief Plays through an audio device (Desc::device, and it started).
    [[nodiscard]] auto hasDevice() const noexcept -> bool;

    /// @brief Starts 'clip' from its beginning. The sound keeps the clip alive while it plays.
    /// @return ErrorCode::NotLoaded for a null or empty clip, ErrorCode::PlayFailed if miniaudio cannot create the
    ///         sound.
    [[nodiscard]] auto play(std::shared_ptr<const assets::AudioClip> clip, const SoundDesc& desc) -> std::expected<SoundId, Error>;

    /// @brief Changes how 'sound' plays; nothing for a sound that finished or was stopped.
    void set(SoundId sound, const SoundDesc& desc);

    /// @brief Stops 'sound' and frees it; nothing for one that finished or was stopped.
    void stop(SoundId sound);

    /// @brief Whether 'sound' is playing: started, and neither finished nor stopped. A looping sound plays until it
    ///        is stopped.
    [[nodiscard]] auto isPlaying(SoundId sound) const -> bool;

    /// @brief Places the listener the spatial sounds are heard from (a world's main camera).
    void setListener(const math::Float3& position, const math::Float3& forward, const math::Float3& up);

    /// @brief Once per frame. Without a device, mixes 'deltaSeconds' of audio (and drops it), so sounds advance by
    ///        frame time; with one, the device mixes on its own thread. Either way frees the sounds that finished.
    void advance(float deltaSeconds);

    [[nodiscard]] auto stats() const -> AudioStats;

private:
    struct Impl;

    explicit AudioEngine(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};
}
