export module aegis.audio:audio_source;

import std;
import aegis.assets.data;
import aegis.reflect;

export namespace aegis::audio
{
/// @brief A sound an entity plays, from where the entity is. A playing world's AudioSystem starts it on begin
///        (playOnBegin) or when a script calls audio::play, and follows the entity's GlobalTransform while it plays.
struct AudioSource
{
    assets::AssetRef<assets::AudioClip> clip;
    float volume{ 1.0f };
    /// @brief Playback speed: 2 is an octave up and twice as fast.
    float pitch{ 1.0f };
    bool loop{ false };
    bool playOnBegin{ true };
    /// @brief Heard from the entity's position (panned and attenuated); otherwise heard the same everywhere.
    bool spatial{ true };
    /// @brief Full volume up to this distance from the listener, then inverse attenuation.
    float minDistance{ 1.0f };
    /// @brief The attenuation stops here.
    float maxDistance{ 100.0f };
};

/// @brief Registers AudioSource (see world::registerComponent), so a scene file saves it and the editor shows it.
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>;
}
