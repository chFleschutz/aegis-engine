module;

#include <miniaudio.h>

module aegis.audio;

import std;
import aegis.core;

namespace aegis::audio
{
namespace
{
// A playing sound: the clip it reads, a cursor over the clip's samples and miniaudio's sound. miniaudio keeps
// pointers to the cursor and the sound, so a Voice never moves.
struct Voice
{
    std::shared_ptr<const assets::AudioClip> clip;
    ma_audio_buffer_ref buffer{};
    ma_sound sound{};
    bool bufferReady{ false };
    bool soundReady{ false };

    Voice() = default;
    Voice(const Voice&) = delete;
    Voice(Voice&&) = delete;
    auto operator=(const Voice&) -> Voice& = delete;
    auto operator=(Voice&&) -> Voice& = delete;

    ~Voice()
    {
        if (soundReady)
            ma_sound_uninit(&sound);
        if (bufferReady)
            ma_audio_buffer_ref_uninit(&buffer);
    }
};

void apply(ma_sound& sound, const SoundDesc& desc)
{
    ma_sound_set_volume(&sound, desc.volume);
    ma_sound_set_pitch(&sound, desc.pitch);
    ma_sound_set_looping(&sound, desc.loop ? MA_TRUE : MA_FALSE);
    ma_sound_set_spatialization_enabled(&sound, desc.spatial ? MA_TRUE : MA_FALSE);
    ma_sound_set_position(&sound, desc.position.x, desc.position.y, desc.position.z);
    ma_sound_set_min_distance(&sound, desc.minDistance);
    ma_sound_set_max_distance(&sound, desc.maxDistance);
}

auto playing(const Voice& voice) -> bool
{
    return ma_sound_is_playing(&voice.sound) == MA_TRUE && ma_sound_at_end(&voice.sound) == MA_FALSE;
}
}

struct AudioEngine::Impl
{
    ma_engine engine{};
    bool device{ false };
    std::uint32_t sampleRate{ 0 };
    std::uint32_t channels{ 0 };
    std::unordered_map<std::uint64_t, std::unique_ptr<Voice>> voices;
    std::uint64_t nextId{ 1 };
    std::uint64_t started{ 0 };
    double pendingFrames{ 0.0 }; // The part of a frame advance has not mixed yet
    std::vector<float> scratch;  // What advance mixes into without a device

    Impl() = default;
    Impl(const Impl&) = delete;
    Impl(Impl&&) = delete;
    auto operator=(const Impl&) -> Impl& = delete;
    auto operator=(Impl&&) -> Impl& = delete;

    ~Impl()
    {
        voices.clear(); // Before the engine they play in
        ma_engine_uninit(&engine);
    }

    [[nodiscard]] auto find(SoundId sound) const -> Voice*
    {
        const auto it = voices.find(sound.value);
        return it == voices.end() ? nullptr : it->second.get();
    }
};

auto AudioEngine::create(const Desc& desc) -> std::expected<std::unique_ptr<AudioEngine>, Error>
{
    auto impl = std::make_unique<Impl>();
    impl->sampleRate = desc.sampleRate;
    impl->channels = desc.channels;

    auto config = ma_engine_config_init();
    config.channels = desc.channels;
    config.sampleRate = desc.sampleRate;
    if (desc.device)
    {
        if (const auto result = ma_engine_init(&config, &impl->engine); result == MA_SUCCESS)
            impl->device = true;
        else
            log::warn("No audio device ({}): sound is mixed without one and not heard", ma_result_description(result));
    }
    if (!impl->device)
    {
        config.noDevice = MA_TRUE;
        if (const auto result = ma_engine_init(&config, &impl->engine); result != MA_SUCCESS)
            return makeError(ErrorCode::DeviceFailed, std::format("Cannot start miniaudio's engine ({})", ma_result_description(result)));
    }
    return std::unique_ptr<AudioEngine>{ new AudioEngine{ std::move(impl) } };
}

AudioEngine::AudioEngine(std::unique_ptr<Impl> impl) :
    m_impl{ std::move(impl) }
{
}

AudioEngine::~AudioEngine() = default;

auto AudioEngine::hasDevice() const noexcept -> bool
{
    return m_impl->device;
}

auto AudioEngine::play(std::shared_ptr<const assets::AudioClip> clip, const SoundDesc& desc) -> std::expected<SoundId, Error>
{
    if (!clip || clip->frames == 0 || clip->channels == 0)
        return makeError(ErrorCode::NotLoaded, "The clip is not loaded or holds no audio");

    auto voice = std::make_unique<Voice>();
    voice->clip = std::move(clip);
    const auto& samples = *voice->clip;
    if (const auto result = ma_audio_buffer_ref_init(ma_format_f32, samples.channels, samples.samples.data(), samples.frames, &voice->buffer);
        result != MA_SUCCESS)
        return makeError(ErrorCode::PlayFailed, std::format("Cannot read the clip ({})", ma_result_description(result)));
    voice->bufferReady = true;
    voice->buffer.sampleRate = samples.sampleRate; // The sound resamples from the clip's rate to the mix's
    if (const auto result = ma_sound_init_from_data_source(&m_impl->engine, &voice->buffer, 0, nullptr, &voice->sound); result != MA_SUCCESS)
        return makeError(ErrorCode::PlayFailed, std::format("Cannot create a sound ({})", ma_result_description(result)));
    voice->soundReady = true;

    ma_sound_set_attenuation_model(&voice->sound, ma_attenuation_model_inverse);
    ma_sound_set_rolloff(&voice->sound, 1.0f);
    apply(voice->sound, desc);
    if (const auto result = ma_sound_start(&voice->sound); result != MA_SUCCESS)
        return makeError(ErrorCode::PlayFailed, std::format("Cannot start a sound ({})", ma_result_description(result)));

    const SoundId id{ m_impl->nextId++ };
    m_impl->voices.emplace(id.value, std::move(voice));
    ++m_impl->started;
    return id;
}

void AudioEngine::set(SoundId sound, const SoundDesc& desc)
{
    if (auto* voice = m_impl->find(sound))
        apply(voice->sound, desc);
}

void AudioEngine::stop(SoundId sound)
{
    if (auto* voice = m_impl->find(sound))
    {
        ma_sound_stop(&voice->sound);
        m_impl->voices.erase(sound.value);
    }
}

auto AudioEngine::isPlaying(SoundId sound) const -> bool
{
    const auto* voice = m_impl->find(sound);
    return voice && playing(*voice);
}

void AudioEngine::setListener(const math::Float3& position, const math::Float3& forward, const math::Float3& up)
{
    ma_engine_listener_set_position(&m_impl->engine, 0, position.x, position.y, position.z);
    ma_engine_listener_set_direction(&m_impl->engine, 0, forward.x, forward.y, forward.z);
    ma_engine_listener_set_world_up(&m_impl->engine, 0, up.x, up.y, up.z);
}

void AudioEngine::advance(float deltaSeconds)
{
    if (!m_impl->device && deltaSeconds > 0.0f)
    {
        m_impl->pendingFrames += static_cast<double>(deltaSeconds) * m_impl->sampleRate;
        auto frames = static_cast<ma_uint64>(m_impl->pendingFrames);
        m_impl->pendingFrames -= static_cast<double>(frames);
        constexpr ma_uint64 chunkFrames{ 4096 };
        m_impl->scratch.resize(chunkFrames * m_impl->channels);
        while (frames > 0)
        {
            const auto chunk = std::min(frames, chunkFrames);
            ma_engine_read_pcm_frames(&m_impl->engine, m_impl->scratch.data(), chunk, nullptr);
            frames -= chunk;
        }
    }
    std::erase_if(m_impl->voices, [](const auto& entry) { return !playing(*entry.second); });
}

auto AudioEngine::stats() const -> AudioStats
{
    const auto playingCount = std::ranges::count_if(m_impl->voices, [](const auto& entry) { return playing(*entry.second); });
    return AudioStats{ .started = m_impl->started, .playing = static_cast<std::uint32_t>(playingCount) };
}
}
