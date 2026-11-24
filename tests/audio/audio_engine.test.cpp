#include <doctest/doctest.h>

import std;
import aegis.assets.data;
import aegis.audio;

namespace
{
using aegis::audio::AudioEngine;

// A mono clip of 'seconds' at 48 kHz holding a constant.
auto makeClip(float seconds) -> std::shared_ptr<aegis::assets::AudioClip>
{
    auto clip = std::make_shared<aegis::assets::AudioClip>();
    clip->channels = 1;
    clip->sampleRate = 48000;
    clip->frames = static_cast<std::uint64_t>(seconds * 48000.0f);
    clip->samples.assign(clip->frames, 0.25f);
    return clip;
}

// An engine without a device: sounds advance only by AudioEngine::advance.
auto makeEngine() -> std::unique_ptr<AudioEngine>
{
    auto engine = AudioEngine::create({ .device = false });
    REQUIRE(engine);
    REQUIRE_FALSE((*engine)->hasDevice());
    return std::move(*engine);
}
}

TEST_SUITE("audio::AudioEngine")
{
    TEST_CASE("a sound counts as started and plays until its clip ends")
    {
        const auto engine = makeEngine();
        const auto sound = engine->play(makeClip(0.1f), {});
        REQUIRE(sound);
        CHECK(sound->valid());
        CHECK(engine->stats().started == 1);
        CHECK(engine->stats().playing == 1);
        CHECK(engine->isPlaying(*sound));

        engine->advance(0.05f);
        CHECK(engine->isPlaying(*sound));
        engine->advance(0.1f);
        CHECK_FALSE(engine->isPlaying(*sound));
        CHECK(engine->stats().playing == 0);
        CHECK(engine->stats().started == 1);
    }

    TEST_CASE("a looping sound keeps playing past its clip's end")
    {
        const auto engine = makeEngine();
        const auto sound = engine->play(makeClip(0.1f), { .loop = true });
        REQUIRE(sound);
        for (int frame = 0; frame < 60; ++frame)
            engine->advance(1.0f / 60.0f);
        CHECK(engine->isPlaying(*sound));
        CHECK(engine->stats().playing == 1);
    }

    TEST_CASE("set changes a playing sound to stop looping")
    {
        const auto engine = makeEngine();
        const auto sound = engine->play(makeClip(0.1f), { .loop = true });
        REQUIRE(sound);
        engine->advance(0.25f);
        engine->set(*sound, { .loop = false });
        engine->advance(0.25f);
        CHECK_FALSE(engine->isPlaying(*sound));
    }

    TEST_CASE("a stopped sound plays no more")
    {
        const auto engine = makeEngine();
        const auto sound = engine->play(makeClip(1.0f), {});
        REQUIRE(sound);
        engine->stop(*sound);
        CHECK_FALSE(engine->isPlaying(*sound));
        CHECK(engine->stats().playing == 0);
        engine->stop(*sound); // A second stop does nothing
    }

    TEST_CASE("each sound gets its own ID")
    {
        const auto engine = makeEngine();
        const auto clip = makeClip(1.0f);
        const auto first = engine->play(clip, {});
        const auto second = engine->play(clip, { .spatial = true, .position = { 3.0f, 0.0f, 0.0f } });
        REQUIRE(first);
        REQUIRE(second);
        CHECK(*first != *second);
        CHECK(engine->stats().started == 2);
        CHECK(engine->stats().playing == 2);
    }

    TEST_CASE("a clip of another sample rate plays for its own length")
    {
        const auto engine = makeEngine();
        auto clip = makeClip(0.2f);
        clip->sampleRate = 24000; // The same frames now last 0.4 s
        const auto sound = engine->play(clip, {});
        REQUIRE(sound);
        engine->advance(0.3f);
        CHECK(engine->isPlaying(*sound));
        engine->advance(0.2f);
        CHECK_FALSE(engine->isPlaying(*sound));
    }

    TEST_CASE("an empty clip is not played")
    {
        const auto engine = makeEngine();
        CHECK(engine->play(nullptr, {}).error().code == aegis::audio::ErrorCode::NotLoaded);
        CHECK(engine->play(std::make_shared<aegis::assets::AudioClip>(), {}).error().code == aegis::audio::ErrorCode::NotLoaded);
        CHECK(engine->stats().started == 0);
    }
}
