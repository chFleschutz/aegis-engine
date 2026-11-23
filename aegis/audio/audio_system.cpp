module aegis.audio;

import std;
import aegis.core;
import aegis.world;

namespace aegis::audio
{
namespace
{
auto soundDesc(const AudioSource& source, const math::Float3& position) -> SoundDesc
{
    return SoundDesc{
        .volume = source.volume,
        .pitch = source.pitch,
        .loop = source.loop,
        .spatial = source.spatial,
        .position = position,
        .minDistance = source.minDistance,
        .maxDistance = source.maxDistance,
    };
}

auto positionOf(world::Registry& registry, world::Entity entity) -> math::Float3
{
    if (registry.has<GlobalTransform>(entity))
        return registry.get<GlobalTransform>(entity).location;
    return registry.get<Transform>(entity).location;
}
}

AudioSystem::~AudioSystem()
{
    stopAll();
}

void AudioSystem::onAttach(world::Registry& registry)
{
    registry.setContext<AudioSystem>(*this);
    registry.addDestroyListener(this, [this](world::Registry&, world::Entity entity) { stop(entity); });
}

void AudioSystem::onDetach(world::Registry& registry)
{
    stopAll();
    if (registry.context<AudioSystem>() == this)
        registry.eraseContext<AudioSystem>();
    registry.removeDestroyListener(this);
}

void AudioSystem::onBegin(world::Registry& registry)
{
    std::vector<world::Entity> starting;
    for (auto&& [entity, source] : registry.view<AudioSource>().each())
    {
        if (source.playOnBegin)
            starting.emplace_back(entity);
    }
    for (const auto entity : starting)
    {
        if (auto played = play(registry, entity); !played)
            log::warn("An AudioSource does not play: {}", describe(played.error()));
    }
}

void AudioSystem::onUpdate(world::Registry& registry, float)
{
    if (const auto camera = registry.first<MainCamera, GlobalTransform>())
    {
        const auto& transform = registry.get<GlobalTransform>(camera);
        m_engine->setListener(transform.location, transform.forward(), transform.up());
    }

    std::erase_if(m_sounds, [this, &registry](const EntitySound& playing) {
        if (!registry.valid(playing.entity) || !registry.has<AudioSource>(playing.entity) || !m_engine->isPlaying(playing.sound))
        {
            m_engine->stop(playing.sound);
            return true;
        }
        m_engine->set(playing.sound, soundDesc(registry.get<AudioSource>(playing.entity), positionOf(registry, playing.entity)));
        return false;
    });
    std::erase_if(m_oneShots, [this](SoundId sound) { return !m_engine->isPlaying(sound); });
}

void AudioSystem::onReset(world::Registry&)
{
    stopAll();
}

auto AudioSystem::play(world::Registry& registry, world::Entity entity) -> std::expected<void, Error>
{
    if (!registry.valid(entity) || !registry.has<AudioSource>(entity))
        return makeError(ErrorCode::NotLoaded, "The entity has no AudioSource");
    const auto& source = registry.get<AudioSource>(entity);
    if (!source.clip)
        return makeError(ErrorCode::NotLoaded, std::format("The clip {} is not loaded", source.clip.id()));

    stop(entity);
    auto sound = m_engine->play(source.clip.shared(), soundDesc(source, positionOf(registry, entity)));
    if (!sound)
        return std::unexpected{ std::move(sound.error()) };
    m_sounds.push_back({ entity, *sound });
    return {};
}

void AudioSystem::stop(world::Entity entity)
{
    std::erase_if(m_sounds, [this, entity](const EntitySound& playing) {
        if (playing.entity != entity)
            return false;
        m_engine->stop(playing.sound);
        return true;
    });
}

auto AudioSystem::isPlaying(world::Entity entity) const -> bool
{
    return std::ranges::any_of(m_sounds, [this, entity](const EntitySound& playing) { return playing.entity == entity && m_engine->isPlaying(playing.sound); });
}

auto AudioSystem::playOneShot(const assets::AssetRef<assets::AudioClip>& clip, const math::Float3& position, float volume) -> std::expected<void, Error>
{
    if (!clip)
        return makeError(ErrorCode::NotLoaded, std::format("The clip {} is not loaded", clip.id()));
    auto sound = m_engine->play(clip.shared(), { .volume = volume, .spatial = true, .position = position });
    if (!sound)
        return std::unexpected{ std::move(sound.error()) };
    m_oneShots.push_back(*sound);
    return {};
}

void AudioSystem::stopAll()
{
    for (const auto& playing : m_sounds)
        m_engine->stop(playing.sound);
    for (const auto sound : m_oneShots)
        m_engine->stop(sound);
    m_sounds.clear();
    m_oneShots.clear();
}

auto play(world::Registry& registry, world::Entity entity) -> std::expected<void, Error>
{
    auto* system = registry.context<AudioSystem>();
    if (!system)
        return makeError(ErrorCode::NoAudio, "The world has no AudioSystem");
    return system->play(registry, entity);
}

void stop(world::Registry& registry, world::Entity entity)
{
    if (auto* system = registry.context<AudioSystem>())
        system->stop(entity);
}

auto playOneShot(world::Registry& registry, const assets::AssetRef<assets::AudioClip>& clip, const math::Float3& position, float volume)
    -> std::expected<void, Error>
{
    auto* system = registry.context<AudioSystem>();
    if (!system)
        return makeError(ErrorCode::NoAudio, "The world has no AudioSystem");
    return system->playOneShot(clip, position, volume);
}
}
