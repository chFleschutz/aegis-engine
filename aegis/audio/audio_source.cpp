module aegis.audio;

import std;
import aegis.reflect;
import aegis.world;

namespace aegis::audio
{
auto registerComponents(reflect::TypeRegistry& types) -> std::expected<void, reflect::Error>
{
    auto builder = world::registerComponent<AudioSource>(types, "AudioSource");
    if (!builder)
        return std::unexpected{ builder.error() };
    builder->displayName("Audio Source")
        .category("Audio")
        .field("clip", &AudioSource::clip)
        .field("volume", &AudioSource::volume, { .min = 0.0, .step = 0.05 })
        .field("pitch", &AudioSource::pitch, { .min = 0.01, .step = 0.05, .tooltip = "Playback speed: 2 is an octave up" })
        .field("loop", &AudioSource::loop)
        .field("playOnBegin", &AudioSource::playOnBegin, { .tooltip = "Starts when the world begins playing" })
        .field("spatial", &AudioSource::spatial, { .tooltip = "Heard from the entity's position; otherwise the same everywhere" })
        .field("minDistance", &AudioSource::minDistance, { .min = 0.0, .step = 0.1, .unit = "m", .tooltip = "Full volume up to this distance" })
        .field("maxDistance", &AudioSource::maxDistance, { .min = 0.0, .step = 1.0, .unit = "m", .tooltip = "The sound gets no quieter beyond this distance" });
    return builder->status();
}
}
