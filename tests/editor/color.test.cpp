#include <doctest/doctest.h>

import std;
import aegis.editor.core;
import aegis.math;

namespace
{
using aegis::editor::iconTint;
using aegis::editor::linearAfterEdit;
using aegis::editor::linearToSrgb;
using aegis::editor::srgbToLinear;
using doctest::Approx;
}

TEST_SUITE("editor::sRGB transfer function")
{
    TEST_CASE("the end points map to themselves")
    {
        CHECK(linearToSrgb(0.0f) == 0.0f);
        CHECK(linearToSrgb(1.0f) == Approx(1.0f));
        CHECK(srgbToLinear(0.0f) == 0.0f);
        CHECK(srgbToLinear(1.0f) == Approx(1.0f));
    }

    TEST_CASE("known values match the standard")
    {
        CHECK(linearToSrgb(0.5f) == Approx(0.735357f).epsilon(1e-5));
        CHECK(srgbToLinear(0.5f) == Approx(0.214041f).epsilon(1e-5));
        CHECK(linearToSrgb(0.8f) == Approx(0.906332f).epsilon(1e-5));
    }

    TEST_CASE("the linear segment joins the power segment at its threshold")
    {
        CHECK(linearToSrgb(0.0031308f) == Approx(0.04045f).epsilon(1e-4));
        CHECK(srgbToLinear(0.04045f) == Approx(0.0031308f).epsilon(1e-4));
        CHECK(linearToSrgb(0.001f) == Approx(0.01292f));
        CHECK(srgbToLinear(0.01f) == Approx(0.01f / 12.92f));
    }

    TEST_CASE("a round trip returns the value")
    {
        for (int i = 0; i <= 100; ++i)
        {
            const float linear = static_cast<float>(i) / 100.0f;
            CHECK(srgbToLinear(linearToSrgb(linear)) == Approx(linear).epsilon(1e-5).scale(1.0));
        }
    }

    TEST_CASE("values above 1 continue the curve")
    {
        CHECK(linearToSrgb(4.0f) > linearToSrgb(1.0f));
        CHECK(srgbToLinear(linearToSrgb(4.0f)) == Approx(4.0f).epsilon(1e-5));
    }

    TEST_CASE("the vector overload converts each channel")
    {
        const glm::vec3 srgb = linearToSrgb(glm::vec3{ 0.0f, 0.5f, 1.0f });
        CHECK(srgb.x == 0.0f);
        CHECK(srgb.y == Approx(linearToSrgb(0.5f)));
        CHECK(srgb.z == Approx(1.0f));
    }
}

TEST_SUITE("editor::linearAfterEdit")
{
    TEST_CASE("an unchanged pick keeps the stored value exactly")
    {
        const glm::vec3 stored{ 0.8f, 0.123456f, 0.3f };
        const glm::vec3 shown = linearToSrgb(stored);
        const glm::vec3 result = linearAfterEdit(stored, shown, shown);
        CHECK(result.x == stored.x);
        CHECK(result.y == stored.y);
        CHECK(result.z == stored.z);
    }

    TEST_CASE("an edited channel is stored linear and the others keep their value")
    {
        const glm::vec3 stored{ 0.8f, 0.123456f, 0.3f };
        const glm::vec3 shown = linearToSrgb(stored);
        glm::vec3 edited = shown;
        edited.y = 0.5f;

        const glm::vec3 result = linearAfterEdit(stored, shown, edited);
        CHECK(result.x == stored.x);
        CHECK(result.y == Approx(srgbToLinear(0.5f)));
        CHECK(result.z == stored.z);
    }
}

TEST_SUITE("editor::iconTint")
{
    TEST_CASE("a color is shown at full brightness, a quarter mixed with white")
    {
        const glm::vec3 tint = iconTint({ 0.2f, 0.0f, 0.0f });
        CHECK(tint.x == Approx(1.0f));
        CHECK(tint.y == Approx(0.25f));
        CHECK(tint.z == Approx(0.25f));
    }

    TEST_CASE("the brightness of the color does not change the tint")
    {
        const glm::vec3 dim = iconTint({ 0.1f, 0.05f, 0.0f });
        const glm::vec3 bright = iconTint({ 20.0f, 10.0f, 0.0f });
        CHECK(dim.x == Approx(bright.x));
        CHECK(dim.y == Approx(bright.y));
        CHECK(dim.z == Approx(bright.z));
        CHECK(dim.y == Approx(linearToSrgb(0.5f) * 0.75f + 0.25f));
    }

    TEST_CASE("black and negative colors are shown white")
    {
        for (const float channel : { 0.0f, -1.0f })
        {
            const glm::vec3 tint = iconTint(glm::vec3{ channel });
            CHECK(tint.x == 1.0f);
            CHECK(tint.y == 1.0f);
            CHECK(tint.z == 1.0f);
        }
    }
}
