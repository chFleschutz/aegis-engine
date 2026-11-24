export module aegis.audio:audio_system;

import std;
import aegis.assets.data;
import aegis.math;
import aegis.world;
import :audio_engine;
import :error;

export namespace aegis::audio
{
/// @brief Plays a world's sounds through the engine's AudioEngine: starts the AudioSource components that play on
///        begin, follows the playing ones' GlobalTransform once per frame and places the listener on the world's
///        main camera. Only a playing world makes sound (it doesn't run in UpdateMode::Edit). It ends its sounds when
///        their entity or AudioSource goes, and all of them when the world is reset or destroyed.
/// @note It makes itself the registry's context (world::Registry::setContext), which play, stop and playOneShot
///       find it through.
class AudioSystem : public world::System
{
public:
    explicit AudioSystem(AudioEngine& engine) :
        m_engine{ &engine }
    {
    }

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) = delete;
    ~AudioSystem() override;

    auto operator=(const AudioSystem&) -> AudioSystem& = delete;
    auto operator=(AudioSystem&&) -> AudioSystem& = delete;

    /// @brief After the transforms and the cameras, whose results it reads
    [[nodiscard]] auto phase() const -> world::Phase override { return world::Phase::LateUpdate; }

    void onAttach(world::Registry& registry) override;
    void onDetach(world::Registry& registry) override;
    void onBegin(world::Registry& registry) override;
    void onUpdate(world::Registry& registry, float deltaSeconds) override;
    void onReset(world::Registry& registry) override;

    /// @brief Starts the entity's AudioSource from the beginning, ending the sound it played before.
    /// @return ErrorCode::NotLoaded if the entity has no AudioSource or its clip is not loaded, or AudioEngine::play's
    ///         errors.
    auto play(world::Registry& registry, world::Entity entity) -> std::expected<void, Error>;

    /// @brief Stops the sound of the entity's AudioSource; nothing if it plays none.
    void stop(world::Entity entity);

    /// @brief Whether the entity's AudioSource is playing.
    [[nodiscard]] auto isPlaying(world::Entity entity) const -> bool;

    /// @brief Plays 'clip' once at 'position' (spatial, with SoundDesc's distances), owned by no entity.
    /// @return ErrorCode::NotLoaded if the clip is not loaded, or AudioEngine::play's errors.
    auto playOneShot(const assets::AssetRef<assets::AudioClip>& clip, const math::Float3& position, float volume) -> std::expected<void, Error>;

private:
    struct EntitySound
    {
        world::Entity entity;
        SoundId sound;
    };

    void stopAll();

    AudioEngine* m_engine;
    std::vector<EntitySound> m_sounds; // The AudioSources playing
    std::vector<SoundId> m_oneShots;   // The one-shots started and not yet seen finished
};

/// @brief Starts the entity's AudioSource in the world of 'registry' (see AudioSystem::play), for scripts.
/// @return ErrorCode::NoAudio if the world has no AudioSystem, or AudioSystem::play's errors.
auto play(world::Registry& registry, world::Entity entity) -> std::expected<void, Error>;

/// @brief Stops the entity's AudioSource in the world of 'registry'; nothing if the world has no AudioSystem.
void stop(world::Registry& registry, world::Entity entity);

/// @brief Plays 'clip' once at 'position' in the world of 'registry' (see AudioSystem::playOneShot), for scripts.
/// @return ErrorCode::NoAudio if the world has no AudioSystem, or AudioSystem::playOneShot's errors.
auto playOneShot(world::Registry& registry, const assets::AssetRef<assets::AudioClip>& clip, const math::Float3& position, float volume = 1.0f)
    -> std::expected<void, Error>;
}
