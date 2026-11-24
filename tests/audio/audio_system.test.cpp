#include <doctest/doctest.h>

import std;
import aegis.assets.data;
import aegis.audio;
import aegis.world;

namespace
{
using namespace aegis;
using audio::AudioSource;

auto makeClip(float seconds) -> assets::AssetRef<assets::AudioClip>
{
    auto clip = std::make_shared<assets::AudioClip>();
    clip->channels = 1;
    clip->sampleRate = 48000;
    clip->frames = static_cast<std::uint64_t>(seconds * 48000.0f);
    clip->samples.assign(clip->frames, 0.25f);
    return assets::AssetRef<assets::AudioClip>::ready(assets::AssetId{ 42 }, std::move(clip));
}

// A world with an AudioSystem playing through an engine without a device.
struct Fixture
{
    std::unique_ptr<audio::AudioEngine> engine;
    world::World world;

    Fixture()
    {
        auto created = audio::AudioEngine::create({ .device = false });
        REQUIRE(created);
        engine = std::move(*created);
        world.addSystem<audio::AudioSystem>(*engine);
    }

    auto registry() -> world::Registry& { return world.registry(); }

    auto addSource(AudioSource source) -> world::Entity
    {
        const auto entity = registry().create("Source");
        registry().add<AudioSource>(entity, std::move(source));
        return entity;
    }

    // Plays 'seconds' at 60 frames a second, as the engine does: the world updates, then the audio advances.
    void run(float seconds)
    {
        constexpr float step{ 1.0f / 60.0f };
        for (float time = 0.0f; time < seconds; time += step)
        {
            world.update(step);
            engine->advance(step);
        }
    }
};
}

TEST_SUITE("audio::AudioSystem")
{
    TEST_CASE("a source that plays on begin is started once")
    {
        Fixture fixture;
        fixture.addSource({ .clip = makeClip(0.5f) });
        fixture.world.begin();
        CHECK(fixture.engine->stats().started == 1);
        fixture.run(0.2f);
        CHECK(fixture.engine->stats().started == 1);
        CHECK(fixture.engine->stats().playing == 1);
    }

    TEST_CASE("a source that does not play on begin waits for play")
    {
        Fixture fixture;
        const auto entity = fixture.addSource({ .clip = makeClip(0.5f), .playOnBegin = false });
        fixture.world.begin();
        CHECK(fixture.engine->stats().started == 0);

        REQUIRE(audio::play(fixture.registry(), entity));
        CHECK(fixture.engine->stats().started == 1);
        CHECK(fixture.world.registry().context<audio::AudioSystem>()->isPlaying(entity));

        audio::stop(fixture.registry(), entity);
        CHECK(fixture.engine->stats().playing == 0);
    }

    TEST_CASE("a looping source keeps playing")
    {
        Fixture fixture;
        const auto entity = fixture.addSource({ .clip = makeClip(0.1f), .loop = true });
        fixture.world.begin();
        fixture.run(1.0f);
        CHECK(fixture.registry().context<audio::AudioSystem>()->isPlaying(entity));
        CHECK(fixture.engine->stats().playing == 1);
        CHECK(fixture.engine->stats().started == 1);
    }

    TEST_CASE("a source that does not loop ends with its clip")
    {
        Fixture fixture;
        const auto entity = fixture.addSource({ .clip = makeClip(0.1f) });
        fixture.world.begin();
        fixture.run(0.5f);
        CHECK_FALSE(fixture.registry().context<audio::AudioSystem>()->isPlaying(entity));
        CHECK(fixture.engine->stats().playing == 0);
    }

    TEST_CASE("a one-shot is counted")
    {
        Fixture fixture;
        fixture.world.begin();
        REQUIRE(audio::playOneShot(fixture.registry(), makeClip(0.1f), { 1.0f, 2.0f, 3.0f }, 0.5f));
        CHECK(fixture.engine->stats().started == 1);
        CHECK(fixture.engine->stats().playing == 1);
        fixture.run(0.5f);
        CHECK(fixture.engine->stats().playing == 0);
    }

    TEST_CASE("a clip that is not loaded does not play")
    {
        Fixture fixture;
        const assets::AssetRef<assets::AudioClip> pending{ assets::AssetId{ 7 } };
        const auto entity = fixture.addSource({ .clip = pending, .playOnBegin = false });
        CHECK(audio::play(fixture.registry(), entity).error().code == audio::ErrorCode::NotLoaded);
        CHECK(audio::playOneShot(fixture.registry(), pending, {}).error().code == audio::ErrorCode::NotLoaded);
        CHECK(fixture.engine->stats().started == 0);
    }

    TEST_CASE("an entity without an AudioSource does not play")
    {
        Fixture fixture;
        const auto entity = fixture.registry().create("Silent");
        CHECK(audio::play(fixture.registry(), entity).error().code == audio::ErrorCode::NotLoaded);
    }

    TEST_CASE("destroying the entity stops its sound")
    {
        Fixture fixture;
        const auto entity = fixture.addSource({ .clip = makeClip(1.0f) });
        fixture.world.begin();
        REQUIRE(fixture.engine->stats().playing == 1);
        fixture.registry().destroy(entity);
        CHECK(fixture.engine->stats().playing == 0);
    }

    TEST_CASE("removing the AudioSource stops its sound on the next update")
    {
        Fixture fixture;
        const auto entity = fixture.addSource({ .clip = makeClip(1.0f) });
        fixture.world.begin();
        fixture.registry().remove<AudioSource>(entity);
        fixture.world.update(0.01f);
        CHECK(fixture.engine->stats().playing == 0);
    }

    TEST_CASE("resetting the world stops every sound and removes the system")
    {
        Fixture fixture;
        fixture.addSource({ .clip = makeClip(1.0f), .loop = true });
        fixture.world.begin();
        REQUIRE(audio::playOneShot(fixture.registry(), makeClip(1.0f), {}));
        REQUIRE(fixture.engine->stats().playing == 2);

        fixture.world.reset();
        CHECK(fixture.engine->stats().playing == 0);
        CHECK(fixture.registry().context<audio::AudioSystem>() == nullptr);
        CHECK(audio::playOneShot(fixture.registry(), makeClip(1.0f), {}).error().code == audio::ErrorCode::NoAudio);
    }

    TEST_CASE("a world in edit mode makes no sound")
    {
        Fixture fixture;
        fixture.addSource({ .clip = makeClip(1.0f) });
        fixture.world.begin(world::UpdateMode::Edit);
        fixture.world.update(0.1f, world::UpdateMode::Edit);
        CHECK(fixture.engine->stats().started == 0);
    }

    TEST_CASE("a world without an AudioSystem has no audio")
    {
        world::World world;
        const auto entity = world.registry().create("Source");
        CHECK(audio::play(world.registry(), entity).error().code == audio::ErrorCode::NoAudio);
        audio::stop(world.registry(), entity); // Nothing to stop
        CHECK(audio::playOneShot(world.registry(), makeClip(0.1f), {}).error().code == audio::ErrorCode::NoAudio);
    }
}
