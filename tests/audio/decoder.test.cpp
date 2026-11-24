#include <doctest/doctest.h>

import std;
import aegis.audio;

namespace
{
auto readFile(const std::filesystem::path& path) -> std::vector<std::byte>
{
    std::ifstream file{ path, std::ios::binary };
    REQUIRE(file);
    std::vector<char> chars{ std::istreambuf_iterator<char>{ file }, {} };
    std::vector<std::byte> bytes(chars.size());
    std::memcpy(bytes.data(), chars.data(), chars.size());
    return bytes;
}

auto fixture(std::string_view name) -> std::vector<std::byte>
{
    return readFile(std::filesystem::path{ AEGIS_TEST_AUDIO_DIR } / name);
}
}

TEST_SUITE("audio::decodeClip")
{
    TEST_CASE("a WAV decodes to its frames, channels and sample rate")
    {
        const auto clip = aegis::audio::decodeClip(fixture("tone_mono.wav"), "tone_mono.wav");
        REQUIRE(clip);
        CHECK(clip->channels == 1);
        CHECK(clip->sampleRate == 22050);
        CHECK(clip->frames == 4410);
        CHECK(clip->samples.size() == 4410);
        CHECK(clip->seconds() == doctest::Approx(0.2f));
        // A 0.5 amplitude sine: never silent, never past full scale
        const auto peak = std::ranges::max(clip->samples, {}, [](float sample) { return std::abs(sample); });
        CHECK(std::abs(peak) == doctest::Approx(0.5f).epsilon(0.02));
    }

    TEST_CASE("an Ogg Vorbis file decodes to its frames, channels and sample rate")
    {
        const auto clip = aegis::audio::decodeClip(fixture("tone_stereo.ogg"), "tone_stereo.ogg");
        REQUIRE(clip);
        CHECK(clip->channels == 2);
        CHECK(clip->sampleRate == 44100);
        CHECK(clip->frames == 22050);
        CHECK(clip->samples.size() == 2 * 22050);
    }

    TEST_CASE("bytes of no audio format are an error")
    {
        const std::array junk{ std::byte{ 1 }, std::byte{ 2 }, std::byte{ 3 }, std::byte{ 4 } };
        const auto clip = aegis::audio::decodeClip(junk, "junk.wav");
        REQUIRE_FALSE(clip);
        CHECK(clip.error().code == aegis::audio::ErrorCode::DecodeFailed);
        CHECK(aegis::audio::describe(clip.error()).contains("junk.wav"));
    }
}
